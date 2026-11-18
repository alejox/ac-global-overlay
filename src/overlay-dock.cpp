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
#include "updater.hpp"

#include <obs-module.h>

#include <QButtonGroup>
#include <QCheckBox>
#include <QDesktopServices>
#include <QDropEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTimer>
#include <QUrl>
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

QString warningHtml(const QString &text)
{
	return QStringLiteral("<span style=\"color:#d9962b\">%1</span>").arg(text.toHtmlEscaped());
}

QColor groupColor(const GroupInfo &group)
{
	const QColor color(group.color);
	return color.isValid() ? color : QColor(128, 128, 128);
}

QString groupHtml(const GroupInfo &group)
{
	return QStringLiteral("<span style=\"color:%1\">\u25CF</span>&nbsp;%2")
		.arg(groupColor(group).name(), group.name.toHtmlEscaped());
}

QIcon dotIcon(const QColor &color)
{
	QPixmap pixmap(24, 24);
	pixmap.fill(Qt::transparent);
	QPainter painter(&pixmap);
	painter.setRenderHint(QPainter::Antialiasing);
	painter.setPen(Qt::NoPen);
	painter.setBrush(color);
	painter.drawEllipse(QRectF(4.0, 4.0, 16.0, 16.0));
	painter.end();
	return QIcon(pixmap);
}

QLabel *captionLabel(const QString &text, QWidget *parent)
{
	QLabel *label = mutedLabel(text, parent);
	QFont font = label->font();
	font.setPointSizeF(font.pointSizeF() * 0.85);
	font.setBold(true);
	label->setFont(font);
	return label;
}

} // namespace

/* ------------------------------------------------------------------ */

ArrowButton::ArrowButton(Kind kind, QWidget *parent) : QAbstractButton(parent), kind_(kind)
{
	setCursor(Qt::PointingHandCursor);
	setAttribute(Qt::WA_Hover);
	setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

QSize ArrowButton::sizeHint() const
{
	if (kind_ == Kind::Back)
		return QSize(34, 34);
	return kind_ == Kind::Locate ? QSize(30, 34) : QSize(28, 34);
}

void ArrowButton::paintEvent(QPaintEvent *)
{
	QPainter painter(this);
	painter.setRenderHint(QPainter::Antialiasing);

	const QColor text = palette().color(QPalette::WindowText);
	const QRectF box = QRectF(rect()).adjusted(1.0, 1.0, -1.0, -1.0);

	if (kind_ == Kind::Back || underMouse() || hasFocus()) {
		QColor fill = text;
		fill.setAlpha(underMouse() ? 60 : 28);
		QColor border = text;
		border.setAlpha(kind_ == Kind::Back || hasFocus() ? 110 : 0);
		painter.setPen(QPen(border, 1.0));
		painter.setBrush(fill);
		painter.drawRoundedRect(box, 6.0, 6.0);
	}

	painter.setPen(QPen(text, 2.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
	painter.setBrush(Qt::NoBrush);
	const QPointF center = box.center();
	if (kind_ == Kind::Back) {
		/* Left arrow with a shaft. */
		painter.drawLine(QPointF(center.x() - 7.0, center.y()), QPointF(center.x() + 7.0, center.y()));
		painter.drawLine(QPointF(center.x() - 7.0, center.y()), QPointF(center.x() - 2.0, center.y() - 5.0));
		painter.drawLine(QPointF(center.x() - 7.0, center.y()), QPointF(center.x() - 2.0, center.y() + 5.0));
	} else if (kind_ == Kind::Locate) {
		/* Target: a ring with four ticks. */
		painter.setPen(QPen(text, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawEllipse(center, 5.0, 5.0);
		painter.drawLine(QPointF(center.x(), center.y() - 9.0), QPointF(center.x(), center.y() - 5.0));
		painter.drawLine(QPointF(center.x(), center.y() + 5.0), QPointF(center.x(), center.y() + 9.0));
		painter.drawLine(QPointF(center.x() - 9.0, center.y()), QPointF(center.x() - 5.0, center.y()));
		painter.drawLine(QPointF(center.x() + 5.0, center.y()), QPointF(center.x() + 9.0, center.y()));
		painter.drawPoint(center);
	} else {
		painter.drawLine(QPointF(center.x() - 3.0, center.y() - 6.0), QPointF(center.x() + 3.0, center.y()));
		painter.drawLine(QPointF(center.x() + 3.0, center.y()), QPointF(center.x() - 3.0, center.y() + 6.0));
	}
}

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
	layout->setSpacing(0);

	/* Update notice, hidden until the updater has something to say. */
	updateBanner_ = new QFrame(this);
	updateBanner_->setObjectName("acgoUpdate");
	updateBanner_->setStyleSheet("QFrame#acgoUpdate { background: rgba(128, 128, 128, 0.2); }");
	auto *bannerLayout = new QHBoxLayout(updateBanner_);
	bannerLayout->setContentsMargins(10, 6, 8, 6);
	updateText_ = new QLabel(updateBanner_);
	updateText_->setWordWrap(true);
	bannerLayout->addWidget(updateText_, 1);
	updateButton_ = new QPushButton(tr_("UpdateDownload"), updateBanner_);
	bannerLayout->addWidget(updateButton_);
	updateBanner_->hide();
	layout->addWidget(updateBanner_);

	/* Shown after "go to source", with the way back to where you were. */
	editBanner_ = new QFrame(this);
	editBanner_->setObjectName("acgoEdit");
	editBanner_->setStyleSheet("QFrame#acgoEdit { background: rgba(128, 128, 128, 0.2); }");
	auto *editLayout = new QHBoxLayout(editBanner_);
	editLayout->setContentsMargins(10, 6, 8, 6);
	editText_ = new QLabel(editBanner_);
	editText_->setWordWrap(true);
	editLayout->addWidget(editText_, 1);
	editReturn_ = new QPushButton(editBanner_);
	editLayout->addWidget(editReturn_);
	editBanner_->hide();
	layout->addWidget(editBanner_);

	connect(editReturn_, &QPushButton::clicked, this, [this]() {
		const QString target = returnUuid_;
		returnUuid_.clear();
		switchToScene(target);
		scheduleRefresh();
	});

	layout->addWidget(stack_, 1);
	setMinimumWidth(260);

	updater_ = new Updater(this);
	connect(updater_, &Updater::installed, this, [this](const QString &version) {
		updateText_->setText(tr_("UpdateInstalled").arg(version));
		updateButton_->hide();
		updateBanner_->show();
	});
	connect(updater_, &Updater::available, this, [this](const QString &version, const QString &url) {
		updateUrl_ = url;
		updateText_->setText(tr_("UpdateAvailable").arg(version));
		updateButton_->setVisible(!url.isEmpty());
		updateBanner_->show();
	});
	connect(updateButton_, &QPushButton::clicked, this, [this]() {
		if (updateUrl_.startsWith("https://github.com/alejox/ac-global-overlay/"))
			QDesktopServices::openUrl(QUrl(updateUrl_));
	});

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

	/* Checked a few seconds after startup so it never slows OBS down. */
	if (event == OBS_FRONTEND_EVENT_FINISHED_LOADING)
		QTimer::singleShot(5000, dock->updater_, [dock]() { dock->updater_->check(); });

	switch (event) {
	case OBS_FRONTEND_EVENT_FINISHED_LOADING:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		/* AC Scene Groups may load after this plugin, so its groups (and
		 * its change signal) are picked up once everything is loaded. */
		dock->manager_->refreshGroups();
		dock->manager_->apply();
		dock->scheduleRefresh();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
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
		dock->returnUuid_.clear();
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
	header->setSpacing(8);
	auto *back = new ArrowButton(ArrowButton::Kind::Back, page);
	back->setAccessibleName(tr_("Back"));
	back->setToolTip(tr_("Back"));
	header->addWidget(back);
	auto *headerText = new QVBoxLayout();
	headerText->setSpacing(1);
	detailTitle_ = boldLabel(QString(), page);
	detailTitle_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
	headerText->addWidget(detailTitle_);
	headerText->addWidget(mutedLabel(tr_("WhereShown"), page));
	header->addLayout(headerText, 1);
	layout->addLayout(header);

	connect(back, &QAbstractButton::clicked, this, [this]() {
		detailUuid_.clear();
		scheduleRefresh();
	});

	/* All scenes / only in groups */
	detailModeBar_ = new QWidget(page);
	auto *modeLayout = new QHBoxLayout(detailModeBar_);
	modeLayout->setContentsMargins(0, 0, 0, 0);
	modeLayout->setSpacing(4);
	detailModeAll_ = new QPushButton(tr_("AllScenes"), detailModeBar_);
	detailModeGroups_ = new QPushButton(tr_("ModeGroups"), detailModeBar_);
	detailModeAll_->setCheckable(true);
	detailModeGroups_->setCheckable(true);
	auto *modeGroup = new QButtonGroup(detailModeBar_);
	modeGroup->setExclusive(true);
	modeGroup->addButton(detailModeAll_);
	modeGroup->addButton(detailModeGroups_);
	modeLayout->addWidget(detailModeAll_, 1);
	modeLayout->addWidget(detailModeGroups_, 1);
	layout->addWidget(detailModeBar_);

	connect(detailModeAll_, &QPushButton::clicked, this, [this]() { manager_->setGroupMode(detailUuid_, false); });
	connect(detailModeGroups_, &QPushButton::clicked, this,
		[this]() { manager_->setGroupMode(detailUuid_, true); });

	/* Groups */
	detailGroups_ = new QWidget(page);
	auto *groupsOuter = new QVBoxLayout(detailGroups_);
	groupsOuter->setContentsMargins(0, 0, 0, 0);
	groupsOuter->setSpacing(6);
	groupsOuter->addWidget(captionLabel(tr_("Groups"), detailGroups_));
	detailGroupsLayout_ = new QVBoxLayout();
	detailGroupsLayout_->setContentsMargins(2, 0, 2, 0);
	detailGroupsLayout_->setSpacing(6);
	groupsOuter->addLayout(detailGroupsLayout_);
	detailGroupNotice_ = new QLabel(tr_("GroupUnavailable"), detailGroups_);
	setClass(detailGroupNotice_, "text-warning");
	detailGroupNotice_->setWordWrap(true);
	groupsOuter->addWidget(detailGroupNotice_);
	layout->addWidget(detailGroups_);

	detailScenesCaption_ = captionLabel(tr_("GroupScenes"), page);
	layout->addWidget(detailScenesCaption_);

	detailSearch_ = new QLineEdit(page);
	detailSearch_->setPlaceholderText(tr_("SearchScene"));
	detailSearch_->setClearButtonEnabled(true);
	layout->addWidget(detailSearch_);
	connect(detailSearch_, &QLineEdit::textChanged, this, [this]() { applySceneFilter(); });

	auto *tools = new QHBoxLayout();
	detailCount_ = mutedLabel(QString(), page);
	tools->addWidget(detailCount_, 1);
	detailAllNone_ = new QWidget(page);
	auto *allNoneLayout = new QHBoxLayout(detailAllNone_);
	allNoneLayout->setContentsMargins(0, 0, 0, 0);
	auto *all = new QPushButton(tr_("All"), detailAllNone_);
	auto *none = new QPushButton(tr_("None"), detailAllNone_);
	allNoneLayout->addWidget(all);
	allNoneLayout->addWidget(none);
	tools->addWidget(detailAllNone_);
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

	/* New scenes switch: only meaningful for "all scenes" */
	detailNewScenes_ = new QWidget(page);
	auto *footer = new QHBoxLayout(detailNewScenes_);
	footer->setContentsMargins(0, 0, 0, 0);
	auto *footerText = new QVBoxLayout();
	footerText->setSpacing(1);
	footerText->addWidget(boldLabel(tr_("ShowInNew"), detailNewScenes_));
	footerText->addWidget(mutedLabel(tr_("AppliesInstantly"), detailNewScenes_));
	footer->addLayout(footerText, 1);
	detailShowInNew_ = new ToggleSwitch(detailNewScenes_);
	detailShowInNew_->setAccessibleName(tr_("ShowInNew"));
	footer->addWidget(detailShowInNew_);
	layout->addWidget(detailNewScenes_);

	connect(detailShowInNew_, &QAbstractButton::clicked, this,
		[this](bool checked) { manager_->setShowInNew(detailUuid_, checked); });

	detailGroupHint_ = mutedLabel(tr_("GroupHint"), page);
	detailGroupHint_->setWordWrap(true);
	layout->addWidget(detailGroupHint_);

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

	const SourceInfo current = manager_->currentScene();
	if (returnUuid_ == current.uuid)
		returnUuid_.clear();
	editBanner_->setVisible(!returnUuid_.isEmpty());
	if (!returnUuid_.isEmpty()) {
		editText_->setText(current.uuid == manager_->stageUuid() ? tr_("EditingStage")
									 : tr_("Editing").arg(current.name));
		editReturn_->setText(tr_("ReturnTo").arg(returnName_));
	}

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

QString OverlayDock::summaryHtml(const OverlayItem &item, int sceneTotal) const
{
	const int hidden = manager_->hiddenSceneCount(item);
	QString html;
	bool nowhere = false;

	if (item.groupMode) {
		const QList<GroupInfo> groups = manager_->selectedGroups(item);
		if (groups.isEmpty()) {
			html = warningHtml(tr_("NoGroup"));
			nowhere = true;
		} else {
			QString names;
			if (groups.size() == 1)
				names = groupHtml(groups[0]);
			else if (groups.size() == 2)
				names = tr_("JoinTwo").arg(groupHtml(groups[0]), groupHtml(groups[1]));
			else
				names = tr_("JoinMore")
						.arg(groupHtml(groups[0]), groupHtml(groups[1]))
						.arg(groups.size() - 2);
			html = tr_("InGroups").arg(names);
		}
	} else if (hidden > 0 && hidden >= sceneTotal) {
		html = warningHtml(tr_("NoScenes"));
		nowhere = true;
	} else {
		html = tr_("AllScenes").toHtmlEscaped();
	}

	if (item.isAudio)
		html = tr_("TypeAudio").toHtmlEscaped() + QStringLiteral(" \u00B7 ") + html;
	if (hidden > 0 && !nowhere)
		html += QStringLiteral(" \u00B7 ") + warningHtml(tr_("HiddenInLower").arg(hidden));
	if (!item.enabled)
		html += QStringLiteral(" \u00B7 ") + tr_("StateOff").toHtmlEscaped();
	return html;
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
	const bool canShow = manager_->canShowInCurrentScene();
	sceneName_->setText(scene.name.isEmpty() ? tr_("NoScene") : scene.name);
	sceneName_->setToolTip(scene.name);
	if (!hidden)
		sceneStatus_->setText(tr_("ShownHere"));
	else
		sceneStatus_->setText(canShow ? tr_("HiddenHere") : tr_("NotInGroups"));
	sceneButton_->setText(hidden ? tr_("ShowHere") : tr_("HideHere"));
	sceneButton_->setEnabled(!scene.uuid.isEmpty() && total > 0 && (!hidden || canShow));

	const int sceneTotal = static_cast<int>(manager_->allScenes().size());

	list_->clear();
	for (const OverlayItem &item : items) {
		const QString uuid = item.uuid;

		auto *row = new QFrame(list_);
		row->setObjectName("acgoRow");
		row->setStyleSheet("QFrame#acgoRow { background: rgba(128, 128, 128, 0.14); border-radius: 6px; }");
		auto *rowLayout = new QHBoxLayout(row);
		rowLayout->setContentsMargins(6, 6, 4, 6);
		rowLayout->setSpacing(6);

		auto *grip = mutedLabel(QStringLiteral("\u22EE\u22EE"), row);
		grip->setCursor(Qt::OpenHandCursor);
		rowLayout->addWidget(grip);

		auto *eye = new QCheckBox(row);
		setClass(eye, "checkbox-icon indicator-visibility");
		eye->setChecked(item.enabled);
		eye->setAccessibleName(tr_("ToggleItem").arg(item.name));
		eye->setToolTip(tr_("ToggleItem").arg(item.name));
		rowLayout->addWidget(eye);

		auto *text = new QVBoxLayout();
		text->setSpacing(1);
		auto *name = boldLabel(item.name, row);
		name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		name->setToolTip(item.name);
		name->setEnabled(item.enabled);
		text->addWidget(name);

		auto *summary = new QLabel(row);
		setClass(summary, "text-muted");
		summary->setTextFormat(Qt::RichText);
		summary->setText(summaryHtml(item, sceneTotal));
		summary->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
		summary->setToolTip(summary->text());
		text->addWidget(summary);
		rowLayout->addLayout(text, 1);

		auto *locate = new ArrowButton(ArrowButton::Kind::Locate, row);
		locate->setAccessibleName(tr_("GoToSource"));
		locate->setToolTip(tr_("GoToSourceTip"));
		rowLayout->addWidget(locate);
		connect(locate, &QAbstractButton::clicked, this, [this, uuid]() { goToSource(uuid); });

		auto *open = new ArrowButton(ArrowButton::Kind::Forward, row);
		open->setAccessibleName(tr_("OpenDetail").arg(item.name));
		open->setToolTip(tr_("WhereShown"));
		rowLayout->addWidget(open);

		connect(eye, &QCheckBox::clicked, this,
			[this, uuid](bool checked) { manager_->setItemEnabled(uuid, checked); });
		connect(open, &QAbstractButton::clicked, this, [this, uuid]() {
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

	const bool groupMode = item->groupMode;
	const QString uuid = detailUuid_;

	detailTitle_->setText(item->name);
	detailTitle_->setToolTip(item->name);
	detailShowInNew_->setChecked(item->showInNew);

	/* Without AC Scene Groups the group option does not exist, unless this
	 * overlay was already limited to groups and needs a way back. */
	detailModeBar_->setVisible(manager_->groupsAvailable() || groupMode);
	detailModeAll_->setChecked(!groupMode);
	detailModeGroups_->setChecked(groupMode);
	detailGroups_->setVisible(groupMode);
	detailScenesCaption_->setVisible(groupMode);
	detailGroupHint_->setVisible(groupMode);
	detailAllNone_->setVisible(!groupMode);
	detailNewScenes_->setVisible(!groupMode);

	while (QLayoutItem *child = detailGroupsLayout_->takeAt(0)) {
		if (child->widget())
			child->widget()->deleteLater();
		delete child;
	}

	bool anyUnavailable = false;
	if (groupMode) {
		for (const GroupInfo &group : manager_->listedGroups(*item)) {
			const QString groupId = group.id;
			QString label = group.name;
			if (!group.available) {
				anyUnavailable = true;
				label += QStringLiteral("  \u00B7  ") + tr_("GroupMissing");
			}

			auto *rowWidget = new QWidget(detailGroups_);
			auto *rowLayout = new QHBoxLayout(rowWidget);
			rowLayout->setContentsMargins(0, 0, 0, 0);
			auto *box = new QCheckBox(label, rowWidget);
			box->setIcon(dotIcon(groupColor(group)));
			box->setIconSize(QSize(12, 12));
			box->setChecked(item->groupIds.contains(groupId));
			rowLayout->addWidget(box, 1);
			rowLayout->addWidget(
				mutedLabel(tr_("SceneCount").arg(static_cast<int>(group.scenes.size())), rowWidget));
			detailGroupsLayout_->addWidget(rowWidget);

			connect(box, &QCheckBox::clicked, this, [this, uuid, groupId](bool checked) {
				manager_->setGroupSelected(uuid, groupId, checked);
			});
		}
	}
	detailGroupNotice_->setVisible(groupMode && anyUnavailable);

	while (QLayoutItem *child = detailScenesLayout_->takeAt(0)) {
		if (child->widget())
			child->widget()->deleteLater();
		delete child;
	}

	const QString current = manager_->currentScene().uuid;
	const QSet<QString> groupScenes = manager_->groupScenes(*item);
	int visible = 0;
	int total = 0;

	for (const SourceInfo &scene : manager_->allScenes()) {
		/* A scene cannot be an overlay on top of itself. */
		if (scene.uuid == uuid)
			continue;
		/* In group mode only the scenes of the listed groups are offered. */
		if (groupMode && !groupScenes.contains(scene.uuid))
			continue;

		const bool shown = manager_->isVisibleIn(*item, scene.uuid);
		total++;
		if (shown)
			visible++;

		QString label = scene.name;
		if (scene.uuid == current)
			label += QStringLiteral("  \u00B7  ") + tr_("Current");

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

	/* What is on screen right now comes first: it is the most likely pick. */
	const QList<SourceInfo> here = manager_->currentSceneSources();
	const QList<SourceInfo> scenes = manager_->addableScenes();
	QList<SourceInfo> sources;
	for (const SourceInfo &info : manager_->addableSources()) {
		bool listed = false;
		for (const SourceInfo &other : here)
			listed = listed || other.uuid == info.uuid;
		if (!listed)
			sources.append(info);
	}

	const QList<SourceInfo> audio = manager_->addableAudio();

	if (here.isEmpty() && scenes.isEmpty() && sources.isEmpty() && audio.isEmpty()) {
		menu.addAction(tr_("NothingToAdd"))->setEnabled(false);
	}
	if (!here.isEmpty()) {
		menu.addSection(tr_("InCurrentScene"));
		for (const SourceInfo &info : here)
			menu.addAction(info.name)->setData(info.uuid);
	}
	if (!scenes.isEmpty()) {
		menu.addSection(tr_("Scenes"));
		for (const SourceInfo &info : scenes)
			menu.addAction(info.name)->setData(info.uuid);
	}
	if (!sources.isEmpty()) {
		menu.addSection(here.isEmpty() ? tr_("Sources") : tr_("OtherSources"));
		for (const SourceInfo &info : sources)
			menu.addAction(info.name)->setData(info.uuid);
	}

	if (!audio.isEmpty()) {
		menu.addSection(tr_("AudioSources"));
		for (const SourceInfo &info : audio)
			menu.addAction(info.name)->setData(info.uuid);
	}

	const QAction *chosen = menu.exec(anchor->mapToGlobal(QPoint(0, anchor->height())));
	if (chosen && chosen->data().isValid())
		manager_->addSource(chosen->data().toString());
}

void OverlayDock::showRowMenu(QString uuid, QPoint globalPos)
{
	const OverlayItem *item = manager_->find(uuid);
	if (!item)
		return;
	const bool isScene = item->isScene;
	const bool positioned = !item->isScene && !item->isAudio;

	QMenu menu(this);
	const QAction *locate = menu.addAction(tr_("GoToSource"));
	const QAction *properties = isScene ? nullptr : menu.addAction(tr_("Properties"));
	const QAction *filters = menu.addAction(tr_("Filters"));

	QList<QAction *> presets;
	if (positioned) {
		QMenu *quick = menu.addMenu(tr_("QuickPosition"));
		const char *names[] = {"PresetFit",      "PresetCenter",     "PresetTopLeft",
				       "PresetTopRight", "PresetBottomLeft", "PresetBottomRight"};
		for (const char *name : names)
			presets.append(quick->addAction(tr_(name)));
	}

	menu.addSeparator();
	const QAction *up = menu.addAction(tr_("MoveUp"));
	const QAction *down = menu.addAction(tr_("MoveDown"));
	menu.addSeparator();
	const QAction *remove = menu.addAction(tr_("Remove"));

	QAction *chosen = menu.exec(globalPos);
	if (!chosen)
		return;

	if (chosen == locate) {
		goToSource(uuid);
	} else if (chosen == properties || chosen == filters) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.toUtf8().constData());
		if (source && chosen == properties)
			obs_frontend_open_source_properties(source);
		else if (source)
			obs_frontend_open_source_filters(source);
	} else if (chosen == up) {
		moveRow(uuid, -1);
	} else if (chosen == down) {
		moveRow(uuid, 1);
	} else if (chosen == remove) {
		manager_->removeItem(uuid);
	} else {
		const int preset = static_cast<int>(presets.indexOf(chosen));
		if (preset >= 0)
			manager_->applyPreset(uuid, static_cast<OverlayPreset>(preset));
	}
}

void OverlayDock::switchToScene(const QString &sceneUuid)
{
	OBSSourceAutoRelease scene = obs_get_source_by_uuid(sceneUuid.toUtf8().constData());
	if (!scene || !obs_source_is_scene(scene))
		return;

	if (obs_frontend_preview_program_mode_active())
		obs_frontend_set_current_preview_scene(scene);
	else
		obs_frontend_set_current_scene(scene);
}

void OverlayDock::goToSource(const QString &uuid)
{
	/* Sound has no place on screen: its settings are the thing to go to. */
	const OverlayItem *item = manager_->find(uuid);
	if (!item)
		return;
	if (item->isAudio) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(uuid.toUtf8().constData());
		if (source)
			obs_frontend_open_source_properties(source);
		return;
	}

	const bool studioMode = obs_frontend_preview_program_mode_active();

	/* Outside studio mode this changes what the audience sees. */
	if (!studioMode && (obs_frontend_streaming_active() || obs_frontend_recording_active())) {
		const auto answer = QMessageBox::question(this, tr_("LiveWarnTitle"), tr_("LiveWarn"),
							  QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
		if (answer != QMessageBox::Yes)
			return;
	}

	const SourceInfo before = manager_->currentScene();
	const QString target = manager_->editScene(uuid);
	if (target.isEmpty())
		return;

	if (!studioMode && before.uuid != target && returnUuid_.isEmpty()) {
		returnUuid_ = before.uuid;
		returnName_ = before.name;
	}

	/* A stage scene that was just created reaches the OBS scene list a
	 * moment later, so the switch waits for it. */
	QTimer::singleShot(150, this, [this, target]() {
		switchToScene(target);
		scheduleRefresh();
	});
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
