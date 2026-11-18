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

#include "overlay-manager.hpp"

#include <obs-frontend-api.h>
#include <obs-module.h>
#include <plugin-support.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace {

/* Output channels 0-6 are used by OBS itself and 7+ by other keyer plugins,
 * so the overlay sits on a high channel to stay above all of them. */
const uint32_t OVERLAY_CHANNEL = 56;

/* Contract with the AC Scene Groups plugin. */
const char *GROUPS_PROC = "ac_scene_groups_get";
const char *GROUPS_SIGNAL = "ac_scene_groups_changed";

bool sameGroups(const QList<GroupInfo> &a, const QList<GroupInfo> &b)
{
	if (a.size() != b.size())
		return false;
	for (int i = 0; i < a.size(); i++) {
		if (a[i].id != b[i].id || a[i].name != b[i].name || a[i].color != b[i].color ||
		    a[i].scenes != b[i].scenes)
			return false;
	}
	return true;
}

QString uuidOf(const obs_source_t *source)
{
	const char *uuid = source ? obs_source_get_uuid(source) : nullptr;
	return uuid ? QString::fromUtf8(uuid) : QString();
}

QString nameOf(const obs_source_t *source)
{
	const char *name = source ? obs_source_get_name(source) : nullptr;
	return name ? QString::fromUtf8(name) : QString();
}

void copyTransform(obs_sceneitem_t *from, obs_sceneitem_t *to)
{
	struct obs_transform_info info = {};
	struct obs_sceneitem_crop crop = {};
	obs_sceneitem_get_info2(from, &info);
	obs_sceneitem_get_crop(from, &crop);
	obs_sceneitem_set_info2(to, &info);
	obs_sceneitem_set_crop(to, &crop);
}

struct StageSearch {
	QString uuid;
	obs_sceneitem_t *found = nullptr;
};

bool isAudioOnly(const obs_source_t *source)
{
	const uint32_t flags = obs_source_get_output_flags(source);
	return (flags & OBS_SOURCE_AUDIO) != 0 && (flags & OBS_SOURCE_VIDEO) == 0;
}

bool sortByName(const SourceInfo &a, const SourceInfo &b)
{
	return a.name.localeAwareCompare(b.name) < 0;
}

/* Reuses the position the source already has in one of the user's scenes,
 * so a logo or a camera frame lands where the user placed it. */
void copyExistingTransform(obs_source_t *source, obs_sceneitem_t *target)
{
	const char *name = obs_source_get_name(source);
	if (!name)
		return;

	obs_sceneitem_t *found = nullptr;

	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	if (current)
		found = obs_scene_find_source(obs_scene_from_source(current), name);

	struct obs_frontend_source_list scenes = {};
	if (!found) {
		obs_frontend_get_scenes(&scenes);
		for (size_t i = 0; i < scenes.sources.num && !found; i++)
			found = obs_scene_find_source(obs_scene_from_source(scenes.sources.array[i]), name);
	}

	if (found) {
		struct obs_transform_info info = {};
		struct obs_sceneitem_crop crop = {};
		obs_sceneitem_get_info2(found, &info);
		obs_sceneitem_get_crop(found, &crop);
		obs_sceneitem_set_info2(target, &info);
		obs_sceneitem_set_crop(target, &crop);
	}

	obs_frontend_source_list_free(&scenes);
}

} // namespace

OverlayManager::OverlayManager(QObject *parent) : QObject(parent)
{
	signal_handler_t *handler = obs_get_signal_handler();
	signal_handler_connect(handler, "source_remove", onSourceRemove, this);
	signal_handler_connect(handler, "source_rename", onSourceRename, this);
}

OverlayManager::~OverlayManager()
{
	signal_handler_t *handler = obs_get_signal_handler();
	if (handler) {
		signal_handler_disconnect(handler, "source_remove", onSourceRemove, this);
		signal_handler_disconnect(handler, "source_rename", onSourceRename, this);
		if (groupSignalConnected_)
			signal_handler_disconnect(handler, GROUPS_SIGNAL, onGroupsChanged, this);
	}
	clear();
}

void OverlayManager::onSourceRemove(void *param, calldata_t *data)
{
	auto *self = static_cast<OverlayManager *>(param);
	auto *source = static_cast<obs_source_t *>(calldata_ptr(data, "source"));
	const QString uuid = uuidOf(source);
	if (uuid.isEmpty())
		return;

	/* Queued on purpose: when a scene collection is unloaded every source is
	 * removed, and the list is cleared before these calls are delivered. */
	QMetaObject::invokeMethod(self, [self, uuid]() { self->sourceRemoved(uuid); }, Qt::QueuedConnection);
}

void OverlayManager::onSourceRename(void *param, calldata_t *data)
{
	auto *self = static_cast<OverlayManager *>(param);
	auto *source = static_cast<obs_source_t *>(calldata_ptr(data, "source"));
	const QString uuid = uuidOf(source);
	const QString name = nameOf(source);

	QMetaObject::invokeMethod(
		self,
		[self, uuid, name]() {
			const int index = self->indexOf(uuid);
			if (index >= 0)
				self->items_[index].name = name;
			emit self->changed();
		},
		Qt::QueuedConnection);
}

void OverlayManager::onGroupsChanged(void *param, calldata_t *)
{
	auto *self = static_cast<OverlayManager *>(param);
	/* The signal may be emitted from any thread. */
	QMetaObject::invokeMethod(self, [self]() { self->refreshGroups(); }, Qt::QueuedConnection);
}

void OverlayManager::onStageItemTransform(void *param, calldata_t *data)
{
	auto *self = static_cast<OverlayManager *>(param);
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(data, "item"));
	const QString uuid = uuidOf(item ? obs_sceneitem_get_source(item) : nullptr);
	if (uuid.isEmpty())
		return;
	QMetaObject::invokeMethod(self, [self, uuid]() { self->syncFromStage(uuid); }, Qt::QueuedConnection);
}

void OverlayManager::onStageItemAdd(void *param, calldata_t *data)
{
	auto *self = static_cast<OverlayManager *>(param);
	auto *item = static_cast<obs_sceneitem_t *>(calldata_ptr(data, "item"));
	const QString uuid = uuidOf(item ? obs_sceneitem_get_source(item) : nullptr);
	if (uuid.isEmpty())
		return;
	QMetaObject::invokeMethod(self, [self, uuid]() { self->stageItemAdded(uuid); }, Qt::QueuedConnection);
}

obs_sceneitem_t *OverlayManager::stageItem(const QString &sourceUuid) const
{
	if (stageUuid_.isEmpty() || sourceUuid.isEmpty())
		return nullptr;

	OBSSourceAutoRelease stage = obs_get_source_by_uuid(stageUuid_.toUtf8().constData());
	obs_scene_t *scene = obs_scene_from_source(stage);
	if (!scene)
		return nullptr;

	StageSearch search;
	search.uuid = sourceUuid;
	auto match = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		auto *state = static_cast<StageSearch *>(param);
		if (uuidOf(obs_sceneitem_get_source(item)) == state->uuid) {
			state->found = item;
			return false;
		}
		return true;
	};
	obs_scene_enum_items(scene, match, &search);
	return search.found;
}

void OverlayManager::syncFromStage(const QString &sourceUuid)
{
	const int index = indexOf(sourceUuid);
	if (index < 0 || items_[index].isScene || items_[index].isAudio || !items_[index].sceneItem)
		return;

	obs_sceneitem_t *staged = stageItem(sourceUuid);
	if (staged)
		copyTransform(staged, items_[index].sceneItem);
}

void OverlayManager::stageItemAdded(const QString &sourceUuid)
{
	/* A source dropped into the stage scene with the normal OBS tools
	 * becomes an overlay on its own. */
	if (indexOf(sourceUuid) >= 0 || !stageItem(sourceUuid))
		return;

	OBSSourceAutoRelease source = obs_get_source_by_uuid(sourceUuid.toUtf8().constData());
	if (!source || obs_source_is_scene(source) || isAudioOnly(source))
		return;
	addSource(sourceUuid);
}

void OverlayManager::connectStage()
{
	disconnectStage();
	if (stageUuid_.isEmpty())
		return;

	OBSSourceAutoRelease stage = obs_get_source_by_uuid(stageUuid_.toUtf8().constData());
	if (!stage || !obs_source_is_scene(stage)) {
		stageUuid_.clear();
		return;
	}

	signal_handler_t *handler = obs_source_get_signal_handler(stage);
	signal_handler_connect(handler, "item_transform", onStageItemTransform, this);
	signal_handler_connect(handler, "item_add", onStageItemAdd, this);
	stageConnected_ = OBSGetWeakRef(stage);
}

void OverlayManager::disconnectStage()
{
	OBSSourceAutoRelease stage = obs_weak_source_get_source(stageConnected_);
	if (stage) {
		signal_handler_t *handler = obs_source_get_signal_handler(stage);
		signal_handler_disconnect(handler, "item_transform", onStageItemTransform, this);
		signal_handler_disconnect(handler, "item_add", onStageItemAdd, this);
	}
	stageConnected_ = nullptr;
}

void OverlayManager::ensureStage()
{
	OBSSourceAutoRelease stage;
	if (!stageUuid_.isEmpty())
		stage = obs_get_source_by_uuid(stageUuid_.toUtf8().constData());

	if (!stage || !obs_source_is_scene(stage)) {
		const QString base = QString::fromUtf8(obs_module_text("StageName"));
		QString name = base;
		for (int suffix = 2; suffix < 100; suffix++) {
			OBSSourceAutoRelease taken = obs_get_source_by_name(name.toUtf8().constData());
			if (!taken)
				break;
			name = QStringLiteral("%1 %2").arg(base).arg(suffix);
		}

		obs_scene_t *created = obs_scene_create(name.toUtf8().constData());
		if (!created)
			return;
		stage = obs_scene_get_source(created);
		obs_source_get_ref(stage);
		obs_scene_release(created);

		stageUuid_ = uuidOf(stage);
		connectStage();
		obs_log(LOG_INFO, "created the stage scene '%s'", name.toUtf8().constData());
	}

	/* Every source overlay gets its item in the stage, placed where it is now. */
	obs_scene_t *scene = obs_scene_from_source(stage);
	for (int i = static_cast<int>(items_.size()) - 1; i >= 0; i--) {
		const OverlayItem &item = items_[i];
		if (item.isScene || item.isAudio || !item.sceneItem || stageItem(item.uuid))
			continue;
		OBSSourceAutoRelease source = obs_get_source_by_uuid(item.uuid.toUtf8().constData());
		if (!source)
			continue;
		obs_sceneitem_t *staged = obs_scene_add(scene, source);
		if (staged)
			copyTransform(item.sceneItem, staged);
	}
}

QString OverlayManager::editScene(const QString &uuid)
{
	const int index = indexOf(uuid);
	if (index < 0)
		return QString();

	/* A scene overlay is edited in the scene itself. */
	if (items_[index].isScene)
		return uuid;
	if (items_[index].isAudio)
		return QString();

	ensureStage();
	if (stageUuid_.isEmpty())
		return QString();

	OBSSourceAutoRelease stage = obs_get_source_by_uuid(stageUuid_.toUtf8().constData());
	obs_scene_t *scene = obs_scene_from_source(stage);
	if (!scene)
		return QString();

	/* Leave only this overlay selected, so it is easy to spot. */
	StageSearch search;
	search.uuid = uuid;
	auto select = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		auto *state = static_cast<StageSearch *>(param);
		obs_sceneitem_select(item, uuidOf(obs_sceneitem_get_source(item)) == state->uuid);
		return true;
	};
	obs_scene_enum_items(scene, select, &search);

	userChanged();
	return stageUuid_;
}

void OverlayManager::applyPreset(const QString &uuid, OverlayPreset preset)
{
	const int index = indexOf(uuid);
	if (index < 0 || items_[index].isScene || items_[index].isAudio || !items_[index].sceneItem)
		return;

	struct obs_video_info video = {};
	if (!obs_get_video_info(&video))
		return;
	const float width = static_cast<float>(video.base_width);
	const float height = static_cast<float>(video.base_height);
	const float margin = height * 0.03f;

	obs_sceneitem_t *target = items_[index].sceneItem;
	struct obs_transform_info info = {};
	obs_sceneitem_get_info2(target, &info);

	switch (preset) {
	case OverlayPreset::Fit: {
		struct obs_sceneitem_crop crop = {};
		obs_sceneitem_set_crop(target, &crop);
		info.rot = 0.0f;
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		info.bounds_type = OBS_BOUNDS_SCALE_INNER;
		info.bounds_alignment = OBS_ALIGN_CENTER;
		vec2_set(&info.pos, 0.0f, 0.0f);
		vec2_set(&info.bounds, width, height);
		break;
	}
	case OverlayPreset::Center:
		info.alignment = OBS_ALIGN_CENTER;
		vec2_set(&info.pos, width / 2.0f, height / 2.0f);
		break;
	case OverlayPreset::TopLeft:
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_TOP;
		vec2_set(&info.pos, margin, margin);
		break;
	case OverlayPreset::TopRight:
		info.alignment = OBS_ALIGN_RIGHT | OBS_ALIGN_TOP;
		vec2_set(&info.pos, width - margin, margin);
		break;
	case OverlayPreset::BottomLeft:
		info.alignment = OBS_ALIGN_LEFT | OBS_ALIGN_BOTTOM;
		vec2_set(&info.pos, margin, height - margin);
		break;
	case OverlayPreset::BottomRight:
		info.alignment = OBS_ALIGN_RIGHT | OBS_ALIGN_BOTTOM;
		vec2_set(&info.pos, width - margin, height - margin);
		break;
	}
	obs_sceneitem_set_info2(target, &info);

	obs_sceneitem_t *staged = stageItem(uuid);
	if (staged)
		copyTransform(target, staged);

	userChanged();
}

QList<SourceInfo> OverlayManager::currentSceneSources() const
{
	QList<SourceInfo> result;
	OBSSourceAutoRelease current = obs_frontend_get_current_scene();
	obs_scene_t *scene = obs_scene_from_source(current);
	if (!scene || uuidOf(current) == stageUuid_)
		return result;

	auto collect = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		auto *list = static_cast<QList<SourceInfo> *>(param);
		obs_source_t *source = obs_sceneitem_get_source(item);
		const uint32_t flags = obs_source_get_output_flags(source);
		if ((flags & OBS_SOURCE_VIDEO) != 0 && !obs_source_is_scene(source) && !obs_source_is_group(source))
			list->append(SourceInfo{uuidOf(source), nameOf(source)});
		return true;
	};
	obs_scene_enum_items(scene, collect, &result);

	QList<SourceInfo> unique;
	QSet<QString> seen;
	for (const SourceInfo &info : result) {
		if (indexOf(info.uuid) >= 0 || seen.contains(info.uuid))
			continue;
		seen.insert(info.uuid);
		unique.append(info);
	}
	return unique;
}

void OverlayManager::sourceRemoved(const QString &uuid)
{
	bool touched = false;

	if (uuid == stageUuid_) {
		/* The stage scene was deleted: overlays keep their last position. */
		stageUuid_.clear();
		stageConnected_ = nullptr;
		touched = true;
	}

	const int index = indexOf(uuid);
	if (index >= 0) {
		if (items_[index].sceneItem)
			obs_sceneitem_remove(items_[index].sceneItem);
		items_.removeAt(index);
		touched = true;
	}

	for (OverlayItem &item : items_) {
		if (item.scenes.remove(uuid))
			touched = true;
		if (item.groupHidden.remove(uuid))
			touched = true;
	}

	if (touched) {
		apply();
		emit changed();
	}
}

int OverlayManager::indexOf(const QString &uuid) const
{
	for (int i = 0; i < items_.size(); i++) {
		if (items_[i].uuid == uuid)
			return i;
	}
	return -1;
}

const OverlayItem *OverlayManager::find(const QString &uuid) const
{
	const int index = indexOf(uuid);
	return index >= 0 ? &items_[index] : nullptr;
}

void OverlayManager::ensureScene()
{
	if (!scene_)
		scene_ = obs_scene_create_private("AC Global Overlay");
}

void OverlayManager::userChanged()
{
	apply();
	emit changed();
	obs_frontend_save();
}

void OverlayManager::setMasterEnabled(bool enabled)
{
	if (master_ == enabled)
		return;
	master_ = enabled;
	userChanged();
}

bool OverlayManager::addSource(const QString &uuid)
{
	if (uuid.isEmpty() || indexOf(uuid) >= 0)
		return false;

	OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.toUtf8().constData());
	if (!source)
		return false;

	ensureScene();
	if (!scene_)
		return false;

	obs_sceneitem_t *sceneItem = obs_scene_add(scene_, source);
	if (!sceneItem)
		return false;

	OverlayItem item;
	item.uuid = uuid;
	item.name = nameOf(source);
	item.isScene = obs_source_is_scene(source);
	item.isAudio = isAudioOnly(source);
	item.sceneItem = sceneItem;

	if (!item.isScene && !item.isAudio) {
		copyExistingTransform(source, sceneItem);

		/* With a stage scene the overlay lives there too, in the same place. */
		OBSSourceAutoRelease stage;
		if (!stageUuid_.isEmpty())
			stage = obs_get_source_by_uuid(stageUuid_.toUtf8().constData());
		obs_scene_t *stageScene = obs_scene_from_source(stage);
		if (stageScene) {
			obs_sceneitem_t *staged = stageItem(uuid);
			if (staged) {
				copyTransform(staged, sceneItem);
			} else {
				staged = obs_scene_add(stageScene, source);
				if (staged)
					copyTransform(sceneItem, staged);
			}
		}
	}

	items_.prepend(item);
	applyOrder();
	userChanged();
	return true;
}

void OverlayManager::removeItem(const QString &uuid)
{
	const int index = indexOf(uuid);
	if (index < 0)
		return;

	if (items_[index].sceneItem)
		obs_sceneitem_remove(items_[index].sceneItem);
	if (!items_[index].isScene && !items_[index].isAudio) {
		obs_sceneitem_t *staged = stageItem(uuid);
		if (staged)
			obs_sceneitem_remove(staged);
	}
	items_.removeAt(index);
	userChanged();
}

void OverlayManager::setItemEnabled(const QString &uuid, bool enabled)
{
	const int index = indexOf(uuid);
	if (index < 0 || items_[index].enabled == enabled)
		return;

	items_[index].enabled = enabled;
	userChanged();
}

void OverlayManager::setOrder(const QStringList &uuids)
{
	QList<OverlayItem> ordered;
	for (const QString &uuid : uuids) {
		const int index = indexOf(uuid);
		if (index >= 0) {
			ordered.append(items_[index]);
			items_.removeAt(index);
		}
	}
	ordered.append(items_);
	items_ = ordered;

	applyOrder();
	userChanged();
}

void OverlayManager::applyOrder()
{
	/* The first row of the list is the topmost layer. */
	const int count = static_cast<int>(items_.size());
	for (int i = count - 1; i >= 0; i--) {
		if (items_[i].sceneItem)
			obs_sceneitem_set_order_position(items_[i].sceneItem, count - 1 - i);
	}
}

bool OverlayManager::isVisibleIn(const OverlayItem &item, const QString &sceneUuid) const
{
	if (sceneUuid.isEmpty())
		return true;
	if (item.groupMode)
		return !item.groupHidden.contains(sceneUuid) && groupScenes(item).contains(sceneUuid);
	const bool listed = item.scenes.contains(sceneUuid);
	return item.showInNew ? !listed : listed;
}

void OverlayManager::setVisibleIn(const QString &uuid, const QString &sceneUuid, bool visible)
{
	const int index = indexOf(uuid);
	if (index < 0 || sceneUuid.isEmpty())
		return;

	OverlayItem &item = items_[index];
	if (item.groupMode) {
		if (visible)
			item.groupHidden.remove(sceneUuid);
		else
			item.groupHidden.insert(sceneUuid);
	} else if (visible == item.showInNew) {
		item.scenes.remove(sceneUuid);
	} else {
		item.scenes.insert(sceneUuid);
	}
	userChanged();
}

void OverlayManager::setVisibleInAll(const QString &uuid, bool visible)
{
	const int index = indexOf(uuid);
	if (index < 0)
		return;

	OverlayItem &item = items_[index];
	item.scenes.clear();
	if (visible != item.showInNew) {
		for (const SourceInfo &scene : allScenes())
			item.scenes.insert(scene.uuid);
	}
	userChanged();
}

void OverlayManager::setShowInNew(const QString &uuid, bool showInNew)
{
	const int index = indexOf(uuid);
	if (index < 0 || items_[index].showInNew == showInNew)
		return;

	/* Keep the visibility of every existing scene and only change what
	 * happens with scenes created from now on. */
	OverlayItem &item = items_[index];
	QSet<QString> scenes;
	for (const SourceInfo &scene : allScenes()) {
		const bool visible = isVisibleIn(item, scene.uuid);
		if (visible != showInNew)
			scenes.insert(scene.uuid);
	}
	item.showInNew = showInNew;
	item.scenes = scenes;
	userChanged();
}

int OverlayManager::hiddenSceneCount(const OverlayItem &item) const
{
	int hidden = 0;
	if (item.groupMode) {
		/* Only the exceptions inside the listed groups count as hidden. */
		const QSet<QString> scenes = groupScenes(item);
		for (const SourceInfo &scene : allScenes()) {
			if (scenes.contains(scene.uuid) && item.groupHidden.contains(scene.uuid))
				hidden++;
		}
		return hidden;
	}

	for (const SourceInfo &scene : allScenes()) {
		if (!isVisibleIn(item, scene.uuid))
			hidden++;
	}
	return hidden;
}

const GroupInfo *OverlayManager::liveGroup(const QString &id) const
{
	if (!groupsAvailable_)
		return nullptr;
	for (const GroupInfo &group : groups_) {
		if (group.id == id)
			return &group;
	}
	return nullptr;
}

QList<GroupInfo> OverlayManager::selectedGroups(const OverlayItem &item) const
{
	QList<GroupInfo> result;
	for (const QString &id : item.groupIds) {
		if (const GroupInfo *live = liveGroup(id)) {
			result.append(*live);
		} else {
			GroupInfo snapshot = item.groupSnapshots.value(id);
			snapshot.id = id;
			snapshot.available = false;
			result.append(snapshot);
		}
	}
	return result;
}

QList<GroupInfo> OverlayManager::listedGroups(const OverlayItem &item) const
{
	QList<GroupInfo> result;
	if (groupsAvailable_)
		result = groups_;
	for (const GroupInfo &group : selectedGroups(item)) {
		if (!group.available)
			result.append(group);
	}
	return result;
}

QSet<QString> OverlayManager::groupScenes(const OverlayItem &item) const
{
	/* A scene that belongs to several groups shows the overlay when any of
	 * them is listed. A missing group falls back to its last known scenes. */
	QSet<QString> scenes;
	for (const GroupInfo &group : selectedGroups(item)) {
		for (const QString &scene : group.scenes)
			scenes.insert(scene);
	}
	return scenes;
}

bool OverlayManager::updateSnapshots()
{
	bool touched = false;
	for (OverlayItem &item : items_) {
		for (const QString &id : item.groupIds) {
			const GroupInfo *live = liveGroup(id);
			if (!live)
				continue;
			const GroupInfo previous = item.groupSnapshots.value(id);
			if (previous.name != live->name || previous.color != live->color ||
			    previous.scenes != live->scenes) {
				item.groupSnapshots.insert(id, *live);
				touched = true;
			}
		}
	}
	return touched;
}

void OverlayManager::refreshGroups()
{
	proc_handler_t *procs = obs_get_proc_handler();
	calldata_t data = {};
	const bool installed = proc_handler_call(procs, GROUPS_PROC, &data);
	QByteArray json;
	if (installed) {
		const char *text = calldata_string(&data, "json");
		if (text)
			json = QByteArray(text);
	}
	calldata_free(&data);

	if (installed && !groupSignalConnected_) {
		signal_handler_connect(obs_get_signal_handler(), GROUPS_SIGNAL, onGroupsChanged, this);
		groupSignalConnected_ = true;
	}

	QList<GroupInfo> groups;
	bool available = false;
	const QJsonDocument document = installed ? QJsonDocument::fromJson(json) : QJsonDocument();
	if (document.isObject() && document.object().value("groups").isArray()) {
		available = true;
		const QJsonArray array = document.object().value("groups").toArray();
		for (qsizetype i = 0; i < array.size(); i++) {
			const QJsonObject object = array.at(i).toObject();
			GroupInfo group;
			group.id = object.value("id").toString();
			group.name = object.value("name").toString();
			group.color = object.value("color").toString();
			const QJsonArray scenes = object.value("scenes").toArray();
			for (qsizetype j = 0; j < scenes.size(); j++) {
				const QString uuid = scenes.at(j).toString();
				if (!uuid.isEmpty())
					group.scenes.append(uuid);
			}
			if (!group.id.isEmpty())
				groups.append(group);
		}
	}

	const bool sameState = available == groupsAvailable_ && sameGroups(groups, groups_);
	groupsAvailable_ = available;
	groups_ = groups;
	const bool snapshotsChanged = updateSnapshots();

	if (!sameState || snapshotsChanged) {
		apply();
		emit changed();
	}
}

void OverlayManager::setGroupMode(const QString &uuid, bool groupMode)
{
	const int index = indexOf(uuid);
	if (index < 0 || items_[index].groupMode == groupMode)
		return;

	/* Each mode keeps its own exceptions, so going back restores them. */
	items_[index].groupMode = groupMode;
	userChanged();
}

void OverlayManager::setGroupSelected(const QString &uuid, const QString &groupId, bool selected)
{
	const int index = indexOf(uuid);
	if (index < 0 || groupId.isEmpty())
		return;

	OverlayItem &item = items_[index];
	if (selected) {
		const GroupInfo *live = liveGroup(groupId);
		if (!live || item.groupIds.contains(groupId))
			return;
		item.groupIds.append(groupId);
		item.groupSnapshots.insert(groupId, *live);
	} else {
		item.groupIds.removeAll(groupId);
		item.groupSnapshots.remove(groupId);
	}
	userChanged();
}

SourceInfo OverlayManager::currentScene() const
{
	OBSSourceAutoRelease source = obs_frontend_get_current_scene();
	return SourceInfo{uuidOf(source), nameOf(source)};
}

bool OverlayManager::currentSceneHidden() const
{
	const QString scene = currentScene().uuid;
	if (scene.isEmpty() || items_.isEmpty())
		return false;

	for (const OverlayItem &item : items_) {
		if (isVisibleIn(item, scene))
			return false;
	}
	return true;
}

void OverlayManager::setCurrentSceneHidden(bool hidden)
{
	const QString scene = currentScene().uuid;
	if (scene.isEmpty())
		return;

	for (OverlayItem &item : items_) {
		if (item.groupMode) {
			/* Outside its groups the overlay stays hidden either way. */
			if (hidden && groupScenes(item).contains(scene))
				item.groupHidden.insert(scene);
			else
				item.groupHidden.remove(scene);
		} else if (hidden == item.showInNew) {
			item.scenes.insert(scene);
		} else {
			item.scenes.remove(scene);
		}
	}
	userChanged();
}

bool OverlayManager::canShowInCurrentScene() const
{
	const QString scene = currentScene().uuid;
	if (scene.isEmpty())
		return false;

	for (const OverlayItem &item : items_) {
		if (!item.groupMode || groupScenes(item).contains(scene))
			return true;
	}
	return false;
}

int OverlayManager::visibleNowCount() const
{
	if (!master_)
		return 0;

	const QString scene = currentScene().uuid;
	int visible = 0;
	for (const OverlayItem &item : items_) {
		if (item.enabled && item.uuid != scene && isVisibleIn(item, scene))
			visible++;
	}
	return visible;
}

QList<SourceInfo> OverlayManager::allScenes() const
{
	QList<SourceInfo> result;
	struct obs_frontend_source_list scenes = {};
	obs_frontend_get_scenes(&scenes);
	for (size_t i = 0; i < scenes.sources.num; i++) {
		obs_source_t *source = scenes.sources.array[i];
		/* The stage is a workbench, not a scene overlays are shown on. */
		if (uuidOf(source) == stageUuid_)
			continue;
		result.append(SourceInfo{uuidOf(source), nameOf(source)});
	}
	obs_frontend_source_list_free(&scenes);
	return result;
}

QList<SourceInfo> OverlayManager::addableScenes() const
{
	QList<SourceInfo> result;
	for (const SourceInfo &scene : allScenes()) {
		if (indexOf(scene.uuid) < 0)
			result.append(scene);
	}
	return result;
}

QList<SourceInfo> OverlayManager::addableSources() const
{
	QList<SourceInfo> result;

	auto collect = [](void *param, obs_source_t *source) {
		auto *list = static_cast<QList<SourceInfo> *>(param);
		const uint32_t flags = obs_source_get_output_flags(source);
		if ((flags & OBS_SOURCE_VIDEO) != 0 && !obs_source_removed(source))
			list->append(SourceInfo{uuidOf(source), nameOf(source)});
		return true;
	};
	obs_enum_sources(collect, &result);

	result.erase(std::remove_if(result.begin(), result.end(),
				    [this](const SourceInfo &info) { return indexOf(info.uuid) >= 0; }),
		     result.end());
	std::sort(result.begin(), result.end(), sortByName);
	return result;
}

QList<SourceInfo> OverlayManager::addableAudio() const
{
	QList<SourceInfo> result;

	auto collect = [](void *param, obs_source_t *source) {
		auto *list = static_cast<QList<SourceInfo> *>(param);
		if (isAudioOnly(source) && !obs_source_removed(source))
			list->append(SourceInfo{uuidOf(source), nameOf(source)});
		return true;
	};
	obs_enum_sources(collect, &result);

	result.erase(std::remove_if(result.begin(), result.end(),
				    [this](const SourceInfo &info) { return indexOf(info.uuid) >= 0; }),
		     result.end());
	std::sort(result.begin(), result.end(), sortByName);
	return result;
}

void OverlayManager::load(obs_data_t *data)
{
	clear();
	if (!data)
		return;

	obs_data_set_default_bool(data, "master", true);
	master_ = obs_data_get_bool(data, "master");

	stageUuid_ = QString::fromUtf8(obs_data_get_string(data, "stage_scene"));
	connectStage();

	OBSDataArrayAutoRelease array = obs_data_get_array(data, "items");
	const size_t count = array ? obs_data_array_count(array) : 0;

	for (size_t i = 0; i < count; i++) {
		OBSDataAutoRelease entry = obs_data_array_item(array, i);
		obs_data_set_default_bool(entry, "enabled", true);
		obs_data_set_default_bool(entry, "show_in_new", true);

		OBSSourceAutoRelease source = obs_get_source_by_uuid(obs_data_get_string(entry, "uuid"));
		if (!source)
			source = obs_get_source_by_name(obs_data_get_string(entry, "name"));
		if (!source || indexOf(uuidOf(source)) >= 0)
			continue;

		ensureScene();
		if (!scene_)
			break;

		obs_sceneitem_t *sceneItem = obs_scene_add(scene_, source);
		if (!sceneItem)
			continue;

		OverlayItem item;
		item.uuid = uuidOf(source);
		item.name = nameOf(source);
		item.isScene = obs_source_is_scene(source);
		item.isAudio = isAudioOnly(source);
		item.enabled = obs_data_get_bool(entry, "enabled");
		item.showInNew = obs_data_get_bool(entry, "show_in_new");
		item.sceneItem = sceneItem;

		OBSDataArrayAutoRelease scenes = obs_data_get_array(entry, "scenes");
		const size_t sceneCount = scenes ? obs_data_array_count(scenes) : 0;
		for (size_t j = 0; j < sceneCount; j++) {
			OBSDataAutoRelease scene = obs_data_array_item(scenes, j);
			const QString uuid = QString::fromUtf8(obs_data_get_string(scene, "uuid"));
			if (!uuid.isEmpty())
				item.scenes.insert(uuid);
		}

		item.groupMode = obs_data_get_bool(entry, "group_mode");

		OBSDataArrayAutoRelease groups = obs_data_get_array(entry, "groups");
		const size_t groupCount = groups ? obs_data_array_count(groups) : 0;
		for (size_t j = 0; j < groupCount; j++) {
			OBSDataAutoRelease saved = obs_data_array_item(groups, j);
			GroupInfo group;
			group.id = QString::fromUtf8(obs_data_get_string(saved, "id"));
			group.name = QString::fromUtf8(obs_data_get_string(saved, "name"));
			group.color = QString::fromUtf8(obs_data_get_string(saved, "color"));
			if (group.id.isEmpty() || item.groupIds.contains(group.id))
				continue;

			OBSDataArrayAutoRelease savedScenes = obs_data_get_array(saved, "scenes");
			const size_t groupSceneCount = savedScenes ? obs_data_array_count(savedScenes) : 0;
			for (size_t k = 0; k < groupSceneCount; k++) {
				OBSDataAutoRelease scene = obs_data_array_item(savedScenes, k);
				const QString uuid = QString::fromUtf8(obs_data_get_string(scene, "uuid"));
				if (!uuid.isEmpty())
					group.scenes.append(uuid);
			}
			item.groupIds.append(group.id);
			item.groupSnapshots.insert(group.id, group);
		}

		OBSDataArrayAutoRelease groupHidden = obs_data_get_array(entry, "group_hidden");
		const size_t groupHiddenCount = groupHidden ? obs_data_array_count(groupHidden) : 0;
		for (size_t j = 0; j < groupHiddenCount; j++) {
			OBSDataAutoRelease scene = obs_data_array_item(groupHidden, j);
			const QString uuid = QString::fromUtf8(obs_data_get_string(scene, "uuid"));
			if (!uuid.isEmpty())
				item.groupHidden.insert(uuid);
		}

		OBSDataAutoRelease transform = obs_data_get_obj(entry, "transform");
		if (transform && !item.isScene) {
			struct obs_transform_info info = {};
			struct obs_sceneitem_crop crop = {};
			obs_sceneitem_get_info2(sceneItem, &info);
			obs_data_get_vec2(transform, "pos", &info.pos);
			obs_data_get_vec2(transform, "scale", &info.scale);
			obs_data_get_vec2(transform, "bounds", &info.bounds);
			info.rot = static_cast<float>(obs_data_get_double(transform, "rot"));
			info.alignment = static_cast<uint32_t>(obs_data_get_int(transform, "alignment"));
			info.bounds_type = static_cast<obs_bounds_type>(obs_data_get_int(transform, "bounds_type"));
			info.bounds_alignment = static_cast<uint32_t>(obs_data_get_int(transform, "bounds_alignment"));
			info.crop_to_bounds = obs_data_get_bool(transform, "crop_to_bounds");
			crop.left = static_cast<int>(obs_data_get_int(transform, "crop_left"));
			crop.top = static_cast<int>(obs_data_get_int(transform, "crop_top"));
			crop.right = static_cast<int>(obs_data_get_int(transform, "crop_right"));
			crop.bottom = static_cast<int>(obs_data_get_int(transform, "crop_bottom"));
			obs_sceneitem_set_info2(sceneItem, &info);
			obs_sceneitem_set_crop(sceneItem, &crop);
		}

		items_.append(item);
	}

	/* The stage scene, when there is one, is where positions are kept. */
	for (const OverlayItem &item : items_)
		syncFromStage(item.uuid);

	applyOrder();
	apply();
	emit changed();
	obs_log(LOG_INFO, "loaded %d overlay(s)", static_cast<int>(items_.size()));
}

void OverlayManager::save(obs_data_t *data) const
{
	obs_data_set_bool(data, "master", master_);
	obs_data_set_string(data, "stage_scene", stageUuid_.toUtf8().constData());

	OBSDataArrayAutoRelease array = obs_data_array_create();
	for (const OverlayItem &item : items_) {
		OBSDataAutoRelease entry = obs_data_create();
		obs_data_set_string(entry, "uuid", item.uuid.toUtf8().constData());
		obs_data_set_string(entry, "name", item.name.toUtf8().constData());
		obs_data_set_bool(entry, "enabled", item.enabled);
		obs_data_set_bool(entry, "show_in_new", item.showInNew);

		OBSDataArrayAutoRelease scenes = obs_data_array_create();
		for (const QString &uuid : item.scenes) {
			OBSDataAutoRelease scene = obs_data_create();
			obs_data_set_string(scene, "uuid", uuid.toUtf8().constData());
			obs_data_array_push_back(scenes, scene);
		}
		obs_data_set_array(entry, "scenes", scenes);

		obs_data_set_bool(entry, "group_mode", item.groupMode);

		OBSDataArrayAutoRelease groups = obs_data_array_create();
		for (const QString &id : item.groupIds) {
			const GroupInfo group = item.groupSnapshots.value(id);
			OBSDataAutoRelease saved = obs_data_create();
			obs_data_set_string(saved, "id", id.toUtf8().constData());
			obs_data_set_string(saved, "name", group.name.toUtf8().constData());
			obs_data_set_string(saved, "color", group.color.toUtf8().constData());

			OBSDataArrayAutoRelease savedScenes = obs_data_array_create();
			for (const QString &uuid : group.scenes) {
				OBSDataAutoRelease scene = obs_data_create();
				obs_data_set_string(scene, "uuid", uuid.toUtf8().constData());
				obs_data_array_push_back(savedScenes, scene);
			}
			obs_data_set_array(saved, "scenes", savedScenes);
			obs_data_array_push_back(groups, saved);
		}
		obs_data_set_array(entry, "groups", groups);

		OBSDataArrayAutoRelease groupHidden = obs_data_array_create();
		for (const QString &uuid : item.groupHidden) {
			OBSDataAutoRelease scene = obs_data_create();
			obs_data_set_string(scene, "uuid", uuid.toUtf8().constData());
			obs_data_array_push_back(groupHidden, scene);
		}
		obs_data_set_array(entry, "group_hidden", groupHidden);

		if (item.sceneItem && !item.isScene) {
			struct obs_transform_info info = {};
			struct obs_sceneitem_crop crop = {};
			obs_sceneitem_get_info2(item.sceneItem, &info);
			obs_sceneitem_get_crop(item.sceneItem, &crop);

			OBSDataAutoRelease transform = obs_data_create();
			obs_data_set_vec2(transform, "pos", &info.pos);
			obs_data_set_vec2(transform, "scale", &info.scale);
			obs_data_set_vec2(transform, "bounds", &info.bounds);
			obs_data_set_double(transform, "rot", info.rot);
			obs_data_set_int(transform, "alignment", info.alignment);
			obs_data_set_int(transform, "bounds_type", info.bounds_type);
			obs_data_set_int(transform, "bounds_alignment", info.bounds_alignment);
			obs_data_set_bool(transform, "crop_to_bounds", info.crop_to_bounds);
			obs_data_set_int(transform, "crop_left", crop.left);
			obs_data_set_int(transform, "crop_top", crop.top);
			obs_data_set_int(transform, "crop_right", crop.right);
			obs_data_set_int(transform, "crop_bottom", crop.bottom);
			obs_data_set_obj(entry, "transform", transform);
		}

		obs_data_array_push_back(array, entry);
	}
	obs_data_set_array(data, "items", array);
}

void OverlayManager::clear()
{
	if (scene_) {
		OBSSourceAutoRelease active = obs_get_output_source(OVERLAY_CHANNEL);
		if (active && active.Get() == obs_scene_get_source(scene_))
			obs_set_output_source(OVERLAY_CHANNEL, nullptr);
	}

	items_.clear();
	disconnectStage();
	stageUuid_.clear();

	if (scene_) {
		obs_scene_release(scene_);
		scene_ = nullptr;
	}
	emit changed();
}

void OverlayManager::apply()
{
	if (!scene_)
		return;

	const QString scene = currentScene().uuid;
	for (OverlayItem &item : items_) {
		if (!item.sceneItem)
			continue;
		/* A scene used as an overlay is not drawn on top of itself. */
		/* On the stage scene the sources are already there to be edited. */
		const bool onStage = !item.isScene && !stageUuid_.isEmpty() && scene == stageUuid_;
		const bool visible = master_ && item.enabled && item.uuid != scene && !onStage &&
				     isVisibleIn(item, scene);
		obs_sceneitem_set_visible(item.sceneItem, visible);
	}

	obs_source_t *wanted = (master_ && !items_.isEmpty()) ? obs_scene_get_source(scene_) : nullptr;
	OBSSourceAutoRelease active = obs_get_output_source(OVERLAY_CHANNEL);
	if (active.Get() != wanted) {
		/* Never take the channel from something that is not ours. */
		if (!active || active.Get() == obs_scene_get_source(scene_))
			obs_set_output_source(OVERLAY_CHANNEL, wanted);
	}
}
