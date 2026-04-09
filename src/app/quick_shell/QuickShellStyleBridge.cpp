#include "QuickShellStyleBridge.h"

#include "mainwindow/MainWindow.h"
#include "UiTheme.h"
#include "ui/WindowParityMetrics.h"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLabel>
#include <QMenuBar>
#include <QMetaObject>
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
        {QStringLiteral("menuBg"), c.menuBg},
        {QStringLiteral("menuBorder"), c.menuBorder},
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
    if (backend_ != nullptr) {
        backend_->installEventFilter(this);
    }

    refreshTimer_->setInterval(1000);
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

void QuickShellStyleBridge::scheduleRefresh()
{
    if (refreshScheduled_ || refreshInProgress_) {
        return;
    }
    refreshScheduled_ = true;
    QMetaObject::invokeMethod(this, [this]() {
        refreshScheduled_ = false;
        refreshFromBackend();
    }, Qt::QueuedConnection);
}

bool QuickShellStyleBridge::eventFilter(QObject* watched, QEvent* event)
{
    Q_UNUSED(watched);

    if (event == nullptr) {
        return QObject::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::Resize:
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::LayoutRequest:
    case QEvent::PaletteChange:
    case QEvent::ApplicationPaletteChange:
    case QEvent::StyleChange:
    case QEvent::ThemeChange:
    case QEvent::FontChange:
    case QEvent::ActivationChange:
    case QEvent::WindowStateChange:
        scheduleRefresh();
        break;
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

void QuickShellStyleBridge::refreshFromBackend()
{
    if (refreshInProgress_) {
        return;
    }
    refreshInProgress_ = true;

    const QVariantMap nextPalette = buildPaletteMap();
    if (nextPalette != palette_) {
        palette_ = nextPalette;
        emit appearanceChanged();
    }

    int initialWindowWidth = miacode::window_parity::kInitialWindowWidth;
    int initialWindowHeight = miacode::window_parity::kInitialWindowHeight;
    int initialWindowX = 120;
    int initialWindowY = 120;
    int previewPanelMinWidth = miacode::window_parity::kEmbeddedPreviewPanelMinWidth;
    int leftColumnMinWidth = 320;
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
        initialWindowX = workArea.left() + qMax(0, (workArea.width() - initialWindowWidth) / 2);
        initialWindowY = workArea.top() + qMax(0, (workArea.height() - initialWindowHeight) / 2);
    }

    QVariantMap nextMetrics{
        {QStringLiteral("initialWindowWidth"), initialWindowWidth},
        {QStringLiteral("initialWindowHeight"), initialWindowHeight},
        {QStringLiteral("initialWindowX"), initialWindowX},
        {QStringLiteral("initialWindowY"), initialWindowY},
        {QStringLiteral("minimumWindowWidth"), miacode::window_parity::kInitialWindowFloorWidth},
        {QStringLiteral("minimumWindowHeight"), miacode::window_parity::kInitialWindowFloorHeight},
        {QStringLiteral("leftColumnMinWidth"), leftColumnMinWidth},
        {QStringLiteral("outlineDockWidth"), miacode::window_parity::kOutlineExpandedDefaultWidth},
        {QStringLiteral("topChromeHeight"), 78},
        {QStringLiteral("statusHeight"), 28},
        {QStringLiteral("previewPanelMinWidth"), previewPanelMinWidth},
        {QStringLiteral("previewPanelMaxWidth"), miacode::window_parity::kEmbeddedPreviewPanelWidthMax},
        {QStringLiteral("previewSplitterHandleWidth"), 6},
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
        const int outlineWidth =
            backend_->outlineDock_ != nullptr
                ? backend_->outlineDock_->width()
                : (backend_->outlineDockCollapsed_
                    ? miacode::window_parity::kOutlineCollapsedWidth
                    : qMax(
                        miacode::window_parity::kOutlineExpandedMinWidth,
                        backend_->outlineDockExpandedWidth_
                    ));
        nextMetrics.insert(QStringLiteral("outlineDockWidth"), qMax(0, outlineWidth));
        int stableLeftColumnMinWidth = 0;
        stableLeftColumnMinWidth += qMax(0, outlineWidth);
        if (backend_->previewLeftColumn_ != nullptr) {
            stableLeftColumnMinWidth += qMax(0, backend_->previewLeftColumn_->minimumWidth());
        }
        leftColumnMinWidth = qMax(leftColumnMinWidth, stableLeftColumnMinWidth);
        nextMetrics.insert(QStringLiteral("leftColumnMinWidth"), leftColumnMinWidth);

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

        if (backend_->previewTotalStatsLabel_ != nullptr && backend_->previewStatsGridLayout_ != nullptr) {
            const QFontMetrics chipMetrics(backend_->previewTotalStatsLabel_->font());
            const int templateChipWidth =
                chipMetrics.horizontalAdvance(QStringLiteral("Total  xxxxx/xxxxx")) + 18;
            const int horizontalSpacing = qMax(0, backend_->previewStatsGridLayout_->horizontalSpacing());
            const QMargins gridMargins = backend_->previewStatsGridLayout_->contentsMargins();
            const int statsHostWidthMin =
                templateChipWidth * miacode::window_parity::kPreviewStatsNarrowLayoutCols
                + horizontalSpacing * qMax(0, miacode::window_parity::kPreviewStatsNarrowLayoutCols - 1)
                + gridMargins.left()
                + gridMargins.right();
            const int panelWidthMin =
                statsHostWidthMin
                + miacode::window_parity::kPreviewPanelMarginX * 2
                + 16;
            previewPanelMinWidth = qMax(previewPanelMinWidth, panelWidthMin);
            nextMetrics.insert(QStringLiteral("previewPanelMinWidth"), previewPanelMinWidth);
        }
    }

    nextMetrics.insert(
        QStringLiteral("minimumWindowWidth"),
        qMax(
            miacode::window_parity::kInitialWindowFloorWidth,
            leftColumnMinWidth
                + nextMetrics.value(QStringLiteral("previewSplitterHandleWidth")).toInt()
                + previewPanelMinWidth
        )
    );

    if (backend_ != nullptr) {
        const int availableWidth = qMax(0, backend_->width());
        const int availableHeight = qMax(
            0,
            backend_->height()
                - nextMetrics.value(QStringLiteral("topChromeHeight")).toInt()
                - nextMetrics.value(QStringLiteral("statusHeight")).toInt()
        );
        const int controlHeight = nextMetrics.value(QStringLiteral("previewControlsHeight")).toInt();
        const int shellWidth = miacode::window_parity::computePreviewPanelTargetWidthForAdaptiveStats(
            availableWidth,
            availableHeight,
            leftColumnMinWidth,
            controlHeight,
            backend_->normalizedPreviewCanvasAspectRatio(backend_->previewCanvasAspectRatio_)
        );
        nextMetrics.insert(
            QStringLiteral("previewShellWidth"),
            qBound(
                previewPanelMinWidth,
                shellWidth,
                miacode::window_parity::kEmbeddedPreviewPanelWidthMax
            )
        );
    }

    if (nextMetrics != metrics_) {
        metrics_ = nextMetrics;
        emit metricsChanged();
    }

    refreshInProgress_ = false;
}
