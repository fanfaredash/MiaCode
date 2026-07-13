#include "QuickShellStyleBridge.h"

#include "QuickShellNativeSurfaceHost.h"
#include "UiTheme.h"
#include "ui/WindowParityMetrics.h"
#include "core/scene/PreviewHudState.h"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QFontMetrics>
#include <QGridLayout>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMetaObject>
#include <QScreen>
#include <QStatusBar>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QWidget>

namespace {

QVariantMap buildPaletteMap()
{
    const UiTheme::Colors& c = UiTheme::colors();
    const QString languageToken = []() -> QString {
        const QString token = UiText::resolvedLanguageToken();
        if (token.startsWith(QStringLiteral("zh"))) {
            return QStringLiteral("zh");
        }
        if (token.startsWith(QStringLiteral("ja"))) {
            return QStringLiteral("ja");
        }
        return QStringLiteral("en");
    }();
    return QVariantMap{
        {QStringLiteral("dark"), c.dark},
        {QStringLiteral("uiLanguage"), languageToken},
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
        {QStringLiteral("timelineHeader"), c.timelineHeader},
        {QStringLiteral("timelineSidebar"), c.timelineSidebar},
        {QStringLiteral("timelineBorder"), c.timelineBorder},
        {QStringLiteral("timelineLabel"), c.timelineLabel},
        {QStringLiteral("timelineAxis"), c.timelineAxis},
    };
}

QVariantMap buildAppBackgroundMap()
{
    if (qApp == nullptr) {
        return QVariantMap{};
    }
    return QVariantMap{
        {QStringLiteral("active"), qApp->property("miacode.appBackgroundActive").toBool()},
        {QStringLiteral("enabled"), qApp->property("miacode.appBackgroundEnabled").toBool()},
        {QStringLiteral("imagePath"), qApp->property("miacode.appBackgroundImagePath").toString()},
        {QStringLiteral("sourceUrl"), qApp->property("miacode.appBackgroundSourceUrl").toString()},
        {QStringLiteral("opacity"), qApp->property("miacode.appBackgroundOpacity").toDouble()},
        {QStringLiteral("blur"), qApp->property("miacode.appBackgroundBlur").toInt()},
        {QStringLiteral("panelAlphaDark"), qApp->property("miacode.appBackgroundPanelAlphaDark").toInt()},
        {QStringLiteral("panelAlphaLight"), qApp->property("miacode.appBackgroundPanelAlphaLight").toInt()},
        {QStringLiteral("sizeMode"), qApp->property("miacode.appBackgroundSizeMode").toString()},
        {QStringLiteral("position"), qApp->property("miacode.appBackgroundPosition").toString()},
    };
}

}  // namespace

QuickShellStyleBridge::QuickShellStyleBridge(
    QuickShellNativeContentProvider* contentProvider,
    QuickShellNativeSurfaceHost* surfaceHost,
    QObject* parent)
    : QObject(parent)
    , contentProvider_(contentProvider)
    , surfaceHost_(surfaceHost)
    , refreshTimer_(new QTimer(this))
{
    if (contentProvider_ != nullptr) {
        if (QWidget* shellWindow = contentProvider_->shellWindowWidget(); shellWindow != nullptr) {
            shellWindow->installEventFilter(this);
        }
        if (QDockWidget* outlineDock = contentProvider_->shellOutlineDockWidget(); outlineDock != nullptr) {
            outlineDock->installEventFilter(this);
        }
        if (QWidget* workspace = contentProvider_->shellWorkspaceWidget(); workspace != nullptr) {
            workspace->installEventFilter(this);
        }
        if (QWidget* bottomTabs = contentProvider_->shellBottomTabsWidget(); bottomTabs != nullptr) {
            bottomTabs->installEventFilter(this);
        }
    }
    if (surfaceHost_ != nullptr) {
        if (QWidget* sidebarSurface = surfaceHost_->sidebarSurfaceWidget(); sidebarSurface != nullptr) {
            sidebarSurface->installEventFilter(this);
        }
        if (QWidget* workspaceSurface = surfaceHost_->workspaceSurfaceWidget(); workspaceSurface != nullptr) {
            workspaceSurface->installEventFilter(this);
        }
        if (QWidget* bottomTabsSurface = surfaceHost_->bottomTabsSurfaceWidget(); bottomTabsSurface != nullptr) {
            bottomTabsSurface->installEventFilter(this);
        }
    }
    if (qApp != nullptr) {
        qApp->installEventFilter(this);
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

QVariantMap QuickShellStyleBridge::appBackground() const
{
    return appBackground_;
}

void QuickShellStyleBridge::syncWindowSize(int width, int height)
{
    if (contentProvider_ == nullptr) {
        return;
    }
    const QSize nextSize(qMax(width, 1), qMax(height, 1));
    if (QWidget* shellWindow = contentProvider_->shellWindowWidget();
        shellWindow != nullptr && shellWindow->size() != nextSize) {
        shellWindow->resize(nextSize);
        contentProvider_->shellRefreshLayoutAfterResize();
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
    case QEvent::DynamicPropertyChange:
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

    if (surfaceHost_ != nullptr) {
        surfaceHost_->refreshSurfaceStyles();
    }

    const QVariantMap nextPalette = buildPaletteMap();
    const QVariantMap nextAppBackground = buildAppBackgroundMap();
    if (nextPalette != palette_ || nextAppBackground != appBackground_) {
        palette_ = nextPalette;
        appBackground_ = nextAppBackground;
        emit appearanceChanged();
    }

    int initialWindowWidth = miacode::window_parity::kInitialWindowWidth;
    int initialWindowHeight = miacode::window_parity::kInitialWindowHeight;
    int initialWindowX = 120;
    int initialWindowY = 120;
    int previewPanelMinWidth = miacode::window_parity::kEmbeddedPreviewPanelMinWidth;
    int workspaceSidebarWidth = miacode::window_parity::kOutlineExpandedDefaultWidth;
    int workspaceContentMinWidth = miacode::window_parity::kWorkspaceContentMinWidth;
    int workspaceCompositeMinWidth = workspaceSidebarWidth + workspaceContentMinWidth;
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
        {QStringLiteral("leftColumnMinWidth"), workspaceCompositeMinWidth},
        {QStringLiteral("outlineDockWidth"), workspaceSidebarWidth},
        {QStringLiteral("workspaceSidebarWidth"), workspaceSidebarWidth},
        {QStringLiteral("workspaceContentMinWidth"), workspaceContentMinWidth},
        {QStringLiteral("workspaceCompositeMinWidth"), workspaceCompositeMinWidth},
        {QStringLiteral("topChromeHeight"), 78},
        {QStringLiteral("statusHeight"), 28},
        {QStringLiteral("previewPanelMinWidth"), previewPanelMinWidth},
        {QStringLiteral("previewPanelMaxWidth"), miacode::window_parity::kEmbeddedPreviewPanelWidthMax},
        {QStringLiteral("previewControlStatsCardMinWidth"), miacode::window_parity::kPreviewControlStatsCardMinWidth},
        {QStringLiteral("previewSplitterHandleWidth"), 6},
        {QStringLiteral("previewShellWidth"), 360},
        {QStringLiteral("previewCanvasAspectRatio"), 1.0},
        {QStringLiteral("previewPanelMarginX"), miacode::window_parity::kPreviewPanelMarginX},
        {QStringLiteral("previewPanelMarginTop"), miacode::window_parity::kPreviewPanelMarginTop},
        {QStringLiteral("previewCanvasControlGap"), miacode::window_parity::kPreviewCanvasControlGap},
        {QStringLiteral("previewStatsBottomGap"), miacode::window_parity::kPreviewStatsBottomGap},
        {QStringLiteral("previewControlsHeight"), 200},
        {QStringLiteral("previewSpeedButtonWidth"), miacode::window_parity::kPreviewSpeedButtonWidth},
        {QStringLiteral("previewControlButtonMinHeight"), miacode::window_parity::kPreviewControlButtonMinHeight},
        {QStringLiteral("previewControlButtonPaddingX"), miacode::window_parity::kPreviewControlButtonPaddingX},
        {QStringLiteral("previewControlButtonPaddingY"), miacode::window_parity::kPreviewControlButtonPaddingY},
        {QStringLiteral("previewControlCardRadius"), miacode::window_parity::kPreviewControlCardRadius},
        {QStringLiteral("previewStatsCardRadius"), miacode::window_parity::kPreviewStatsCardRadius},
        {QStringLiteral("previewStatsChipRadius"), miacode::window_parity::kPreviewStatsChipRadius},
        {QStringLiteral("previewStatsChipHeight"), miacode::window_parity::kPreviewStatsChipHeight},
        {QStringLiteral("previewStatsHorizontalSpacing"), miacode::window_parity::kPreviewStatsHorizontalSpacing},
        {QStringLiteral("previewStatsVerticalSpacing"), miacode::window_parity::kPreviewStatsVerticalSpacing},
        {QStringLiteral("previewStatsWideLayoutCols"), miacode::window_parity::kPreviewStatsWideLayoutCols},
        {QStringLiteral("previewStatsNarrowLayoutCols"), miacode::window_parity::kPreviewStatsNarrowLayoutCols},
        {QStringLiteral("previewStatsWideLayoutMinChipWidth"), miacode::window_parity::kPreviewStatsWideLayoutMinChipWidth},
        {QStringLiteral("previewControlStatsGap"), miacode::window_parity::kPreviewControlStatsGap},
        {QStringLiteral("bottomTabsHostHeight"), 260},
        {QStringLiteral("bottomTabsTabBarHeight"), 40},
        {QStringLiteral("bottomTabsResizeHotzoneHeight"), 8},
        {QStringLiteral("fullscreenHintTopMargin"), miacode::window_parity::kPreviewFullscreenHintTopMargin},
        {QStringLiteral("fullscreenOverlaySideMargin"), miacode::window_parity::kPreviewFullscreenOverlaySideMargin},
        {QStringLiteral("fullscreenOverlayBottomMargin"), miacode::window_parity::kPreviewFullscreenOverlayBottomMargin},
        {QStringLiteral("fullscreenOverlayMaxWidth"), miacode::window_parity::kPreviewFullscreenOverlayMaxWidth},
        {QStringLiteral("fullscreenOverlayHideOffset"), miacode::window_parity::kPreviewFullscreenOverlayHideOffset},
        {QStringLiteral("fullscreenControlsRevealHotzoneHeight"), miacode::window_parity::kPreviewFullscreenControlsRevealHotzoneHeight},
        {QStringLiteral("fullscreenControlsAutoHideDelayMs"), miacode::window_parity::kPreviewFullscreenControlsAutoHideDelayMs},
        {QStringLiteral("fullscreenHintAutoHideDelayMs"), miacode::window_parity::kPreviewFullscreenHintAutoHideDelayMs},
    };

    if (contentProvider_ != nullptr) {
        nextMetrics.insert(
            QStringLiteral("previewCanvasAspectRatio"),
            contentProvider_->shellNormalizedPreviewCanvasAspectRatio()
        );
        const int outlineWidth =
            contentProvider_->shellOutlineDockWidget() != nullptr
                ? contentProvider_->shellOutlineDockWidget()->width()
                : (contentProvider_->shellOutlineDockCollapsed()
                    ? miacode::window_parity::kOutlineCollapsedWidth
                    : qMax(
                        miacode::window_parity::kOutlineExpandedMinWidth,
                        contentProvider_->shellOutlineDockExpandedWidth()
                    ));
        workspaceSidebarWidth = qMax(0, outlineWidth);
        nextMetrics.insert(QStringLiteral("outlineDockWidth"), workspaceSidebarWidth);
        nextMetrics.insert(QStringLiteral("workspaceSidebarWidth"), workspaceSidebarWidth);
        if (QWidget* workspace = contentProvider_->shellWorkspaceWidget(); workspace != nullptr) {
            workspaceContentMinWidth = qMax(workspaceContentMinWidth, qMax(0, workspace->minimumWidth()));
        }
        workspaceCompositeMinWidth = workspaceSidebarWidth + workspaceContentMinWidth;
        nextMetrics.insert(QStringLiteral("workspaceContentMinWidth"), workspaceContentMinWidth);
        nextMetrics.insert(QStringLiteral("workspaceCompositeMinWidth"), workspaceCompositeMinWidth);
        nextMetrics.insert(QStringLiteral("leftColumnMinWidth"), workspaceCompositeMinWidth);
        nextMetrics.insert(
            QStringLiteral("bottomTabsHostHeight"),
            qMax(0, contentProvider_->shellBottomTabsHeight())
        );
        if (QWidget* bottomTabsWidget = contentProvider_->shellBottomTabsWidget(); bottomTabsWidget != nullptr) {
            if (auto* tabWidget = qobject_cast<QTabWidget*>(bottomTabsWidget); tabWidget != nullptr) {
                if (QTabBar* tabBar = tabWidget->tabBar(); tabBar != nullptr) {
                    tabBar->ensurePolished();
                    nextMetrics.insert(
                        QStringLiteral("bottomTabsTabBarHeight"),
                        qMax(tabBar->minimumSizeHint().height(), tabBar->sizeHint().height())
                    );
                }
            }
        }

        if (QMainWindow* mainWindow = qobject_cast<QMainWindow*>(contentProvider_->shellWindowWidget());
            mainWindow != nullptr) {
            const int menuHeight =
                mainWindow->menuBar() != nullptr ? qMax(24, mainWindow->menuBar()->sizeHint().height()) : 30;
            QToolBar* mainToolBar = mainWindow->findChild<QToolBar*>();
            const int toolBarHeight =
                mainToolBar != nullptr ? qMax(28, mainToolBar->sizeHint().height()) : 32;
            const int statusHeight =
                mainWindow->statusBar() != nullptr ? qMax(24, mainWindow->statusBar()->sizeHint().height()) : 24;
            nextMetrics.insert(QStringLiteral("topChromeHeight"), menuHeight + toolBarHeight + 2);
            nextMetrics.insert(QStringLiteral("statusHeight"), statusHeight);
        }
    }

    const QFont statsFont = miacode::preview::scene::previewHudMonoFont(10, QFont::DemiBold);
    const QFontMetrics chipMetrics(statsFont);
    const int templateChipWidth =
        chipMetrics.horizontalAdvance(QStringLiteral("Total  xxxxx/xxxxx"))
        + miacode::window_parity::kPreviewStatsChipPaddingX * 2
        + 2;
    const int statsHostWidthMin =
        templateChipWidth * miacode::window_parity::kPreviewStatsNarrowLayoutCols
        + miacode::window_parity::kPreviewStatsHorizontalSpacing
            * qMax(0, miacode::window_parity::kPreviewStatsNarrowLayoutCols - 1)
        + 4;
    const int panelWidthMin =
        statsHostWidthMin
        + miacode::window_parity::kPreviewPanelMarginX * 2
        + 16;
    previewPanelMinWidth = qMax(previewPanelMinWidth, panelWidthMin);
    nextMetrics.insert(QStringLiteral("previewPanelMinWidth"), previewPanelMinWidth);

    const int transportCardHeight =
        8
        + qMax(18, miacode::window_parity::kPreviewControlButtonMinHeight - 10)
        + 8
        + miacode::window_parity::kPreviewControlButtonMinHeight
        + 8;
    const miacode::window_parity::PreviewStatsLayout statsLayout =
        miacode::window_parity::computePreviewStatsLayout(
            qMax(statsHostWidthMin, previewPanelMinWidth - miacode::window_parity::kPreviewPanelMarginX * 2 - 16)
        );
    nextMetrics.insert(
        QStringLiteral("previewControlsHeight"),
        qMax(
            180,
            transportCardHeight
                + miacode::window_parity::kPreviewControlStatsGap
                + statsLayout.minCardHeight
        )
    );

    nextMetrics.insert(
        QStringLiteral("minimumWindowWidth"),
        qMax(
            miacode::window_parity::kInitialWindowFloorWidth,
            workspaceCompositeMinWidth
                + nextMetrics.value(QStringLiteral("previewSplitterHandleWidth")).toInt()
                + previewPanelMinWidth
        )
    );

    if (contentProvider_ != nullptr && contentProvider_->shellWindowWidget() != nullptr) {
        const int availableWidth = qMax(0, contentProvider_->shellWindowWidget()->width());
        const int availableHeight = qMax(
            0,
            contentProvider_->shellWindowWidget()->height()
                - nextMetrics.value(QStringLiteral("topChromeHeight")).toInt()
                - nextMetrics.value(QStringLiteral("statusHeight")).toInt()
        );
        const int controlHeight = nextMetrics.value(QStringLiteral("previewControlsHeight")).toInt();
        const int shellWidth = miacode::window_parity::computePreviewPanelTargetWidthForAdaptiveStats(
            availableWidth,
            availableHeight,
            workspaceCompositeMinWidth,
            controlHeight,
            contentProvider_->shellNormalizedPreviewCanvasAspectRatio()
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
