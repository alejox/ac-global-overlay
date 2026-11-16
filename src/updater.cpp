/*
AC Global Overlay
Copyright (C) 2026 alejoxcode

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program. If not, see <https://www.gnu.org/licenses/>
*/

#include "updater.hpp"

#include <obs-module.h>
#include <plugin-support.h>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QVersionNumber>

namespace {

const char *LATEST_RELEASE_URL = "https://api.github.com/repos/alejox/ac-global-overlay/releases/latest";
const char *DOWNLOAD_PREFIX = "https://github.com/alejox/ac-global-overlay/releases/download/";
const char *BUNDLE_NAME = "ac-global-overlay.plugin";
const char *ARCHIVE_SUFFIX = "-macos-universal.tar.xz";

#ifdef __APPLE__
const char *CURL = "/usr/bin/curl";
const char *TAR = "/usr/bin/tar";
#else
const char *CURL = "curl";
#endif

QVersionNumber parseVersion(QString text)
{
	if (text.startsWith('v') || text.startsWith('V'))
		text.remove(0, 1);
	return QVersionNumber::fromString(text);
}

} // namespace

Updater::Updater(QObject *parent) : QObject(parent) {}

void Updater::run(const QString &program, const QStringList &arguments, const Callback &callback)
{
	auto *process = new QProcess(this);

	connect(process, &QProcess::finished, this, [process, callback](int exitCode, QProcess::ExitStatus status) {
		const bool ok = status == QProcess::NormalExit && exitCode == 0;
		callback(ok, process->readAllStandardOutput());
		process->deleteLater();
	});
	connect(process, &QProcess::errorOccurred, this, [process, callback](QProcess::ProcessError error) {
		/* Any other error is followed by finished(). */
		if (error == QProcess::FailedToStart) {
			callback(false, QByteArray());
			process->deleteLater();
		}
	});

	process->start(program, arguments);
}

void Updater::check()
{
	if (started_)
		return;
	started_ = true;

	const QStringList arguments = {"-fsSL",
				       "--proto",
				       "=https",
				       "--proto-redir",
				       "=https",
				       "-m",
				       "20",
				       "-H",
				       "Accept: application/vnd.github+json",
				       "-H",
				       "User-Agent: ac-global-overlay",
				       LATEST_RELEASE_URL};

	run(CURL, arguments, [this](bool ok, const QByteArray &output) {
		if (ok)
			onLatestRelease(output);
		else
			obs_log(LOG_INFO, "update check skipped: could not reach GitHub");
	});
}

void Updater::onLatestRelease(const QByteArray &json)
{
	const QJsonObject release = QJsonDocument::fromJson(json).object();
	const QString tag = release.value("tag_name").toString();
	const QVersionNumber latest = parseVersion(tag);
	const QVersionNumber current = parseVersion(QString::fromUtf8(PLUGIN_VERSION));

	if (latest.isNull() || QVersionNumber::compare(latest, current) <= 0) {
		obs_log(LOG_INFO, "update check: version %s is up to date", PLUGIN_VERSION);
		return;
	}

	version_ = latest.toString();
	pageUrl_ = release.value("html_url").toString();
	obs_log(LOG_INFO, "update check: version %s is available", version_.toUtf8().constData());

#ifdef __APPLE__
	const QJsonArray assets = release.value("assets").toArray();
	for (qsizetype i = 0; i < assets.size(); i++) {
		const QJsonObject asset = assets.at(i).toObject();
		const QString url = asset.value("browser_download_url").toString();
		/* Only archives published in this plugin's own releases are accepted. */
		if (asset.value("name").toString().endsWith(ARCHIVE_SUFFIX) && url.startsWith(DOWNLOAD_PREFIX)) {
			assetUrl_ = url;
			break;
		}
	}

	/* .../plugins/ac-global-overlay.plugin/Contents/MacOS/ac-global-overlay */
	const char *binary = obs_get_module_binary_path(obs_current_module());
	QDir bundle = QFileInfo(QString::fromUtf8(binary ? binary : "")).dir();
	const bool insideBundle = bundle.cdUp() && bundle.cdUp() && bundle.dirName() == BUNDLE_NAME;
	bundlePath_ = bundle.absolutePath();

	QDir pluginsDir = bundle;
	const bool hasParent = pluginsDir.cdUp();
	const bool userInstall = insideBundle && hasParent && bundlePath_.startsWith(QDir::homePath() + "/") &&
				 QFileInfo(pluginsDir.absolutePath()).isWritable();

	if (!assetUrl_.isEmpty() && userInstall) {
		QDir dataDir = pluginsDir;
		dataDir.cdUp();
		stagingPath_ = dataDir.absoluteFilePath("ac-global-overlay-update");
		download();
		return;
	}
#endif

	emit available(version_, pageUrl_);
}

void Updater::download()
{
#ifdef __APPLE__
	QDir(stagingPath_).removeRecursively();
	if (!QDir().mkpath(stagingPath_)) {
		emit available(version_, pageUrl_);
		return;
	}

	const QStringList arguments = {"-fsSL",         "--proto", "=https",
				       "--proto-redir", "=https",  "-m",
				       "120",           "-o",      stagingPath_ + "/update.tar.xz",
				       assetUrl_};

	run(CURL, arguments, [this](bool ok, const QByteArray &) {
		if (ok) {
			extract();
		} else {
			obs_log(LOG_WARNING, "update: the download failed");
			cleanUp();
			emit available(version_, pageUrl_);
		}
	});
#endif
}

void Updater::extract()
{
#ifdef __APPLE__
	const QStringList arguments = {"-xJf", stagingPath_ + "/update.tar.xz", "-C", stagingPath_};

	run(TAR, arguments, [this](bool ok, const QByteArray &) {
		const QString binary = stagingPath_ + "/" + BUNDLE_NAME + "/Contents/MacOS/ac-global-overlay";
		if (ok && QFileInfo(binary).isFile()) {
			swapBundle();
		} else {
			obs_log(LOG_WARNING, "update: the downloaded archive is not a valid plugin");
			cleanUp();
			emit available(version_, pageUrl_);
		}
	});
#endif
}

void Updater::swapBundle()
{
	/* The running copy is moved aside, never deleted first, so a failed swap
	 * leaves the installed plugin exactly as it was. */
	const QString fresh = stagingPath_ + "/" + BUNDLE_NAME;
	const QString previous = stagingPath_ + "/previous.plugin";
	QDir dir;

	if (!dir.rename(bundlePath_, previous)) {
		obs_log(LOG_WARNING, "update: could not replace the installed plugin");
		cleanUp();
		emit available(version_, pageUrl_);
		return;
	}

	if (!dir.rename(fresh, bundlePath_)) {
		dir.rename(previous, bundlePath_);
		obs_log(LOG_WARNING, "update: could not install the new plugin, the previous one was restored");
		cleanUp();
		emit available(version_, pageUrl_);
		return;
	}

	cleanUp();
	obs_log(LOG_INFO, "update: version %s installed, it loads on the next OBS start",
		version_.toUtf8().constData());
	emit installed(version_);
}

void Updater::cleanUp()
{
	if (!stagingPath_.isEmpty())
		QDir(stagingPath_).removeRecursively();
}
