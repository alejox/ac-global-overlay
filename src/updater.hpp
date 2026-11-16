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

#pragma once

#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>

#include <functional>

/* Looks for a newer release on GitHub once per OBS session.
 *
 * On macOS, when the plugin lives in the user's own plugins folder, the new
 * bundle is downloaded and swapped in place, and takes effect on the next OBS
 * start. Everywhere else it only reports that a newer version exists. */
class Updater : public QObject {
	Q_OBJECT

public:
	explicit Updater(QObject *parent = nullptr);
	void check();

signals:
	void installed(const QString &version);
	void available(const QString &version, const QString &url);

private:
	using Callback = std::function<void(bool ok, const QByteArray &output)>;

	void run(const QString &program, const QStringList &arguments, const Callback &callback);
	void onLatestRelease(const QByteArray &json);
	void download();
	void extract();
	void swapBundle();
	void cleanUp();

	bool started_ = false;
	QString version_;
	QString pageUrl_;
	QString assetUrl_;
	QString bundlePath_;
	QString stagingPath_;
};
