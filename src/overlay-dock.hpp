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
	void applySceneFilter();
	void showAddMenu(QWidget *anchor);
	void showRowMenu(QString uuid, QPoint globalPos);
	void moveRow(const QString &uuid, int delta);

	static void onFrontendEvent(enum obs_frontend_event event, void *param);
	static void onSaveLoad(obs_data_t *saveData, bool saving, void *param);

	OverlayManager *manager_ = nullptr;
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

	QString detailUuid_;
	bool refreshPending_ = false;
};
