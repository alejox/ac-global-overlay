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

#include <obs.hpp>

#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

/* A scene group published by AC Scene Groups. */
struct GroupInfo {
	QString id;
	QString name;
	QString color;
	QStringList scenes;
	/* False when the group only survives as the copy stored with an overlay. */
	bool available = true;
};

struct OverlayItem {
	QString uuid;
	QString name;
	bool isScene = false;
	bool enabled = true;
	/* When true, `scenes` lists the scenes where the overlay is hidden.
	 * When false, `scenes` lists the only scenes where it is shown. */
	bool showInNew = true;
	QSet<QString> scenes;
	/* "Only in groups" mode: the overlay follows the listed scene groups and
	 * `groupHidden` holds the scenes of those groups where it is hidden.
	 * `groupSnapshots` keeps the last known content of each listed group, used
	 * when AC Scene Groups is missing or the group no longer exists. */
	bool groupMode = false;
	QStringList groupIds;
	QHash<QString, GroupInfo> groupSnapshots;
	QSet<QString> groupHidden;
	OBSSceneItem sceneItem;
};

struct SourceInfo {
	QString uuid;
	QString name;
};

class OverlayManager : public QObject {
	Q_OBJECT

public:
	explicit OverlayManager(QObject *parent = nullptr);
	~OverlayManager() override;

	const QList<OverlayItem> &items() const { return items_; }
	const OverlayItem *find(const QString &uuid) const;

	bool masterEnabled() const { return master_; }
	void setMasterEnabled(bool enabled);

	bool addSource(const QString &uuid);
	void removeItem(const QString &uuid);
	void setItemEnabled(const QString &uuid, bool enabled);
	void setOrder(const QStringList &uuids);

	bool isVisibleIn(const OverlayItem &item, const QString &sceneUuid) const;
	void setVisibleIn(const QString &uuid, const QString &sceneUuid, bool visible);
	void setVisibleInAll(const QString &uuid, bool visible);
	void setShowInNew(const QString &uuid, bool showInNew);
	int hiddenSceneCount(const OverlayItem &item) const;

	bool groupsAvailable() const { return groupsAvailable_; }
	void refreshGroups();
	void setGroupMode(const QString &uuid, bool groupMode);
	void setGroupSelected(const QString &uuid, const QString &groupId, bool selected);
	QList<GroupInfo> selectedGroups(const OverlayItem &item) const;
	QList<GroupInfo> listedGroups(const OverlayItem &item) const;
	QSet<QString> groupScenes(const OverlayItem &item) const;

	SourceInfo currentScene() const;
	bool canShowInCurrentScene() const;
	bool currentSceneHidden() const;
	void setCurrentSceneHidden(bool hidden);
	int visibleNowCount() const;

	QList<SourceInfo> allScenes() const;
	QList<SourceInfo> addableScenes() const;
	QList<SourceInfo> addableSources() const;

	void load(obs_data_t *data);
	void save(obs_data_t *data) const;
	void clear();
	void apply();

signals:
	void changed();

private:
	int indexOf(const QString &uuid) const;
	void ensureScene();
	void applyOrder();
	void userChanged();
	void sourceRemoved(const QString &uuid);
	const GroupInfo *liveGroup(const QString &id) const;
	bool updateSnapshots();

	static void onSourceRemove(void *param, calldata_t *data);
	static void onSourceRename(void *param, calldata_t *data);
	static void onGroupsChanged(void *param, calldata_t *data);

	QList<OverlayItem> items_;
	obs_scene_t *scene_ = nullptr;
	bool master_ = true;

	QList<GroupInfo> groups_;
	bool groupsAvailable_ = false;
	bool groupSignalConnected_ = false;
};
