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

#include <obs-frontend-api.h>

#include <QAbstractButton>
#include <QListWidget>
#include <QString>
#include <QWidget>

class OverlayManager;
class Updater;
struct OverlayItem;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

class ToggleSwitch : public QAbstractButton {
public:
	explicit ToggleSwitch(QWidget *parent = nullptr);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;
};

/* Arrow button painted by the plugin, so it never depends on a font glyph
 * or on the active OBS theme. */
class ArrowButton : public QAbstractButton {
public:
	enum class Kind { Back, Forward, Locate };

	explicit ArrowButton(Kind kind, QWidget *parent = nullptr);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	Kind kind_;
};

class ReorderList : public QListWidget {
	Q_OBJECT

public:
	explicit ReorderList(QWidget *parent = nullptr);

signals:
	void reordered();

protected:
	void dropEvent(QDropEvent *event) override;
};

class OverlayDock : public QWidget {
	Q_OBJECT

public:
	explicit OverlayDock(QWidget *parent = nullptr);
	~OverlayDock() override;

private:
	QWidget *buildListPage();
	QWidget *buildScenesPage();
	QWidget *buildEmptyPage();

	void scheduleRefresh();
	void refresh();
	void rebuildList();
	void rebuildScenes();
	QString summaryHtml(const OverlayItem &item, int sceneTotal) const;
	void applySceneFilter();
	void showAddMenu(QWidget *anchor);
	void showRowMenu(QString uuid, QPoint globalPos);
	void moveRow(const QString &uuid, int delta);
	void goToSource(const QString &uuid);
	void showSupport();
	void switchToScene(const QString &sceneUuid);

	static void onFrontendEvent(enum obs_frontend_event event, void *param);
	static void onSaveLoad(obs_data_t *saveData, bool saving, void *param);

	OverlayManager *manager_ = nullptr;
	Updater *updater_ = nullptr;
	QWidget *updateBanner_ = nullptr;
	QLabel *updateText_ = nullptr;
	QPushButton *updateButton_ = nullptr;
	QString updateUrl_;
	QWidget *editBanner_ = nullptr;
	QLabel *editText_ = nullptr;
	QPushButton *editReturn_ = nullptr;
	QString returnUuid_;
	QString returnName_;
	QStackedWidget *stack_ = nullptr;
	QWidget *listPage_ = nullptr;
	QWidget *scenesPage_ = nullptr;
	QWidget *emptyPage_ = nullptr;

	ToggleSwitch *masterSwitch_ = nullptr;
	QLabel *masterStatus_ = nullptr;
	QLabel *sceneName_ = nullptr;
	QLabel *sceneStatus_ = nullptr;
	QPushButton *sceneButton_ = nullptr;
	ReorderList *list_ = nullptr;

	QLabel *detailTitle_ = nullptr;
	QLabel *detailCount_ = nullptr;
	QLineEdit *detailSearch_ = nullptr;
	QWidget *detailScenes_ = nullptr;
	QVBoxLayout *detailScenesLayout_ = nullptr;
	ToggleSwitch *detailShowInNew_ = nullptr;
	QWidget *detailModeBar_ = nullptr;
	QPushButton *detailModeAll_ = nullptr;
	QPushButton *detailModeGroups_ = nullptr;
	QWidget *detailGroups_ = nullptr;
	QVBoxLayout *detailGroupsLayout_ = nullptr;
	QLabel *detailGroupNotice_ = nullptr;
	QPushButton *detailExceptionsToggle_ = nullptr;
	QWidget *detailExceptions_ = nullptr;
	QWidget *detailSpacer_ = nullptr;
	bool exceptionsOpen_ = false;
	QWidget *detailAllNone_ = nullptr;
	QWidget *detailNewScenes_ = nullptr;
	QLabel *detailGroupHint_ = nullptr;

	QString detailUuid_;
	bool refreshPending_ = false;
};
