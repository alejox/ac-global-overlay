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
#include <plugin-support.h>

#include <algorithm>

namespace {

/* Output channels 0-6 are used by OBS itself and 7+ by other keyer plugins,
 * so the overlay sits on a high channel to stay above all of them. */
const uint32_t OVERLAY_CHANNEL = 56;

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

void OverlayManager::sourceRemoved(const QString &uuid)
{
	bool touched = false;

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
	item.sceneItem = sceneItem;

	if (!item.isScene)
		copyExistingTransform(source, sceneItem);

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
	const bool listed = item.scenes.contains(sceneUuid);
	return item.showInNew ? !listed : listed;
}

void OverlayManager::setVisibleIn(const QString &uuid, const QString &sceneUuid, bool visible)
{
	const int index = indexOf(uuid);
	if (index < 0 || sceneUuid.isEmpty())
		return;

	OverlayItem &item = items_[index];
	if (visible == item.showInNew)
		item.scenes.remove(sceneUuid);
	else
		item.scenes.insert(sceneUuid);
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
	for (const SourceInfo &scene : allScenes()) {
		if (!isVisibleIn(item, scene.uuid))
			hidden++;
	}
	return hidden;
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
		if (hidden == item.showInNew)
			item.scenes.insert(scene);
		else
			item.scenes.remove(scene);
	}
	userChanged();
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

void OverlayManager::load(obs_data_t *data)
{
	clear();
	if (!data)
		return;

	obs_data_set_default_bool(data, "master", true);
	master_ = obs_data_get_bool(data, "master");

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

	applyOrder();
	apply();
	emit changed();
	obs_log(LOG_INFO, "loaded %d overlay(s)", static_cast<int>(items_.size()));
}

void OverlayManager::save(obs_data_t *data) const
{
	obs_data_set_bool(data, "master", master_);

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
		const bool visible = master_ && item.enabled && item.uuid != scene && isVisibleIn(item, scene);
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
