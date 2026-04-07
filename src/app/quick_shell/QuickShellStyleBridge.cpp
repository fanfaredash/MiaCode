#include "QuickShellStyleBridge.h"

#include "mainwindow/MainWindow.h"
#include "UiTheme.h"
#include "ui/WindowParityMetrics.h"

#include <QApplication>
#include <QMenuBar>
#include <QScreen>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>

namespace {

QVariantMap buildPaletteMap()
{
    const UiTheme::Colors& c = UiTheme::colors();
    return QVariantMap{
        {QStringLiteral("dark"), c.dark},
        {QStringLiteral("windowBg"), c.windowBg},
        {QStringLiteral("windowAltBg"), c.windowAltBg},
        {QStringLiteral("toolbarBg"), c.toolbarBg},
        {QStringLiteral("statusBg"), c.statusBg},
        {QStringLiteral("panelBg"), c.panelBg},
        {QStringLiteral("cardBg"), c.cardBg},
        {QStringLiteral("cardAltBg"), c.cardAltBg},
        {QStringLiteral("inputBg"), c.inputBg},
        {QStringLiteral("inputDisabledBg"), c.inputDisabledBg},
        {QStringLiteral("canvasBg"), c.canvasBg},
        {QStringLiteral("textPrimary"), c.textPrimary},
        {QStringLiteral("textSecondary"), c.textSecondary},
        {QStringLiteral("textMuted"), c.textMuted},
        {QStringLiteral("border"), c.border},
        {QStringLiteral("borderSoft"), c.borderSoft},
        {QStringLiteral("accent"), c.accent},
        {QStringLiteral("accentHover"), c.accentHover},
        {QStringLiteral("accentPressed"), c.accentPressed},
        {QStringLiteral("accentText"), c.accentText},
        {QStringLiteral("menuHoverBg"), c.menuHoverBg},
        {QStringLiteral("iconPrimary"), c.iconPrimary},
    };
}

}  // namespace

QuickShellStyleBridge::QuickShellStyleBridge(MainWindow* backend, QObject* parent)
    : QObject(parent)
    , backend_(backend)
    , refreshTimer_(new QTimer(this))
{
    refreshTimer_->setInterval(250);
    connect(refreshTimer_, &QTimer::timeout, this, &QuickShellStyleBridge::refreshFromBackend);
    refreshTimer_->start();
    refreshFromBackend();
}

QVariantMap QuickShellStyleBridge::palette() const
{
    return palette_;
}

QVariantMap QuickShellStyleBridge::metrics() const
{
    return metrics_;
}

void QuickShellStyleBridge::syncWindowSize(int width, int height)
{
    if (backend_ == nullptr) {
        return;
    }
    const QSize nextSize(qMax(width, 1), qMax(height, 1));
    if (backend_->size() != nextSize) {
        backend_->resize(nextSize);
        backend_->refreshLayoutAfterPageSwitch();
    }
    refreshFromBackend();
}

void QuickShellStyleBridge::refreshNow()
{
    refreshFromBackend();
}

void QuickShellStyleBridge::refreshFromBackend()
{
    const QVariantMap nextPalette = buildPaletteMap();
    if (nextPalette != palette_) {
        palette_ = nextPalette;
        emit appearanceChanged();
    }

    int initialWindowWidth = miacode::window_parity::kInitialWindowWidth;
    int initialWindowHeight = miacode::window_parity::kInitialWindowHeight;
    if (QScreen* screen = QApplication::primaryScreen(); screen != nullptr) {
        const QRect workArea = screen->availableGeometry();
        initialWindowWidth = qMin(
            miacode::window_parity::kInitialWindowWidth,
            qMax(miacode::window_parity::kInitialWindowFloorWidth, workArea.width() - 120)
        );
        initialWindowHeight = qMin(
            miacode::window_parity::kInitialWindowHeight,
            qMax(miacode::window_parity::kInitialWindowFloorHeight, workArea.height() - 120)
        );
    }

    QVariantMap nextMetrics{
        {QStringLiteral("initialWindowWidth"), initialWindowWidth},
        {QStringLiteral("initialWindowHeight"), initialWindowHeight},
        {QStringLiteral("minimumWindowWidth"), miacode::window_parity::kInitialWindowFloorWidth},
        {QStringLiteral("minimumWindowHeight"), miacode::window_parity::kInitialWindowFloorHeight},
        {QStringLiteral("topChromeHeight"), 78},
        {QStringLiteral("statusHeight"), 28},
        {QStringLiteral("previewShellWidth"), 360},
        {QStringLiteral("previewControlsHeight"), 200},
        {QStringLiteral("previewSpeedButtonWidth"), miacode::window_parity::kPreviewSpeedButtonWidth},
        {QStringLiteral("previewControlButtonMinHeight"), miacode::window_parity::kPreviewControlButtonMinHeight},
        {QStringLiteral("fullscreenHintTopMargin"), miacode::window_parity::kPreviewFullscreenHintTopMargin},
        {QStringLiteral("fullscreenOverlaySideMargin"), miacode::window_parity::kPreviewFullscreenOverlaySideMargin},
        {QStringLiteral("fullscreenOverlayBottomMargin"), miacode::window_parity::kPreviewFullscreenOverlayBottomMargin},
        {QStringLiteral("fullscreenOverlayMaxWidth"), miacode::window_parity::kPreviewFullscreenOverlayMaxWidth},
        {QStringLiteral("fullscreenOverlayHideOffset"), miacode::window_parity::kPreviewFullscreenOverlayHideOffset},
        {QStringLiteral("fullscreenControlsRevealHotzoneHeight"), miacode::window_parity::kPreviewFullscreenControlsRevealHotzoneHeight},
        {QStringLiteral("fullscreenControlsAutoHideDelayMs"), miacode::window_parity::kPreviewFullscreenControlsAutoHideDelayMs},
        {QStringLiteral("fullscreenHintAutoHideDelayMs"), miacode::window_parity::kPreviewFullscreenHintAutoHideDelayMs},
    };

    if (backend_ != nullptr) {
        const int menuHeight =
            backend_->menuBar() != nullptr ? qMax(24, backend_->menuBar()->sizeHint().height()) : 30;
        QToolBar* mainToolBar = backend_->findChild<QToolBar*>();
        const int toolBarHeight =
            mainToolBar != nullptr ? qMax(28, mainToolBar->sizeHint().height()) : 32;
        const int statusHeight =
            backend_->statusBar() != nullptr ? qMax(24, backend_->statusBar()->sizeHint().height()) : 24;
        nextMetrics.insert(QStringLiteral("topChromeHeight"), menuHeight + toolBarHeight + 2);
        nextMetrics.insert(QStringLiteral("statusHeight"), statusHeight);
    }

    if (backend_ != nullptr
        && backend_->previewControlCard_ != nullptr
        && backend_->previewStatsCard_ != nullptr) {
        nextMetrics.insert(
            QStringLiteral("previewControlsHeight"),
            qMax(
                180,
                backend_->previewControlCard_->sizeHint().height()
                    + backend_->previewStatsCard_->sizeHint().height()
                    + 22
            )
        );
    }

    if (backend_ != nullptr) {
        const int availableWidth = qMax(0, backend_->width());
        const int availableHeight = qMax(
            0,
            backend_->height()
                - nextMetrics.value(QStringLiteral("topChromeHeight")).toInt()
                - nextMetrics.value(QStringLiteral("statusHeight")).toInt()
        );
        const int controlHeight = nextMetrics.value(QStringLiteral("previewControlsHeight")).toInt();
        const int minimumStatsHeight = miacode::window_parity::computePreviewStatsLayout(320).minCardHeight;
        const int shellWidth = miacode::window_parity::computePreviewPanelTargetWidth(
            availableWidth,
            availableHeight,
            320,
            controlHeight,
            minimumStatsHeight,
            backend_->normalizedPreviewCanvasAspectRatio(backend_->previewCanvasAspectRatio_)
        );
        nextMetrics.insert(
            QStringLiteral("previewShellWidth"),
            qBound(
                miacode::window_parity::kEmbeddedPreviewPanelMinWidth,
                shellWidth,
                miacode::window_parity::kEmbeddedPreviewPanelWidthMax
            )
        );
    }

    if (nextMetrics != metrics_) {
        metrics_ = nextMetrics;
        emit metricsChanged();
    }
}
