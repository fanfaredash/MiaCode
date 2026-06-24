#include "tools/cover_export/CoverLayerListPanel.h"

#include "tools/cover_export/CoverLayerListModel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAbstractItemView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QListView>
#include <QPushButton>
#include <QVBoxLayout>

namespace miacode::cover_export {
namespace {

QString l10n(const QString& en, const QString& zh)
{
    return UiText::isChineseUi() ? zh : en;
}

void setupToolButton(QPushButton* button, const QString& tooltip)
{
    button->setToolTip(tooltip);
    button->setAccessibleName(tooltip);
    button->setStyleSheet(UiTheme::dialogPushButtonStyleSheet()
        + QStringLiteral("QPushButton { min-width: 22px; max-width: 22px;"
                         " min-height: 22px; max-height: 22px; padding: 0; }"));
    button->setFixedSize(24, 24);
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
    layout->addWidget(view_, 1);

    auto* row = new QHBoxLayout();
    row->setSpacing(4);
    auto* addButton = new QPushButton(QStringLiteral("+"), this);
    auto* duplicateButton = new QPushButton(QStringLiteral("⧉"), this);
    auto* removeButton = new QPushButton(QStringLiteral("-"), this);
    auto* upButton = new QPushButton(QStringLiteral("^"), this);
    auto* downButton = new QPushButton(QStringLiteral("v"), this);
    auto* topButton = new QPushButton(QStringLiteral("⇈"), this);
    auto* bottomButton = new QPushButton(QStringLiteral("⇊"), this);
    setupToolButton(addButton, l10n(QStringLiteral("Add chart frame"), QStringLiteral("添加谱面帧")));
    setupToolButton(duplicateButton, l10n(QStringLiteral("Duplicate layer"), QStringLiteral("复制图层")));
    setupToolButton(removeButton, l10n(QStringLiteral("Delete layer"), QStringLiteral("删除图层")));
    setupToolButton(upButton, l10n(QStringLiteral("Move up"), QStringLiteral("上移")));
    setupToolButton(downButton, l10n(QStringLiteral("Move down"), QStringLiteral("下移")));
    setupToolButton(topButton, l10n(QStringLiteral("Bring to front"), QStringLiteral("置顶")));
    setupToolButton(bottomButton, l10n(QStringLiteral("Send to back"), QStringLiteral("置底")));
    row->addWidget(addButton);
    row->addWidget(duplicateButton);
    row->addWidget(removeButton);
    row->addWidget(upButton);
    row->addWidget(downButton);
    row->addWidget(topButton);
    row->addWidget(bottomButton);
    row->addStretch(1);
    layout->addLayout(row);

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
    connect(duplicateButton, &QPushButton::clicked, studio_, &CoverStudioPanel::duplicateActiveLayer);
    connect(removeButton, &QPushButton::clicked, studio_, &CoverStudioPanel::removeActiveLayer);
    connect(upButton, &QPushButton::clicked, studio_, &CoverStudioPanel::moveActiveLayerUp);
    connect(downButton, &QPushButton::clicked, studio_, &CoverStudioPanel::moveActiveLayerDown);
    connect(topButton, &QPushButton::clicked, studio_, &CoverStudioPanel::moveActiveLayerToTop);
    connect(bottomButton, &QPushButton::clicked, studio_, &CoverStudioPanel::moveActiveLayerToBottom);
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

}  // namespace miacode::cover_export
