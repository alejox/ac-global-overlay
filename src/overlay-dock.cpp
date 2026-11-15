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

#include "overlay-dock.hpp"
#include "overlay-manager.hpp"

#include <obs-module.h>

#include <QCheckBox>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {

const char *SAVE_KEY = "ac-global-overlay";

QString tr_(const char *key)
{
	return QString::fromUtf8(obs_module_text(key));
}

void setClass(QWidget *widget, const char *name)
{
	widget->setProperty("class", name);
}

QLabel *mutedLabel(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	setClass(label, "text-muted");
	return label;
}

QLabel *boldLabel(const QString &text, QWidget *parent)
{
	auto *label = new QLabel(text, parent);
	QFont font = label->font();
	font.setBold(true);
	label->setFont(font);
	return label;
}

} // namespace

/* ------------------------------------------------------------------ */

ToggleSwitch::ToggleSwitch(QWidget *parent) : QAbstractButton(parent)
{
	setCheckable(true);
	setCursor(Qt::PointingHandCursor);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize ToggleSwitch::sizeHint() const
{
	return QSize(40, 24);
}

void ToggleSwitch::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const QRectF track(1.0, 1.0, width() - 2.0, height() - 2.0);
	const qreal radius = track.height() / 2.0;
	const QColor on = palette().color(QPalette::Highlight);
	QColor off = palette().color(QPalette::WindowText);
	off.setAlpha(90);

	painter.setPen(Qt::NoPen);
	painter.setBrush(isChecked() ? on : off);
	painter.setOpacity(isEnabled() ? 1.0 : 0.5);
	painter.drawRoundedRect(track, radius, radius);

	const qreal knob = track.height() - 6.0;
	const qreal x = isChecked() ? track.right() - knob - 3.0 : track.left() + 3.0;
	painter.setBrush(Qt::white);
	painter.drawEllipse(QRectF(x, track.top() + 3.0, knob, knob));

	if (hasFocus()) {
		painter.setBrush(Qt::NoBrush);
		painter.setPen(QPen(palette().color(QPalette::WindowText), 1.0));
		painter.drawRoundedRect(track, radius, radius);
	}
}

/* ------------------------------------------------------------------ */

ReorderList::ReorderList(QWidget *parent) : QListWidget(parent)
{
	setDragDropMode(QAbstractItemView::InternalMove);
	setDefaultDropAction(Qt::MoveAction);
	setSelectionMode(QAbstractItemView::SingleSelection);
	setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
	setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	setFrameShape(QFrame::NoFrame);
	setSpacing(3);
	setStyleSheet("QListWidget { background: transparent; }"
		      "QListWidget::item { background: transparent; border: none; padding: 0px; }"
		      "QListWidget::item:selected { background: transparent; }"
		      "QListWidget::item:hover { background: transparent; }");
}

void ReorderList::dropEvent(QDropEvent *event)
{
	QListWidget::dropEvent(event);
	emit reordered();
}

/* ------------------------------------------------------------------ */

OverlayDock::OverlayDock(QWidget *parent) : QWidget(parent)
{
	manager_ = new OverlayManager(this);

	stack_ = new QStackedWidget(this);
	listPage_ = buildListPage();
	scenesPage_ = buildScenesPage();
	emptyPage_ = buildEmptyPage();
	stack_->addWidget(listPage_);
	stack_->addWidget(scenesPage_);
	stack_->addWidget(emptyPage_);

	auto *layout = new QVBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(stack_);
	setMinimumWidth(260);

	connect(manager_, &OverlayManager::changed, this, &OverlayDock::scheduleRefresh);

	obs_frontend_add_event_callback(onFrontendEvent, this);
	obs_frontend_add_save_callback(onSaveLoad, this);

	refresh();
}

OverlayDock::~OverlayDock()
{
	obs_frontend_remove_event_callback(onFrontendEvent, this);
	obs_frontend_remove_save_callback(onSaveLoad, this);
}

void OverlayDock::onFrontendEvent(enum obs_frontend_event event, void *param)
{
	auto *dock = static_cast<OverlayDock *>(param);

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		dock->manager_->apply();
		dock->scheduleRefresh();
		break;
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
		dock->scheduleRefresh();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
	case OBS_FRONTEND_EVENT_EXIT:
		dock->detailUuid_.clear();
		dock->manager_->clear();
		break;
	default:
		break;
	}
}

void OverlayDock::onSaveLoad(obs_data_t *saveData, bool saving, void *param)
{
	auto *dock = static_cast<OverlayDock *>(param);

	if (saving) {
		OBSDataAutoRelease data = obs_data_create();
		dock->manager_->save(data);
		obs_data_set_obj(saveData, SAVE_KEY, data);
	} else {
		OBSDataAutoRelease data = obs_data_get_obj(saveData, SAVE_KEY);
		dock->detailUuid_.clear();
		dock->manager_->load(data);
	}
}

/* ------------------------------------------------------------------ */

QWidget *OverlayDock::buildListPage()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	/* Master switch */
	auto *header = new QHBoxLayout();
	auto *headerText = new QVBoxLayout();
	headerText->setSpacing(1);
	headerText->addWidget(boldLabel(tr_("OverlaysActive"), page));
	masterStatus_ = mutedLabel(QString(), page);
	headerText->addWidget(masterStatus_);
	header->addLayout(headerText, 1);
	masterSwitch_ = new ToggleSwitch(page);
	masterSwitch_->setAccessibleName(tr_("MasterSwitch"));
	masterSwitch_->setToolTip(tr_("MasterSwitch"));
	header->addWidget(masterSwitch_);
	layout->addLayout(header);

	connect(masterSwitch_, &QAbstractButton::clicked, this,
		[this](bool checked) { manager_->setMasterEnabled(checked); });

	/* Current scene card */
	auto *card = new QFrame(page);
	card->setObjectName("acgoCard");
	card->setStyleSheet("QFrame#acgoCard { border: 1px solid rgba(128, 128, 128, 0.45); border-radius: 6px; }");
	auto *cardLayout = new QHBoxLayout(card);
	cardLayout->setContentsMargins(10, 8, 8, 8);
	auto *cardText = new QVBoxLayout();
	cardText->setSpacing(1);
	auto *cardCaption = mutedLabel(tr_("CurrentScene"), card);
	QFont captionFont = cardCaption->font();
	captionFont.setPointSizeF(captionFont.pointSizeF() * 0.85);
	captionFont.setBold(true);
	cardCaption->setFont(captionFont);
	cardText->addWidget(cardCaption);
	sceneName_ = boldLabel(QString(), card);
	sceneName_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	cardText->addWidget(sceneName_);
	sceneStatus_ = mutedLabel(QString(), card);
	sceneStatus_->setWordWrap(true);
	cardText->addWidget(sceneStatus_);
	cardLayout->addLayout(cardText, 1);
	sceneButton_ = new QPushButton(card);
	cardLayout->addWidget(sceneButton_, 0, Qt::AlignVCenter);
	layout->addWidget(card);

	connect(sceneButton_, &QPushButton::clicked, this,
		[this]() { manager_->setCurrentSceneHidden(!manager_->currentSceneHidden()); });

	/* Overlay list */
	auto *section = mutedLabel(tr_("AlwaysOnTop"), page);
	section->setFont(captionFont);
	layout->addWidget(section);

	list_ = new ReorderList(page);
	layout->addWidget(list_, 1);

	connect(list_, &ReorderList::reordered, this, [this]() {
		QStringList order;
		for (int i = 0; i < list_->count(); i++)
			order.append(list_->item(i)->data(Qt::UserRole).toString());
		manager_->setOrder(order);
	});

	auto *addButton = new QPushButton(tr_("Add"), page);
	setClass(addButton, "icon-plus");
	layout->addWidget(addButton);
	connect(addButton, &QPushButton::clicked, this, [this, addButton]() { showAddMenu(addButton); });

	return page;
}

QWidget *OverlayDock::buildScenesPage()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(8);

	auto *header = new QHBoxLayout();
	auto *back = new QPushButton(QStringLiteral("‹"), page);
	back->setAccessibleName(tr_("Back"));
	back->setToolTip(tr_("Back"));
	back->setFixedWidth(32);
	header->addWidget(back);
	auto *headerText = new QVBoxLayout();
	headerText->setSpacing(1);
	detailTitle_ = boldLabel(QString(), page);
	detailTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	headerText->addWidget(detailTitle_);
	headerText->addWidget(mutedLabel(tr_("WhereShown"), page));
	header->addLayout(headerText, 1);
	layout->addLayout(header);

	connect(back, &QPushButton::clicked, this, [this]() {
		detailUuid_.clear();
		scheduleRefresh();
	});

	detailSearch_ = new QLineEdit(page);
	detailSearch_->setPlaceholderText(tr_("SearchScene"));
	detailSearch_->setClearButtonEnabled(true);
	layout->addWidget(detailSearch_);
	connect(detailSearch_, &QLineEdit::textChanged, this, [this]() { applySceneFilter(); });

	auto *tools = new QHBoxLayout();
	detailCount_ = mutedLabel(QString(), page);
	tools->addWidget(detailCount_, 1);
	auto *all = new QPushButton(tr_("All"), page);
	auto *none = new QPushButton(tr_("None"), page);
	tools->addWidget(all);
	tools->addWidget(none);
	layout->addLayout(tools);

	connect(all, &QPushButton::clicked, this, [this]() { manager_->setVisibleInAll(detailUuid_, true); });
	connect(none, &QPushButton::clicked, this, [this]() { manager_->setVisibleInAll(detailUuid_, false); });

	auto *scroll = new QScrollArea(page);
	scroll->setWidgetResizable(true);
	scroll->setFrameShape(QFrame::NoFrame);
	scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	detailScenes_ = new QWidget(scroll);
	detailScenesLayout_ = new QVBoxLayout(detailScenes_);
	detailScenesLayout_->setContentsMargins(2, 2, 2, 2);
	detailScenesLayout_->setSpacing(6);
	scroll->setWidget(detailScenes_);
	layout->addWidget(scroll, 1);

	auto *footer = new QHBoxLayout();
	auto *footerText = new QVBoxLayout();
	footerText->setSpacing(1);
	footerText->addWidget(boldLabel(tr_("ShowInNew"), page));
	footerText->addWidget(mutedLabel(tr_("AppliesInstantly"), page));
	footer->addLayout(footerText, 1);
	detailShowInNew_ = new ToggleSwitch(page);
	detailShowInNew_->setAccessibleName(tr_("ShowInNew"));
	footer->addWidget(detailShowInNew_);
	layout->addLayout(footer);

	connect(detailShowInNew_, &QAbstractButton::clicked, this,
		[this](bool checked) { manager_->setShowInNew(detailUuid_, checked); });

	auto *remove = new QPushButton(tr_("Remove"), page);
	setClass(remove, "icon-trash");
	layout->addWidget(remove);
	connect(remove, &QPushButton::clicked, this, [this]() {
		const QString uuid = detailUuid_;
		detailUuid_.clear();
		manager_->removeItem(uuid);
	});

	return page;
}

QWidget *OverlayDock::buildEmptyPage()
{
	auto *page = new QWidget(this);
	auto *layout = new QVBoxLayout(page);
	layout->setContentsMargins(20, 20, 20, 20);
	layout->setSpacing(10);
	layout->addStretch(1);

	auto *title = boldLabel(tr_("EmptyTitle"), page);
	QFont titleFont = title->font();
	titleFont.setPointSizeF(titleFont.pointSizeF() * 1.25);
	title->setFont(titleFont);
	title->setAlignment(Qt::AlignCenter);
	title->setWordWrap(true);
	layout->addWidget(title);

	auto *body = mutedLabel(tr_("EmptyBody"), page);
	body->setAlignment(Qt::AlignCenter);
	body->setWordWrap(true);
	layout->addWidget(body);

	auto *addButton = new QPushButton(tr_("Add"), page);
	setClass(addButton, "icon-plus");
	layout->addWidget(addButton, 0, Qt::AlignHCenter);
	connect(addButton, &QPushButton::clicked, this, [this, addButton]() { showAddMenu(addButton); });

	auto *hint = mutedLabel(tr_("EmptyHint"), page);
	hint->setAlignment(Qt::AlignCenter);
	hint->setWordWrap(true);
	layout->addWidget(hint);

	layout->addStretch(1);
	return page;
}

/* ------------------------------------------------------------------ */

void OverlayDock::scheduleRefresh()
{
	if (refreshPending_)
		return;
	refreshPending_ = true;
	/* Deferred so a row is never rebuilt while one of its buttons is still
	 * handling the click that caused the change. */
	QTimer::singleShot(0, this, [this]() { refresh(); });
}

void OverlayDock::refresh()
{
	refreshPending_ = false;

	if (!detailUuid_.isEmpty() && !manager_->find(detailUuid_))
		detailUuid_.clear();

	rebuildList();

	if (!detailUuid_.isEmpty()) {
		rebuildScenes();
		stack_->setCurrentWidget(scenesPage_);
	} else if (manager_->items().isEmpty()) {
		stack_->setCurrentWidget(emptyPage_);
	} else {
		stack_->setCurrentWidget(listPage_);
	}
}

void OverlayDock::rebuildList()
{
	const QList<OverlayItem> &items = manager_->items();
	const int total = static_cast<int>(items.size());

	masterSwitch_->setChecked(manager_->masterEnabled());
	masterStatus_->setText(manager_->masterEnabled() ? tr_("VisibleNow").arg(manager_->visibleNowCount()).arg(total)
							 : tr_("AllOff"));

	const SourceInfo scene = manager_->currentScene();
	const bool hidden = manager_->currentSceneHidden();
	sceneName_->setText(scene.name.isEmpty() ? tr_("NoScene") : scene.name);
	sceneName_->setToolTip(scene.name);
	sceneStatus_->setText(hidden ? tr_("HiddenHere") : tr_("ShownHere"));
	sceneButton_->setText(hidden ? tr_("ShowHere") : tr_("HideHere"));
	sceneButton_->setEnabled(!scene.uuid.isEmpty() && total > 0);

	const int sceneTotal = static_cast<int>(manager_->allScenes().size());

	list_->clear();
	for (const OverlayItem &item : items) {
		const QString uuid = item.uuid;

		auto *row = new QFrame(list_);
		row->setObjectName("acgoRow");
		row->setStyleSheet("QFrame#acgoRow { background: rgba(128, 128, 128, 0.14); border-radius: 6px; }");
		auto *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(6, 6, 6, 6);
		rowLayout->setSpacing(6);

		auto *grip = mutedLabel(QStringLiteral("⋮⋮"), row);
		grip->setCursor(Qt::OpenHandCursor);
		rowLayout->addWidget(grip);

		auto *eye = new QCheckBox(row);
		setClass(eye, "checkbox-icon indicator-visibility");
		eye->setChecked(item.enabled);
		eye->setAccessibleName(tr_("ToggleItem").arg(item.name));
		eye->setToolTip(tr_("ToggleItem").arg(item.name));
		rowLayout->addWidget(eye);

		auto *text = new QVBoxLayout();
		text->setSpacing(0);
		auto *name = boldLabel(item.name, row);
		name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		name->setToolTip(item.name);
		name->setEnabled(item.enabled);
		text->addWidget(name);
		QString type = item.isScene ? tr_("TypeScene") : tr_("TypeSource");
		if (!item.enabled)
			type += QStringLiteral(" · ") + tr_("StateOff");
		text->addWidget(mutedLabel(type, row));
		rowLayout->addLayout(text, 1);

		const int hiddenCount = manager_->hiddenSceneCount(item);
		auto *chip = new QPushButton(row);
		if (hiddenCount == 0) {
			chip->setText(tr_("AllScenes"));
		} else if (hiddenCount >= sceneTotal) {
			chip->setText(tr_("NoScenes"));
			setClass(chip, "text-warning");
		} else {
			chip->setText(tr_("HiddenIn").arg(hiddenCount));
			setClass(chip, "text-warning");
		}
		chip->setToolTip(tr_("WhereShown"));
		chip->setCursor(Qt::PointingHandCursor);
		rowLayout->addWidget(chip);

		connect(eye, &QCheckBox::clicked, this,
			[this, uuid](bool checked) { manager_->setItemEnabled(uuid, checked); });
		connect(chip, &QPushButton::clicked, this, [this, uuid]() {
			detailUuid_ = uuid;
			detailSearch_->clear();
			scheduleRefresh();
		});

		row->setContextMenuPolicy(Qt::CustomContextMenu);
		connect(row, &QWidget::customContextMenuRequested, this,
			[this, uuid, row](const QPoint &pos) { showRowMenu(uuid, row->mapToGlobal(pos)); });

		auto *listItem = new QListWidgetItem(list_);
		listItem->setData(Qt::UserRole, uuid);
		listItem->setSizeHint(row->sizeHint());
		list_->setItemWidget(listItem, row);
	}
}

void OverlayDock::rebuildScenes()
{
	const OverlayItem *item = manager_->find(detailUuid_);
	if (!item)
		return;

	detailTitle_->setText(item->name);
	detailTitle_->setToolTip(item->name);
	detailShowInNew_->setChecked(item->showInNew);

	while (QLayoutItem *child = detailScenesLayout_->takeAt(0)) {
		if (child->widget())
			child->widget()->deleteLater();
		delete child;
	}

	const QString current = manager_->currentScene().uuid;
	const QString uuid = detailUuid_;
	int visible = 0;
	int total = 0;

	for (const SourceInfo &scene : manager_->allScenes()) {
		/* A scene cannot be an overlay on top of itself. */
		if (scene.uuid == uuid)
			continue;

		const bool shown = manager_->isVisibleIn(*item, scene.uuid);
		total++;
		if (shown)
			visible++;

		QString label = scene.name;
		if (scene.uuid == current)
			label += QStringLiteral("  ·  ") + tr_("Current");

		auto *box = new QCheckBox(label, detailScenes_);
		box->setChecked(shown);
		box->setProperty("sceneName", scene.name);
		detailScenesLayout_->addWidget(box);

		const QString sceneUuid = scene.uuid;
		connect(box, &QCheckBox::clicked, this,
			[this, uuid, sceneUuid](bool checked) { manager_->setVisibleIn(uuid, sceneUuid, checked); });
	}
	detailScenesLayout_->addStretch(1);

	detailCount_->setText(tr_("VisibleIn").arg(visible).arg(total));
	applySceneFilter();
}

void OverlayDock::applySceneFilter()
{
	const QString filter = detailSearch_->text().trimmed();
	for (int i = 0; i < detailScenesLayout_->count(); i++) {
		QWidget *widget = detailScenesLayout_->itemAt(i)->widget();
		if (!widget)
			continue;
		const QString name = widget->property("sceneName").toString();
		widget->setVisible(filter.isEmpty() || name.contains(filter, Qt::CaseInsensitive));
	}
}

void OverlayDock::showAddMenu(QWidget *anchor)
{
	QMenu menu(this);

	const QList<SourceInfo> scenes = manager_->addableScenes();
	const QList<SourceInfo> sources = manager_->addableSources();

	if (scenes.isEmpty() && sources.isEmpty()) {
		menu.addAction(tr_("NothingToAdd"))->setEnabled(false);
	}
	if (!scenes.isEmpty()) {
		menu.addSection(tr_("Scenes"));
		for (const SourceInfo &info : scenes)
			menu.addAction(info.name)->setData(info.uuid);
	}
	if (!sources.isEmpty()) {
		menu.addSection(tr_("Sources"));
		for (const SourceInfo &info : sources)
			menu.addAction(info.name)->setData(info.uuid);
	}

	const QAction *chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
	if (chosen && chosen->data().isValid())
		manager_->addSource(chosen->data().toString());
}

void OverlayDock::showRowMenu(QString uuid, QPoint globalPos)
{
	QMenu menu(this);
	const QAction *up = menu.addAction(tr_("MoveUp"));
	const QAction *down = menu.addAction(tr_("MoveDown"));
	menu.addSeparator();
	const QAction *remove = menu.addAction(tr_("Remove"));

	const QAction *chosen = menu.exec(globalPos);
	if (!chosen)
		return;
	if (chosen == up)
		moveRow(uuid, -1);
	else if (chosen == down)
		moveRow(uuid, 1);
	else if (chosen == remove)
		manager_->removeItem(uuid);
}

void OverlayDock::moveRow(const QString &uuid, int delta)
{
	QStringList order;
	for (const OverlayItem &item : manager_->items())
		order.append(item.uuid);

	const int from = static_cast<int>(order.indexOf(uuid));
	const int to = from + delta;
	if (from < 0 || to < 0 || to >= order.size())
		return;

	order.move(from, to);
	manager_->setOrder(order);
}
