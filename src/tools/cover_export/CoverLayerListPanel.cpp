#include "tools/cover_export/CoverLayerListPanel.h"

#include "tools/cover_export/CoverLayerListModel.h"
#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAbstractItemView>
#include <QAction>
#include <QEvent>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QPoint>
#include <QPushButton>
#include <QVBoxLayout>

namespace miacode::cover_export {
namespace {

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

}  // namespace

CoverLayerListPanel::CoverLayerListPanel(CoverStudioPanel* studio, QWidget* parent)
    : QWidget(parent)
    , studio_(studio)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* title = new QLabel(l10n(QStringLiteral("Layers"), QStringLiteral("图层")), this);
    title->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(title);

    model_ = new CoverLayerListModel(this);
    if (studio_ != nullptr) {
        model_->setLayoutModel(studio_->layoutModel());
    }

    view_ = new QListView(this);
    view_->setModel(model_);
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setStyleSheet(UiTheme::outlineListStyleSheet());
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    // Drag rows to change z-order (the model maps view rows → z, §12.12).
    view_->setDragEnabled(true);
    view_->setAcceptDrops(true);
    view_->setDropIndicatorShown(true);
    view_->setDragDropMode(QAbstractItemView::InternalMove);
    view_->setDefaultDropAction(Qt::MoveAction);
    view_->installEventFilter(this);
    layout->addWidget(view_, 1);

    // Simplified to two buttons (诉求 8 / R3): add + delete. Z-order (上移/下移/置顶/
    // 置底) and show/lock move to the right-click context menu + keyboard; duplicate
    // is removed entirely.
    const QString compactButton = UiTheme::dialogPushButtonStyleSheet()
        + QStringLiteral("QPushButton { min-width: 0px; padding: 4px 10px; }");
    auto* row = new QHBoxLayout();
    row->setSpacing(6);
    auto* addButton = new QPushButton(l10n(QStringLiteral("＋ Add frame"), QStringLiteral("＋ 添加谱面帧")), this);
    auto* removeButton = new QPushButton(l10n(QStringLiteral("Delete"), QStringLiteral("删除")), this);
    addButton->setStyleSheet(compactButton);
    removeButton->setStyleSheet(compactButton);
    addButton->setToolTip(l10n(QStringLiteral("Add a chart frame (A)"), QStringLiteral("添加谱面帧（快捷键 A）")));
    removeButton->setToolTip(l10n(QStringLiteral("Delete the selected layer (Delete)"),
                                  QStringLiteral("删除当前图层（Delete）")));
    addButton->setAccessibleName(addButton->toolTip());
    removeButton->setAccessibleName(removeButton->toolTip());
    row->addWidget(addButton);
    row->addWidget(removeButton);
    row->addStretch(1);
    layout->addLayout(row);

    connect(view_, &QWidget::customContextMenuRequested, this, &CoverLayerListPanel::showContextMenu);
    connect(view_->selectionModel(), &QItemSelectionModel::currentChanged, this, [this](const QModelIndex& current) {
        if (studio_ == nullptr || !current.isValid()) {
            return;
        }
        studio_->setActiveLayerKey(model_->data(current, CoverLayerListModel::KeyRole).toString());
    });
    connect(model_, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex& topLeft, const QModelIndex&, const QList<int>& roles) {
        if (studio_ == nullptr || !topLeft.isValid()
            || (!roles.contains(Qt::CheckStateRole) && !roles.contains(CoverLayerListModel::VisibleRole))) {
            return;
        }
        studio_->setActiveLayerKey(model_->data(topLeft, CoverLayerListModel::KeyRole).toString());
        studio_->setActiveLayerVisible(model_->data(topLeft, CoverLayerListModel::VisibleRole).toBool());
    });
    connect(addButton, &QPushButton::clicked, studio_, &CoverStudioPanel::addChartFrameLayer);
    connect(removeButton, &QPushButton::clicked, studio_, &CoverStudioPanel::removeActiveLayer);
    // A reorder (drag / context menu) resets the model, which clears the view's
    // selection — re-assert the active layer's highlight afterwards.
    connect(model_, &QAbstractItemModel::modelReset, this, [this] {
        if (studio_ != nullptr) syncSelectionFromStudio(studio_->activeLayerKey());
    });
    if (studio_ != nullptr) {
        connect(studio_, &CoverStudioPanel::activeLayerChanged, this, &CoverLayerListPanel::syncSelectionFromStudio);
        connect(studio_, &CoverStudioPanel::compositionChanged, model_, &CoverLayerListModel::refresh);
        syncSelectionFromStudio(studio_->activeLayerKey());
    }
}

void CoverLayerListPanel::syncSelectionFromStudio(const QString& key)
{
    const int row = model_->rowForKey(key);
    if (row < 0) {
        return;
    }
    const QModelIndex index = model_->index(row, 0);
    view_->selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
}

bool CoverLayerListPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == view_ && event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if ((keyEvent->key() == Qt::Key_Delete || keyEvent->key() == Qt::Key_Backspace)
            && keyEvent->modifiers() == Qt::NoModifier) {
            if (studio_ != nullptr) {
                studio_->removeActiveLayer();   // auto-selects the neighbour → hold to clear
            }
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void CoverLayerListPanel::showContextMenu(const QPoint& pos)
{
    if (studio_ == nullptr || model_ == nullptr) {
        return;
    }
    const QModelIndex index = view_->indexAt(pos);
    if (!index.isValid()) {
        return;
    }
    const QString key = model_->data(index, CoverLayerListModel::KeyRole).toString();
    studio_->setActiveLayerKey(key);   // right-click selects the row first
    CoverLayoutModel* layout = studio_->layoutModel();
    CoverLayer* layer = layout != nullptr ? layout->layer(key) : nullptr;
    if (layer == nullptr) {
        return;
    }
    const bool isCard = layer->kind() == QStringLiteral("card");

    QMenu menu(this);
    UiTheme::styleRoundedMenu(menu);
    QAction* visAction = menu.addAction(layer->visible()
        ? l10n(QStringLiteral("Hide"), QStringLiteral("隐藏"))
        : l10n(QStringLiteral("Show"), QStringLiteral("显示")));
    QAction* lockAction = menu.addAction(layer->locked()
        ? l10n(QStringLiteral("Unlock"), QStringLiteral("解锁"))
        : l10n(QStringLiteral("Lock"), QStringLiteral("锁定")));
    menu.addSeparator();
    QAction* upAction = menu.addAction(l10n(QStringLiteral("Move up"), QStringLiteral("上移")));
    QAction* downAction = menu.addAction(l10n(QStringLiteral("Move down"), QStringLiteral("下移")));
    QAction* topAction = menu.addAction(l10n(QStringLiteral("Bring to front"), QStringLiteral("置顶")));
    QAction* bottomAction = menu.addAction(l10n(QStringLiteral("Send to back"), QStringLiteral("置底")));
    menu.addSeparator();
    QAction* deleteAction = menu.addAction(l10n(QStringLiteral("Delete"), QStringLiteral("删除")));
    deleteAction->setEnabled(!isCard);   // the card is the stable, non-deletable layer

    QAction* chosen = menu.exec(view_->viewport()->mapToGlobal(pos));
    if (chosen == nullptr) {
        return;
    }
    if (chosen == visAction) {
        studio_->setActiveLayerVisible(!layer->visible());
    } else if (chosen == lockAction) {
        studio_->setActiveLayerLocked(!layer->locked());
    } else if (chosen == upAction) {
        studio_->moveActiveLayerUp();
    } else if (chosen == downAction) {
        studio_->moveActiveLayerDown();
    } else if (chosen == topAction) {
        studio_->moveActiveLayerToTop();
    } else if (chosen == bottomAction) {
        studio_->moveActiveLayerToBottom();
    } else if (chosen == deleteAction) {
        studio_->removeActiveLayer();
    }
}

}  // namespace miacode::cover_export
