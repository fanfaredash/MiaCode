#include "tools/cover_export/CoverLayerListPanel.h"

#include "tools/cover_export/CoverLayerListModel.h"
#include "tools/cover_export/CoverLayoutModel.h"
#include "tools/cover_export/CoverStudioPanel.h"
#include "UiText.h"
#include "UiComponents.h"
#include "UiTheme.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCursor>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QKeyEvent>
#include <QLabel>
#include <QListView>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QPoint>
#include <QPushButton>
#include <QSizePolicy>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QStyleOption>
#include <QToolTip>
#include <QVBoxLayout>

#include <functional>

namespace miacode::cover_export {
namespace {

constexpr int kLayerRowHeight = 58;
constexpr int kLayerRowRadius = 6;
constexpr int kLayerRowHPad = 8;
constexpr int kInlineControlSize = 22;
constexpr int kInlineControlGap = 4;
constexpr int kLayerThumbSize = 38;

enum InlineLayerControl {
    NoInlineControl = 0,
    VisibilityInlineControl = 1,
    LockInlineControl = 2,
};

QString formatOpacity(qreal opacity)
{
    return QStringLiteral("%1%").arg(qRound(qBound<qreal>(0.0, opacity, 1.0) * 100.0));
}

void applyPanelTitleStyle(QLabel* title)
{
    if (title == nullptr) {
        return;
    }
    QFont titleFont = title->font();
    titleFont.setWeight(QFont::DemiBold);
    titleFont.setPointSizeF(qMax<qreal>(titleFont.pointSizeF(), 10.5));
    title->setFont(titleFont);
    title->setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: 13px; font-weight: 700; padding: 0 6px; }")
        .arg(UiTheme::colors().textPrimary.name(QColor::HexRgb)));
}

QRect rowContentRect(const QStyleOptionViewItem& option)
{
    return option.rect.adjusted(4, 3, -4, -3);
}

QRect visibilityControlRect(const QRect& content)
{
    const int y = content.top() + (content.height() - kInlineControlSize) / 2;
    return QRect(content.left() + kLayerRowHPad, y, kInlineControlSize, kInlineControlSize);
}

QRect lockControlRect(const QRect& content)
{
    QRect rect = visibilityControlRect(content);
    rect.translate(kInlineControlSize + kInlineControlGap, 0);
    return rect;
}

InlineLayerControl inlineControlAt(const QStyleOptionViewItem& option, const QPoint& pos)
{
    const QRect content = rowContentRect(option);
    if (visibilityControlRect(content).contains(pos)) {
        return VisibilityInlineControl;
    }
    if (lockControlRect(content).contains(pos)) {
        return LockInlineControl;
    }
    return NoInlineControl;
}

void drawEyeIcon(QPainter* painter, const QRect& rect, const QColor& color, bool visible)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    const QRectF eye = rect.adjusted(4, 6, -4, -6);
    QPainterPath path;
    path.moveTo(eye.left(), eye.center().y());
    path.cubicTo(eye.left() + eye.width() * 0.26, eye.top(),
                  eye.right() - eye.width() * 0.26, eye.top(),
                  eye.right(), eye.center().y());
    path.cubicTo(eye.right() - eye.width() * 0.26, eye.bottom(),
                  eye.left() + eye.width() * 0.26, eye.bottom(),
                  eye.left(), eye.center().y());
    painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path);
    painter->setBrush(color);
    painter->setPen(Qt::NoPen);
    painter->drawEllipse(rect.center(), 2, 2);
    if (!visible) {
        painter->setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap));
        painter->drawLine(rect.bottomLeft() + QPoint(5, -4), rect.topRight() + QPoint(-5, 4));
    }
    painter->restore();
}

void drawLockIcon(QPainter* painter, const QRect& rect, const QColor& color, bool locked)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->setBrush(Qt::NoBrush);
    const QRectF body(rect.left() + 6.0, rect.top() + 10.0, rect.width() - 12.0, rect.height() - 14.0);
    painter->drawRoundedRect(body, 2.0, 2.0);
    if (locked) {
        painter->drawArc(QRectF(rect.left() + 7.0, rect.top() + 4.0, rect.width() - 14.0, 13.0),
                         0, 180 * 16);
    } else {
        painter->drawArc(QRectF(rect.left() + 8.0, rect.top() + 4.0, rect.width() - 14.0, 13.0),
                         20 * 16, 145 * 16);
        painter->drawLine(QPointF(rect.left() + 15.0, rect.top() + 10.5),
                          QPointF(rect.left() + 18.0, rect.top() + 10.5));
    }
    painter->restore();
}

void drawInlineButton(QPainter* painter,
                      const QRect& rect,
                      bool active,
                      bool hovered,
                      const QColor& iconColor,
                      const QColor& activeBg,
                      const QColor& hoverBg,
                      const std::function<void(QPainter*, const QRect&, const QColor&)>& drawIcon)
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (active || hovered) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(active ? activeBg : hoverBg);
        painter->drawRoundedRect(rect, 5.0, 5.0);
    }
    drawIcon(painter, rect, iconColor);
    painter->restore();
}

class CoverLayerItemDelegate final : public QStyledItemDelegate {
public:
    explicit CoverLayerItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(kLayerRowHeight);
        return size;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem opt(option);
        initStyleOption(&opt, index);

        const UiTheme::Colors& c = UiTheme::colors();
        const bool selected = opt.state.testFlag(QStyle::State_Selected);
        const bool hovered = opt.state.testFlag(QStyle::State_MouseOver);
        const bool visible = index.data(CoverLayerListModel::VisibleRole).toBool();
        const bool locked = index.data(CoverLayerListModel::LockedRole).toBool();
        const qreal opacity = index.data(CoverLayerListModel::OpacityRole).toReal();
        const QString title = index.data(CoverLayerListModel::LabelRole).toString();
        QString subtitle = index.data(CoverLayerListModel::SubtitleRole).toString();
        if (!visible) {
            subtitle += UiText::text(QStringLiteral("cover.hidden"));
        } else if (!qFuzzyCompare(opacity, 1.0)) {
            subtitle += QStringLiteral(" · ") + formatOpacity(opacity);
        }
        const QImage thumbnail = qvariant_cast<QImage>(index.data(CoverLayerListModel::ThumbnailRole));

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        const QRect content = rowContentRect(opt);
        const QColor selectedBg = c.dark ? QColor("#263344") : QColor("#EDF4FF");
        const QColor hoverBg = c.dark ? QColor("#2A3442") : QColor("#F3F7FD");
        const QColor border = selected ? c.accent : (hovered ? c.borderStrong : Qt::transparent);
        painter->setPen(QPen(border, selected ? 1.2 : 1.0));
        painter->setBrush(selected ? selectedBg : (hovered ? hoverBg : Qt::transparent));
        painter->drawRoundedRect(content, kLayerRowRadius, kLayerRowRadius);

        const QRect visibleRect = visibilityControlRect(content);
        const QRect lockRect = lockControlRect(content);
        const QColor iconColor = visible ? c.iconPrimary : c.textMuted;
        const QColor activeBg = c.dark ? QColor(59, 130, 246, 48) : QColor(37, 99, 235, 30);
        const QColor inlineHoverBg = c.dark ? QColor(148, 163, 184, 36) : QColor(15, 23, 42, 18);
        const QPoint mousePos = opt.widget != nullptr
            ? opt.widget->mapFromGlobal(QCursor::pos())
            : QPoint(-1, -1);
        drawInlineButton(painter, visibleRect, visible, visibleRect.contains(mousePos),
                         iconColor, activeBg, inlineHoverBg,
                         [visible](QPainter* p, const QRect& r, const QColor& color) {
                             drawEyeIcon(p, r, color, visible);
                         });
        drawInlineButton(painter, lockRect, locked, lockRect.contains(mousePos),
                         locked ? c.accent : c.textMuted, activeBg, inlineHoverBg,
                         [locked](QPainter* p, const QRect& r, const QColor& color) {
                             drawLockIcon(p, r, color, locked);
                         });

        QRect thumbRect(lockRect.right() + 10,
                        content.top() + (content.height() - kLayerThumbSize) / 2,
                        kLayerThumbSize,
                        kLayerThumbSize);
        painter->setPen(QPen(c.border, 1.0));
        painter->setBrush(c.dark ? QColor("#111827") : QColor("#F8FAFC"));
        painter->drawRoundedRect(thumbRect, 5.0, 5.0);
        if (!thumbnail.isNull()) {
            QPainterPath clip;
            clip.addRoundedRect(thumbRect.adjusted(1, 1, -1, -1), 4.0, 4.0);
            painter->setClipPath(clip);
            const QImage scaled = thumbnail.scaled(thumbRect.size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            const QPoint imageTopLeft(thumbRect.center().x() - scaled.width() / 2,
                                      thumbRect.center().y() - scaled.height() / 2);
            painter->setOpacity(visible ? 1.0 : 0.45);
            painter->drawImage(imageTopLeft, scaled);
            painter->setOpacity(1.0);
            painter->setClipping(false);
        }

        const QRect textRect(thumbRect.right() + 10,
                             content.top() + 8,
                             qMax(0, content.right() - thumbRect.right() - 18),
                             content.height() - 16);
        QFont titleFont = opt.font;
        titleFont.setWeight(QFont::DemiBold);
        painter->setPen(visible ? c.textPrimary : c.textMuted);
        painter->setFont(titleFont);
        const QFontMetrics titleFm(titleFont);
        const QString elidedTitle = titleFm.elidedText(title, Qt::ElideRight, textRect.width());
        painter->drawText(QRect(textRect.left(), textRect.top(), textRect.width(), titleFm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, elidedTitle);

        QFont subtitleFont = opt.font;
        subtitleFont.setPointSizeF(qMax(8.0, subtitleFont.pointSizeF() - 1.0));
        painter->setFont(subtitleFont);
        painter->setPen(c.textMuted);
        const QFontMetrics subFm(subtitleFont);
        const QString elidedSubtitle = subFm.elidedText(subtitle, Qt::ElideRight, textRect.width());
        painter->drawText(QRect(textRect.left(), textRect.top() + titleFm.height() + 4,
                                textRect.width(), subFm.height()),
                          Qt::AlignLeft | Qt::AlignVCenter, elidedSubtitle);

        if (opt.state.testFlag(QStyle::State_HasFocus)) {
            QStyleOptionFocusRect focus;
            focus.QStyleOption::operator=(opt);
            focus.rect = content.adjusted(2, 2, -2, -2);
            focus.backgroundColor = selectedBg;
            const QWidget* widget = opt.widget;
            QStyle* style = widget != nullptr ? widget->style() : QApplication::style();
            style->drawPrimitive(QStyle::PE_FrameFocusRect, &focus, painter, widget);
        }
        painter->restore();
    }
};

}  // namespace

CoverLayerListPanel::CoverLayerListPanel(CoverStudioPanel* studio, QWidget* parent)
    : QWidget(parent)
    , studio_(studio)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    auto* title = new QLabel(UiText::text(QStringLiteral("cover.layers")), this);
    applyPanelTitleStyle(title);
    layout->addWidget(title);

    model_ = new CoverLayerListModel(this);
    if (studio_ != nullptr) {
        model_->setLayoutModel(studio_->layoutModel());
    }

    view_ = new QListView(this);
    view_->setModel(model_);
    view_->setItemDelegate(new CoverLayerItemDelegate(view_));
    view_->setSelectionMode(QAbstractItemView::SingleSelection);
    view_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    view_->setMouseTracking(true);
    view_->viewport()->setMouseTracking(true);
    const UiTheme::Colors& colors = UiTheme::colors();
    view_->setStyleSheet(QStringLiteral(
        "QListView { background: %1; color: %2; border: 1px solid %3; padding: 4px; outline: none; }"
        "QListView::item { background: transparent; border: none; }"
        "QListView::item:selected { background: transparent; color: %2; }"
        "QListView::item:hover { background: transparent; }")
        .arg(colors.cardBg.name(QColor::HexRgb),
             colors.textPrimary.name(QColor::HexRgb),
             colors.border.name(QColor::HexRgb)));
    miacode::ui::applyScrollBarStyle(view_);
    view_->setContextMenuPolicy(Qt::CustomContextMenu);
    // Drag rows to change z-order (the model maps view rows → z, §12.12).
    view_->setDragEnabled(true);
    view_->setAcceptDrops(true);
    view_->setDropIndicatorShown(true);
    view_->setDragDropMode(QAbstractItemView::InternalMove);
    view_->setDefaultDropAction(Qt::MoveAction);
    view_->installEventFilter(this);
    view_->viewport()->installEventFilter(this);
    layout->addWidget(view_, 1);

    // Simplified to two buttons (诉求 8 / R3): add + delete. Z-order (上移/下移/置顶/
    // 置底) and show/lock move to the right-click context menu + keyboard; duplicate
    // is removed entirely.
    const QString compactButton = UiTheme::dialogPushButtonStyleSheet()
        + QStringLiteral("QPushButton { min-width: 0px; min-height: 28px; padding: 0 10px; border-radius: 7px; }");
    auto* row = new QHBoxLayout();
    row->setSpacing(6);
    auto* addButton = new QPushButton(UiText::text(QStringLiteral("cover.add_frame")), this);
    auto* removeButton = new QPushButton(UiText::text(QStringLiteral("metadata.delete")), this);
    addButton->setStyleSheet(compactButton);
    removeButton->setStyleSheet(compactButton);
    addButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    removeButton->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    addButton->setToolTip(UiText::text(QStringLiteral("cover.add_a_chart_frame_a")));
    removeButton->setToolTip(UiText::text(QStringLiteral("cover.delete_the_selected_layer_delete")));
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
        view_->selectionModel()->clearSelection();
        view_->selectionModel()->setCurrentIndex(QModelIndex(), QItemSelectionModel::NoUpdate);
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
    if (watched == view_->viewport()) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                const QModelIndex index = view_->indexAt(mouseEvent->pos());
                if (index.isValid()) {
                    QStyleOptionViewItem option;
                    option.rect = view_->visualRect(index);
                    pressedInlineControl_ = inlineControlAt(option, mouseEvent->pos());
                    if (pressedInlineControl_ != NoInlineControl) {
                        view_->viewport()->update(option.rect);
                        return true;
                    }
                }
            }
            pressedInlineControl_ = NoInlineControl;
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::LeftButton && pressedInlineControl_ != NoInlineControl) {
                const int pressedControl = pressedInlineControl_;
                pressedInlineControl_ = NoInlineControl;
                const QModelIndex index = view_->indexAt(mouseEvent->pos());
                if (!index.isValid() || studio_ == nullptr || model_ == nullptr) {
                    return true;
                }
                QStyleOptionViewItem option;
                option.rect = view_->visualRect(index);
                if (inlineControlAt(option, mouseEvent->pos()) != pressedControl) {
                    view_->viewport()->update(option.rect);
                    return true;
                }
                const QString key = model_->data(index, CoverLayerListModel::KeyRole).toString();
                if (pressedControl == VisibilityInlineControl) {
                    studio_->setLayerVisible(key, !model_->data(index, CoverLayerListModel::VisibleRole).toBool());
                } else if (pressedControl == LockInlineControl) {
                    studio_->setLayerLocked(key, !model_->data(index, CoverLayerListModel::LockedRole).toBool());
                }
                view_->viewport()->update(option.rect);
                return true;
            }
            pressedInlineControl_ = NoInlineControl;
        } else if (event->type() == QEvent::MouseMove) {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QModelIndex index = view_->indexAt(mouseEvent->pos());
            bool overInlineControl = false;
            QString tooltip;
            if (index.isValid()) {
                QStyleOptionViewItem option;
                option.rect = view_->visualRect(index);
                const InlineLayerControl control = inlineControlAt(option, mouseEvent->pos());
                overInlineControl = control != NoInlineControl;
                if (control == VisibilityInlineControl) {
                    const bool visible = model_->data(index, CoverLayerListModel::VisibleRole).toBool();
                    tooltip = visible
                        ? UiText::text(QStringLiteral("cover.hide_layer_v"))
                        : UiText::text(QStringLiteral("cover.show_layer_v"));
                } else if (control == LockInlineControl) {
                    const bool locked = model_->data(index, CoverLayerListModel::LockedRole).toBool();
                    tooltip = locked
                        ? UiText::text(QStringLiteral("cover.unlock_geometry_l"))
                        : UiText::text(QStringLiteral("cover.lock_geometry_l"));
                }
            }
            view_->viewport()->setCursor(overInlineControl ? Qt::PointingHandCursor : Qt::ArrowCursor);
            if (overInlineControl) {
                view_->viewport()->setToolTip(tooltip);
            } else {
                view_->viewport()->setToolTip(QString());
            }
        } else if (event->type() == QEvent::Leave) {
            pressedInlineControl_ = NoInlineControl;
            view_->viewport()->unsetCursor();
            view_->viewport()->setToolTip(QString());
        }
    }
    if ((watched == view_ || watched == view_->viewport()) && model_ != nullptr) {
        if (event->type() == QEvent::DragMove || event->type() == QEvent::Drop) {
            auto* dropEvent = static_cast<QDropEvent*>(event);
            const QPoint pos = dropEvent->position().toPoint();
            const int y = watched == view_->viewport() ? pos.y() : view_->viewport()->mapFrom(view_, pos).y();
            if (y < 0) {
                model_->setNextDropRowOverride(0);
            } else if (y > view_->viewport()->height()) {
                model_->setNextDropRowOverride(model_->rowCount());
            } else {
                model_->setNextDropRowOverride(-1);
            }
        } else if (event->type() == QEvent::DragLeave) {
            model_->setNextDropRowOverride(-1);
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
        ? UiText::text(QStringLiteral("cover.hide"))
        : UiText::text(QStringLiteral("cover.show")));
    QAction* lockAction = menu.addAction(layer->locked()
        ? UiText::text(QStringLiteral("cover.unlock"))
        : UiText::text(QStringLiteral("cover.lock")));
    menu.addSeparator();
    QAction* upAction = menu.addAction(UiText::text(QStringLiteral("cover.move_up")));
    QAction* downAction = menu.addAction(UiText::text(QStringLiteral("cover.move_down")));
    QAction* topAction = menu.addAction(UiText::text(QStringLiteral("cover.bring_to_front")));
    QAction* bottomAction = menu.addAction(UiText::text(QStringLiteral("cover.send_to_back")));
    menu.addSeparator();
    QAction* deleteAction = menu.addAction(UiText::text(QStringLiteral("metadata.delete")));
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
