#pragma once

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QLineEdit>
#include <QPointF>
#include <QPolygonF>
#include <QList>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QPen>
#include <QString>
#include <QStyledItemDelegate>
#include <QStyle>
#include <QStyleOptionFrame>
#include <QStyleOptionViewItem>
#include <QVector>
#include <QtGlobal>

#include "UiTheme.h"
#include "WindowParityMetrics.h"

class QDialog;
class QFileInfo;
class QMenu;
class QTextEdit;
class QWidget;

namespace miacode::mainwindow::shared {

inline constexpr int kEmbeddedPreviewPanelMinWidth = miacode::window_parity::kEmbeddedPreviewPanelMinWidth;
inline constexpr int kPreviewPanelMarginX = miacode::window_parity::kPreviewPanelMarginX;
inline constexpr int kPreviewControlStatsCardMinWidth = miacode::window_parity::kPreviewControlStatsCardMinWidth;
inline constexpr int kPreviewFullscreenHintTopMargin = miacode::window_parity::kPreviewFullscreenHintTopMargin;
inline constexpr int kPreviewFullscreenOverlaySideMargin =
    miacode::window_parity::kPreviewFullscreenOverlaySideMargin;
inline constexpr int kPreviewFullscreenOverlayBottomMargin =
    miacode::window_parity::kPreviewFullscreenOverlayBottomMargin;
inline constexpr int kPreviewFullscreenOverlayMaxWidth = miacode::window_parity::kPreviewFullscreenOverlayMaxWidth;
inline constexpr int kPreviewFullscreenOverlayHideOffset =
    miacode::window_parity::kPreviewFullscreenOverlayHideOffset;
inline constexpr int kPreviewFullscreenControlsRevealHotzoneHeight =
    miacode::window_parity::kPreviewFullscreenControlsRevealHotzoneHeight;
inline constexpr int kPreviewFullscreenControlsAutoHideDelayMs =
    miacode::window_parity::kPreviewFullscreenControlsAutoHideDelayMs;
inline constexpr int kEditorTextFontSizeMin = 8;
inline constexpr int kEditorTextFontSizeMax = 28;
inline constexpr double kEditorLineSpacingFactorDefault = 3.0;
inline constexpr int kAutosaveIntervalMs = 2 * 60 * 1000;
inline constexpr int kAutosaveHistoryMaxVersions = 30;
inline constexpr int kAutosaveLatestIdleMs = 2 * 1000;
inline constexpr double kTimelineMaxUiUpdateFps = 3600.0;
// Cap interactive preview scrub updates at <= ? FPS so timeline dragging and
// preview-slider dragging do not spam seek work faster than the video path can settle.
inline constexpr int kPreviewScrubRenderIntervalMs = 67;

extern const QList<double> kEditorLineSpacingFactorOptions;

double normalizeEditorLineSpacingFactor(double factor);
QString editorLineSpacingFactorLabel(double factor);
int nearestPreviewPlaybackRateIndex(double rate);
double steppedPreviewPlaybackRate(double rate, int direction);
QString uiText(const QString& key, const QString& fallback);
void centerDialogOnAnchor(QDialog* dialog, QWidget* parent);
QByteArray autosaveContentSignature(const QString& text);
QString resolveProjectDataDirectoryPath(const QString& filePath);
void appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs);
QFont editorFont(int pointSize = -1);
QFont timelineHeaderLineNumberFont(int pointSize = -1);
int blockSpacingPixelsForPointSize(int pointSize, double spacingFactor);
void applyBlockSpacingToTextEdit(QTextEdit* editor, int blockSpacingPixels);
QFont uiOutputFont();
QFont uiAccentFont(int pointSize, QFont::Weight weight = QFont::Medium);
QFont uiMonoFont(int pointSize, QFont::Weight weight = QFont::Medium);
QString previewFullscreenControlCardStyleSheet();
QString previewFullscreenHintStyleSheet();
QString previewPlaybackRateToastStyleSheet();
QString outlineCollapseButtonStyleSheet();
QColor previewFullscreenOverlayIconColor();
QString previewFullscreenPauseButtonStyleSheet(bool active);
QIcon makeMenuSelectionCheckIcon(const QColor& color, bool visible = true);
QIcon makePreviewPlayIcon(const QColor& color);
QIcon makePreviewStopIcon(const QColor& color);
QIcon makePreviewPauseIcon(const QColor& color);
QIcon makePreviewResumeIcon(const QColor& color);
QIcon makePreviewEnterFullscreenIcon(const QColor& color);
QIcon makePreviewExitFullscreenIcon(const QColor& color);
QIcon makeDifficultyBadgeIcon(int difficultyId);
QIcon makeOutlineCloseIcon(const QColor& color);
QIcon makeSettingsGearIcon(const QColor& color);
QIcon makeToolboxAccessIcon(const QColor& toolboxColor, const QColor& gearColor);
QIcon makeMusicNoteIcon(const QColor& color);
QIcon makeExportAccessIcon(const QColor& color);
QIcon makeTransformMirrorLeftRightIcon(const QColor& color);
QIcon makeTransformMirrorUpDownIcon(const QColor& color);
QIcon makeTransformRotate180Icon(const QColor& color);
QIcon makeTransformRotateCcw45Icon(const QColor& color);
QIcon makeTransformRotateCw45Icon(const QColor& color);
QString modernScrollBarStyle();
void styleRoundedMenu(QMenu& menu);
qint64 fileLastModifiedMs(const QFileInfo& fileInfo);
double probeAudioDurationSeconds(const QString& trackPath);

// Sidebar (outlineList_) item data roles, shared by the list builder
// (DocumentSection::rebuildFieldSidebar), the click/context-menu wiring
// (FrameBootstrap) and OutlineItemDelegate below. Kinds in use: "metadata",
// "add", "difficulty_chart", "bookmark_group", "bookmark", "export", "toolbox".
inline constexpr int kOutlineItemKindRole = Qt::UserRole;
inline constexpr int kOutlineItemDifficultyRole = Qt::UserRole + 1;
inline constexpr int kOutlineItemLineRole = Qt::UserRole + 2;
inline constexpr int kOutlineItemSecondRole = Qt::UserRole + 3;
// bookmark_group rows: bool — chevron/fold state.
inline constexpr int kOutlineItemExpandedRole = Qt::UserRole + 4;
// bookmark rows: bool — draws the "last activated" accent marker.
inline constexpr int kOutlineItemActiveRole = Qt::UserRole + 5;

class OutlineItemDelegate : public QStyledItemDelegate {
public:
    explicit OutlineItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    // Left inset of second-level bookmark rows and their group header.
    static constexpr int kBookmarkRowIndent = 22;
    static constexpr int kBookmarkGroupIndent = 10;
    static constexpr int kIconOnlyThreshold = 120;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString kind = index.data(kOutlineItemKindRole).toString();
        if (kind == QLatin1String("bookmark")) {
            paintRowFill(painter, option);
            paintBookmarkRow(painter, option, index);
            return;
        }
        if (kind == QLatin1String("bookmark_group")) {
            paintRowFill(painter, option);
            paintBookmarkGroupRow(painter, option, index);
            return;
        }

        QStyleOptionViewItem drawOption(option);
        initStyleOption(&drawOption, index);
        const UiTheme::Colors& colors = UiTheme::colors();
        paintRowFill(painter, option);

        const int listWidth = option.widget != nullptr ? option.widget->width() : option.rect.width();
        const bool iconOnly = listWidth > 0 && listWidth < kIconOnlyThreshold;
        if (iconOnly) {
            drawOption.text.clear();
            drawOption.features &= ~QStyleOptionViewItem::HasDisplay;
            drawOption.decorationAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        }

        drawOption.state &= ~QStyle::State_Selected;
        drawOption.state &= ~QStyle::State_MouseOver;
        drawOption.backgroundBrush = Qt::NoBrush;
        drawOption.palette.setColor(QPalette::HighlightedText, colors.textPrimary);
        QStyledItemDelegate::paint(painter, drawOption, index);
    }

    // Inline rename: the editor covers the name area of a bookmark row (after
    // the indent + line badge) so the typed text lines up with the display.
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (editor != nullptr && index.data(kOutlineItemKindRole).toString() == QLatin1String("bookmark")) {
            QRect rect = option.rect.adjusted(kBookmarkRowIndent + badgeWidth(option, index) + 6, 1, -2, -1);
            if (rect.width() < 60) {
                rect.setLeft(qMax(option.rect.left() + 2, option.rect.right() - 60));
            }
            editor->setGeometry(rect);
            return;
        }
        QStyledItemDelegate::updateEditorGeometry(editor, option, index);
    }

private:
    void paintRowFill(QPainter* painter, const QStyleOptionViewItem& option) const
    {
        const UiTheme::Colors& colors = UiTheme::colors();
        const QColor selectedBorder = colors.dark ? QColor("#6B8BB8") : QColor("#9EC2EF");
        const QColor selectedFill = colors.dark ? QColor("#314158") : QColor("#F1F6FF");
        const QColor hoverFill = colors.dark ? QColor("#2A3442") : QColor("#F3F7FD");

        painter->save();
        const QRect fillRect = option.rect.adjusted(1, 1, -1, -1);
        if (option.state.testFlag(QStyle::State_Selected)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(QPen(selectedBorder, 1.0));
            painter->setBrush(selectedFill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        } else if (option.state.testFlag(QStyle::State_MouseOver)) {
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(hoverFill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        }
        painter->restore();
    }

    QFont badgeFont(const QStyleOptionViewItem& option) const
    {
        QFont font = option.font;
        font.setPointSize(qMax(7, font.pointSize() - 2));
        return font;
    }

    int badgeWidth(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        const QFontMetrics metrics(badgeFont(option));
        const QString lineText = QString::number(qMax(1, index.data(kOutlineItemLineRole).toInt()));
        return qMax(18, metrics.horizontalAdvance(lineText) + 10);
    }

    void paintBookmarkRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        const UiTheme::Colors& colors = UiTheme::colors();
        const bool active = index.data(kOutlineItemActiveRole).toBool();
        const int listWidth = option.widget != nullptr ? option.widget->width() : option.rect.width();
        const bool iconOnly = listWidth > 0 && listWidth < kIconOnlyThreshold;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        // Last-activated marker: a slim accent bar inside the indent gutter.
        // Deliberately different from the first-level selected-row treatment.
        if (active) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(colors.accent);
            const QRect barRect(option.rect.left() + 9, option.rect.top() + 7, 3, option.rect.height() - 14);
            painter->drawRoundedRect(barRect, 1.5, 1.5);
        }

        // Short line-number badge.
        const int badgeW = badgeWidth(option, index);
        const int badgeH = qMin(option.rect.height() - 10, 16);
        const QRect badgeRect(
            option.rect.left() + (iconOnly ? kBookmarkGroupIndent : kBookmarkRowIndent),
            option.rect.top() + (option.rect.height() - badgeH) / 2,
            badgeW,
            badgeH);
        QColor badgeBg = colors.accent;
        badgeBg.setAlpha(colors.dark ? 56 : 32);
        painter->setPen(Qt::NoPen);
        painter->setBrush(badgeBg);
        painter->drawRoundedRect(badgeRect, 4.0, 4.0);
        painter->setFont(badgeFont(option));
        painter->setPen(colors.dark ? colors.textPrimary : colors.accent.darker(120));
        painter->drawText(badgeRect, Qt::AlignCenter,
                          QString::number(qMax(1, index.data(kOutlineItemLineRole).toInt())));

        // Bookmark name, elided to the remaining width (full name in tooltip).
        if (!iconOnly) {
            const QRect nameRect(badgeRect.right() + 6, option.rect.top(),
                                 option.rect.right() - badgeRect.right() - 12, option.rect.height());
            if (nameRect.width() > 8) {
                painter->setFont(option.font);
                painter->setPen(colors.textPrimary);
                const QFontMetrics metrics(option.font);
                painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                                  metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, nameRect.width()));
            }
        }
        painter->restore();
    }

    void paintBookmarkGroupRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        const UiTheme::Colors& colors = UiTheme::colors();
        const bool expanded = index.data(kOutlineItemExpandedRole).toBool();
        const int listWidth = option.widget != nullptr ? option.widget->width() : option.rect.width();
        const bool iconOnly = listWidth > 0 && listWidth < kIconOnlyThreshold;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);

        // Fold chevron: ▾ when expanded, ▸ when collapsed.
        const int chevronSize = 7;
        const QPointF center(option.rect.left() + kBookmarkGroupIndent + chevronSize / 2.0,
                             option.rect.top() + option.rect.height() / 2.0);
        QPolygonF chevron;
        if (expanded) {
            chevron << QPointF(center.x() - chevronSize / 2.0, center.y() - chevronSize / 4.0)
                    << QPointF(center.x() + chevronSize / 2.0, center.y() - chevronSize / 4.0)
                    << QPointF(center.x(), center.y() + chevronSize / 2.0);
        } else {
            chevron << QPointF(center.x() - chevronSize / 4.0, center.y() - chevronSize / 2.0)
                    << QPointF(center.x() - chevronSize / 4.0, center.y() + chevronSize / 2.0)
                    << QPointF(center.x() + chevronSize / 2.0, center.y());
        }
        painter->setPen(Qt::NoPen);
        painter->setBrush(colors.textSecondary);
        painter->drawPolygon(chevron);

        if (!iconOnly) {
            QFont font = option.font;
            font.setPointSize(qMax(7, font.pointSize() - 1));
            painter->setFont(font);
            painter->setPen(colors.textSecondary);
            const QRect textRect(option.rect.left() + kBookmarkGroupIndent + chevronSize + 8, option.rect.top(),
                                 option.rect.width() - kBookmarkGroupIndent - chevronSize - 16, option.rect.height());
            const QFontMetrics metrics(font);
            painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter,
                              metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, textRect.width()));
        }
        painter->restore();
    }
};

class LeftPlaceholderLineEdit : public QLineEdit {
public:
    explicit LeftPlaceholderLineEdit(QWidget* parent = nullptr)
        : QLineEdit(parent)
    {}

    void setLeftPlaceholderText(const QString& text)
    {
        leftPlaceholderText_ = text;
        QLineEdit::setPlaceholderText(QString());
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QLineEdit::paintEvent(event);
        if (!text().isEmpty() || leftPlaceholderText_.isEmpty()) {
            return;
        }

        QStyleOptionFrame option;
        initStyleOption(&option);
        const QRect contentsRect = style()->subElementRect(QStyle::SE_LineEditContents, &option, this);

        QPainter painter(this);
        painter.setPen(palette().color(QPalette::PlaceholderText));
        painter.setFont(font());
        painter.drawText(contentsRect.adjusted(0, 0, -2, 0), Qt::AlignLeft | Qt::AlignVCenter, leftPlaceholderText_);
    }

private:
    QString leftPlaceholderText_;
};

}  // namespace miacode::mainwindow::shared
