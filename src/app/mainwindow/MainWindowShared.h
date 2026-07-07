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

#include "SimaiNativeParser.h"
#include "UiText.h"
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
// The parser validation locale matching the session UI language.
SimaiNativeValidationLocale uiValidationLocale();
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
// "add", "difficulty_chart", "bookmark", "export", "toolbox", "spacer".
inline constexpr int kOutlineItemKindRole = Qt::UserRole;
inline constexpr int kOutlineItemDifficultyRole = Qt::UserRole + 1;
inline constexpr int kOutlineItemLineRole = Qt::UserRole + 2;
// difficulty_chart rows: bool — bookmark chevron/fold state.
inline constexpr int kOutlineItemExpandedRole = Qt::UserRole + 3;
// metadata/export/difficulty_chart rows: bool — persistent "you are here"
// marker (accent edge bar + fill), driven by app state instead of transient
// list selection. Bookmark rows intentionally do not use a persistent marker.
inline constexpr int kOutlineItemActiveRole = Qt::UserRole + 4;
// difficulty_chart rows: int — number of derived comment bookmarks.
inline constexpr int kOutlineItemBookmarkCountRole = Qt::UserRole + 5;
// bookmark rows: int — largest bookmark line in the same difficulty group;
// sizes the line badge uniformly so bookmark names align vertically.
inline constexpr int kOutlineItemMaxLineRole = Qt::UserRole + 6;

class OutlineItemDelegate : public QStyledItemDelegate {
public:
    explicit OutlineItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    // Left inset of the bookmark line badge (third tree level).
    static constexpr int kBookmarkRowIndent = 44;
    static constexpr int kBookmarkGroupIndent = 8;
    static constexpr int kIconOnlyThreshold = 120;
    // Row-start column vacated on difficulty rows for the fold chevron.
    static constexpr int kDifficultyChevronColumn = 14;
    // X of the chevron glyph center; the bookmark indent guide shares it.
    static constexpr int kDifficultyFoldGlyphX = 13;
    // Click zone (from the row's left edge) that toggles the fold state.
    static constexpr int kDifficultyFoldHitZone = 24;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        const QString kind = index.data(kOutlineItemKindRole).toString();
        if (kind == QLatin1String("spacer")) {
            // Keep the item's own tiny size hint — the generic minimum below
            // would inflate the 4px section gap to a full row.
            size.setHeight(4);
        } else if (kind == QLatin1String("bookmark")) {
            size.setHeight(24);
        } else if (kind == QLatin1String("difficulty_chart")) {
            size.setHeight(30);
        } else {
            size.setHeight(qMax(size.height(), 28));
        }
        return size;
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const QString kind = index.data(kOutlineItemKindRole).toString();
        if (kind == QLatin1String("spacer")) {
            return;  // pure visual gap between sidebar sections
        }
        if (kind == QLatin1String("bookmark")) {
            paintRowFill(painter, option, index);
            paintBookmarkRow(painter, option, index);
            return;
        }

        QStyleOptionViewItem drawOption(option);
        initStyleOption(&drawOption, index);
        const UiTheme::Colors& colors = UiTheme::colors();
        paintRowFill(painter, option, index);

        const int listWidth = option.widget != nullptr ? option.widget->width() : option.rect.width();
        const bool iconOnly = listWidth > 0 && listWidth < kIconOnlyThreshold;
        const bool isDifficulty = kind == QLatin1String("difficulty_chart");
        if (iconOnly) {
            drawOption.text.clear();
            drawOption.features &= ~QStyleOptionViewItem::HasDisplay;
            drawOption.decorationAlignment = Qt::AlignLeft | Qt::AlignVCenter;
        } else if (isDifficulty) {
            // Vacate the fold-chevron column at the row start (IDE-tree
            // layout); the chevron itself is painted after the styled body.
            drawOption.rect.adjust(kDifficultyChevronColumn, 0, 0, 0);
        }

        drawOption.state &= ~QStyle::State_Selected;
        drawOption.state &= ~QStyle::State_MouseOver;
        drawOption.backgroundBrush = Qt::NoBrush;
        drawOption.palette.setColor(QPalette::HighlightedText, colors.textPrimary);
        QStyledItemDelegate::paint(painter, drawOption, index);
        if (isDifficulty && !iconOnly) {
            paintDifficultyFoldChevron(painter, option, index);
        }
    }

    QWidget* createEditor(QWidget* parent, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QWidget* editor = QStyledItemDelegate::createEditor(parent, option, index);
        if (editor != nullptr && index.data(kOutlineItemKindRole).toString() == QLatin1String("bookmark")) {
            if (auto* lineEdit = qobject_cast<QLineEdit*>(editor)) {
                QFont nameFont = option.font;
                nameFont.setPointSize(qMax(8, option.font.pointSize() - 2));
                lineEdit->setFont(nameFont);
                lineEdit->setFrame(false);
                lineEdit->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
                lineEdit->setContentsMargins(0, 0, 0, 0);
                lineEdit->setTextMargins(0, 0, 0, 0);
                const QColor bg = bookmarkEditorBackgroundColor(option);
                const QColor fg = UiTheme::colors().textSecondary;
                lineEdit->setStyleSheet(QStringLiteral(
                    "QLineEdit { background: %1; color: %2; border: none; padding: 0px;"
                    " selection-background-color: %3; selection-color: %4; }")
                    .arg(bg.name(QColor::HexRgb),
                         fg.name(QColor::HexRgb),
                         UiTheme::colors().accent.name(QColor::HexRgb),
                         UiTheme::colors().accentText.name(QColor::HexRgb)));
            }
        }
        return editor;
    }

    // Inline rename: the editor covers the name area of a bookmark row (after
    // the indent + line badge) so the typed text lines up with the display.
    void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        if (editor != nullptr && index.data(kOutlineItemKindRole).toString() == QLatin1String("bookmark")) {
            QRect rect = bookmarkNameRect(option, index).adjusted(0, 0, -2, 0);
            if (rect.width() < 60) {
                rect.setLeft(qMax(option.rect.left() + 2, option.rect.right() - 60));
            }
            editor->setGeometry(rect);
            return;
        }
        QStyledItemDelegate::updateEditorGeometry(editor, option, index);
    }

private:
    // One selection language for every level: the persistent "you are here"
    // row gets a borderless fill plus a 3px accent bar on its left edge;
    // hovering any other row shows a weaker flat fill. The QListWidget
    // selection itself is deliberately NOT painted — "current" is expressed
    // by the active marker (pages/difficulties) or not at all (bookmark jumps
    // are one-shot actions).
    void paintRowFill(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        const UiTheme::Colors& colors = UiTheme::colors();
        const QColor activeFill = colors.dark ? QColor("#314158") : QColor("#E7F0FD");
        const QColor weakFill = colors.dark ? QColor("#2A3442") : QColor("#F3F7FD");
        const bool isBookmark = index.data(kOutlineItemKindRole).toString() == QLatin1String("bookmark");
        const bool activeMarker = index.data(kOutlineItemActiveRole).toBool() && !isBookmark;

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(Qt::NoPen);
        QRect fillRect = option.rect.adjusted(1, 1, -1, -1);
        if (isBookmark) {
            // Keep the hover fill clear of the tree indent guide.
            fillRect.setLeft(option.rect.left() + kDifficultyFoldGlyphX + 5);
        }
        if (activeMarker) {
            QColor fill = activeFill;
            if (option.state.testFlag(QStyle::State_MouseOver)) {
                fill = colors.dark ? activeFill.lighter(115) : QColor("#DCE9FB");
            }
            painter->setBrush(fill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
            painter->setBrush(colors.accent);
            const QRect barRect(fillRect.left(), fillRect.top() + 5, 3, fillRect.height() - 10);
            painter->drawRoundedRect(barRect, 1.5, 1.5);
        } else if (option.state.testFlag(QStyle::State_MouseOver)) {
            painter->setBrush(weakFill);
            painter->drawRoundedRect(fillRect, 6.0, 6.0);
        }
        painter->restore();
    }

    QFont badgeFont(const QStyleOptionViewItem& option) const
    {
        QFont font = option.font;
        font.setPointSize(qMax(6, font.pointSize() - 3));
        return font;
    }

    int badgeWidth(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        const QFontMetrics metrics(badgeFont(option));
        // Uniform width across the difficulty group (sized to its largest
        // line number) so the bookmark names align vertically.
        const int maxLine = qMax(qMax(1, index.data(kOutlineItemLineRole).toInt()),
                                 index.data(kOutlineItemMaxLineRole).toInt());
        return qMax(18, metrics.horizontalAdvance(QString::number(maxLine)) + 8);
    }

    QRect bookmarkBadgeRect(const QStyleOptionViewItem& option, const QModelIndex& index, bool iconOnly) const
    {
        const int badgeW = badgeWidth(option, index);
        const int badgeH = qMin(option.rect.height() - 8, 15);
        return QRect(
            option.rect.left() + (iconOnly ? kBookmarkGroupIndent : kBookmarkRowIndent),
            option.rect.top() + (option.rect.height() - badgeH) / 2,
            badgeW,
            badgeH);
    }

    QRect bookmarkNameRect(const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        const int listWidth = option.widget != nullptr ? option.widget->width() : option.rect.width();
        const bool iconOnly = listWidth > 0 && listWidth < kIconOnlyThreshold;
        const QRect badgeRect = bookmarkBadgeRect(option, index, iconOnly);
        return QRect(
            badgeRect.right() + 7,
            option.rect.top(),
            option.rect.right() - badgeRect.right() - 12,
            option.rect.height());
    }

    QColor bookmarkEditorBackgroundColor(const QStyleOptionViewItem& option) const
    {
        // Selection is not painted anymore, so the editor sits on the plain
        // list background.
        Q_UNUSED(option);
        return UiTheme::colors().dark ? QColor("#1E2733") : QColor("#FFFFFF");
    }

    void paintBookmarkRow(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        const UiTheme::Colors& colors = UiTheme::colors();
        const int listWidth = option.widget != nullptr ? option.widget->width() : option.rect.width();
        const bool iconOnly = listWidth > 0 && listWidth < kIconOnlyThreshold;

        painter->save();

        // Tree indent guide: a 1px segment per row under the parent
        // difficulty's fold-chevron column; each segment overshoots the row
        // by 1px so the list's 2px item spacing doesn't break the line.
        // Drawn before the AA hint so it stays a crisp hairline.
        if (!iconOnly) {
            QColor guide = colors.textSecondary;
            guide.setAlpha(colors.dark ? 48 : 58);
            painter->fillRect(
                QRect(option.rect.left() + kDifficultyFoldGlyphX, option.rect.top() - 1, 1, option.rect.height() + 2),
                guide);
        }

        painter->setRenderHint(QPainter::Antialiasing, true);

        // Line-number badge: neutral and compact; bookmark jumps are one-shot
        // actions, so no selected/pressed treatment lingers on the row.
        const QRect badgeRect = bookmarkBadgeRect(option, index, iconOnly);
        QColor badgeBg = colors.textSecondary;
        badgeBg.setAlpha(colors.dark ? 40 : 34);
        const QColor badgeFg = colors.textSecondary;
        painter->setPen(Qt::NoPen);
        painter->setBrush(badgeBg);
        painter->drawRoundedRect(badgeRect, 3.0, 3.0);
        painter->setFont(badgeFont(option));
        painter->setPen(badgeFg);
        painter->drawText(badgeRect, Qt::AlignCenter,
                          QString::number(qMax(1, index.data(kOutlineItemLineRole).toInt())));

        // Bookmark name, elided to the remaining width (full name in tooltip).
        if (!iconOnly) {
            const QRect nameRect = bookmarkNameRect(option, index);
            if (nameRect.width() > 8) {
                QFont nameFont = option.font;
                nameFont.setPointSize(qMax(8, option.font.pointSize() - 2));
                painter->setFont(nameFont);
                painter->setPen(colors.textSecondary);
                const QFontMetrics metrics(nameFont);
                painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter,
                                  metrics.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, nameRect.width()));
            }
        }
        painter->restore();
    }

    // Row-start fold chevron (IDE-tree convention). Difficulties without
    // bookmarks skip the glyph but keep the vacated column, so the badge
    // icons of sibling difficulty rows stay aligned.
    void paintDifficultyFoldChevron(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const
    {
        if (index.data(kOutlineItemBookmarkCountRole).toInt() <= 0) {
            return;
        }
        const UiTheme::Colors& colors = UiTheme::colors();
        const bool expanded = index.data(kOutlineItemExpandedRole).toBool();

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        QPen pen(colors.textSecondary, 1.6);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);
        const QPointF center(option.rect.left() + kDifficultyFoldGlyphX + 0.5,
                             option.rect.top() + option.rect.height() / 2.0);
        QPolygonF chevron;
        if (expanded) {
            chevron << QPointF(center.x() - 3.5, center.y() - 1.75)
                    << QPointF(center.x(), center.y() + 1.75)
                    << QPointF(center.x() + 3.5, center.y() - 1.75);
        } else {
            chevron << QPointF(center.x() - 1.75, center.y() - 3.5)
                    << QPointF(center.x() + 1.75, center.y())
                    << QPointF(center.x() - 1.75, center.y() + 3.5);
        }
        painter->drawPolyline(chevron);
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
