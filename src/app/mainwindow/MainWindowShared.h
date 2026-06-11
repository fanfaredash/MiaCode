#pragma once

#include <QByteArray>
#include <QColor>
#include <QFont>
#include <QIcon>
#include <QLineEdit>
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

class OutlineItemDelegate : public QStyledItemDelegate {
public:
    explicit OutlineItemDelegate(QObject* parent = nullptr)
        : QStyledItemDelegate(parent)
    {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        QStyleOptionViewItem drawOption(option);
        initStyleOption(&drawOption, index);
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

        const int listWidth = option.widget != nullptr ? option.widget->width() : option.rect.width();
        constexpr int kIconOnlyThreshold = 120;
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
