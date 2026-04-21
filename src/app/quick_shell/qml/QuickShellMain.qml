import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import MiaCode.Preview

ApplicationWindow {
    id: root

    property var paletteMap: ({})
    property var metricsMap: ({})
    property var shellController: controller
    property bool fullscreenControlsVisible: controller.previewFullscreen
    property bool fullscreenHintVisible: false
    property bool previewSpeedToastInitialized: false
    property string previewSpeedToastLastLabel: ""
    property bool previewSpeedToastVisible: false
    property string previewSpeedToastPercentText: "100%"
    property bool initialGeometryApplied: false
    property real previewPaneWidth: previewPaneInitialWidth(
        metric("initialWindowWidth", 1280),
        Math.max(
            1,
            metric("initialWindowHeight", 800)
                - metric("topChromeHeight", 106)
                - metric("statusHeight", 28)
        )
    )
    property bool previewPaneUserResized: false
    property bool previewPaneStartupBalancePending: true
    property bool startupLayoutLocked: true
    property bool startupContentReady: false
    property real lastPreviewCanvasAspectRatio: 1.0
    property real previewPaneWidthBeforeExport: 0
    property bool previewPaneWidthBeforeExportUserResized: false
    property bool previewPaneWidthRestorePending: false
    property real lastPreviewPaneRestoreGeneration: 0
    property real pendingStartupLayoutWidth: 0
    property real pendingStartupLayoutHeight: 0
    property real startupMinimumWindowWidth: 960
    property real startupMinimumWindowHeight: 640
    property bool embeddedSeparateSurfaceReady: true
    property bool embeddedInlineSurfaceActive: !controller.previewFullscreen
    property bool fullscreenInlineSurfaceActive: false
    property bool fullscreenHoveringRevealZone: false
    property bool fullscreenHoveringControls: false
    property rect previewSeekHotRect: {
        if (!previewPaneFrame || !root.contentItem)
            return Qt.rect(0, 0, 0, 0)
        const topLeft = previewPaneFrame.mapToItem(root.contentItem, 0, 0)
        return Qt.rect(topLeft.x, topLeft.y, previewPaneFrame.width, previewPaneFrame.height)
    }

    function metric(key, fallback) {
        return metricsMap && metricsMap[key] !== undefined ? metricsMap[key] : fallback
    }

    function tone(key, fallback) {
        return paletteMap && paletteMap[key] !== undefined ? paletteMap[key] : fallback
    }

    function syncStyleBridgeState() {
        if (!styleBridge)
            return
        paletteMap = styleBridge.palette
        metricsMap = styleBridge.metrics
    }

    function formatTransportTime(secondsValue) {
        const safeSeconds = Math.max(0, Math.floor(secondsValue + 0.0001))
        const minutes = Math.floor(safeSeconds / 60)
        const seconds = safeSeconds % 60
        return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0")
    }

    function formatPreviewSpeedPercent(speedLabel) {
        const normalized = (speedLabel || "").replace("x", "").trim()
        const numericValue = Number(normalized)
        if (!isFinite(numericValue) || numericValue <= 0)
            return "100%"
        return Math.round(numericValue * 100) + "%"
    }

    function showPreviewSpeedToast(speedLabel) {
        previewSpeedToastPercentText = formatPreviewSpeedPercent(speedLabel)
        previewSpeedToastVisible = true
        previewSpeedToastHideTimer.restart()
    }

    function handlePreviewSeekPress(event) {
        if (!event || event.modifiers !== Qt.NoModifier)
            return
        if (!event.isAutoRepeat && event.key === Qt.Key_F11) {
            controller.previewFullscreen = !controller.previewFullscreen
            event.accepted = true
            return
        }
        if (!event.isAutoRepeat && event.key === Qt.Key_Space) {
            controller.togglePreviewPlayback()
            event.accepted = true
            return
        }
        let direction = 0
        if (event.key === Qt.Key_Left)
            direction = -1
        else if (event.key === Qt.Key_Right)
            direction = 1
        if (direction === 0)
            return
        if (!controller.previewFullscreen)
            return
        event.accepted = true
        if (event.isAutoRepeat)
            return
        controller.beginPreviewHeldSeek(direction, event.key)
        controller.stepPreviewBySeconds(direction * controller.previewSeekSingleStepSeconds, true)
    }

    function handlePreviewSeekRelease(event) {
        if (!event || event.modifiers !== Qt.NoModifier)
            return
        if (event.key === Qt.Key_Space) {
            event.accepted = true
            return
        }
        if (!controller.previewFullscreen)
            return
        if (!event.isAutoRepeat && (event.key === Qt.Key_Left || event.key === Qt.Key_Right)) {
            controller.stopPreviewHeldSeek(event.key)
            event.accepted = true
        }
    }

    function showFullscreenControls() {
        if (!controller.previewFullscreen)
            return
        fullscreenControlsVisible = true
        if (fullscreenHoveringRevealZone || fullscreenHoveringControls) {
            fullscreenControlsHideTimer.stop()
            return
        }
        fullscreenControlsHideTimer.restart()
    }

    function previewLoaderStatusName(status) {
        if (status === Loader.Null)
            return "null"
        if (status === Loader.Ready)
            return "ready"
        if (status === Loader.Loading)
            return "loading"
        if (status === Loader.Error)
            return "error"
        return "unknown"
    }

    function logPreviewSurfaceTransition(action, extra) {
        if (!controller)
            return
        let payload = "fullscreen=" + (controller.previewFullscreen ? 1 : 0)
            + " fullscreen_window_visible=" + (fullscreenPreviewWindow.visible ? 1 : 0)
            + " embedded_inline_active=" + (embeddedInlineSurfaceActive ? 1 : 0)
            + " fullscreen_inline_active=" + (fullscreenInlineSurfaceActive ? 1 : 0)
            + " separate_surface=" + (controller.previewUsesSeparateSurface ? 1 : 0)
        if (extra && extra.length > 0)
            payload += " " + extra
        controller.logPreviewInteraction(action, payload)
    }

    function disableInlinePreviewSurfaces(reason) {
        embeddedInlineSurfaceActivateTimer.stop()
        fullscreenInlineSurfaceActivateTimer.stop()
        const embeddedWasActive = embeddedInlineSurfaceActive
        const fullscreenWasActive = fullscreenInlineSurfaceActive
        embeddedInlineSurfaceActive = false
        fullscreenInlineSurfaceActive = false
        logPreviewSurfaceTransition(
            "preview_surface_route_disable",
            "reason=" + reason
                + " embedded_was_active=" + (embeddedWasActive ? 1 : 0)
                + " fullscreen_was_active=" + (fullscreenWasActive ? 1 : 0)
        )
    }

    function armEmbeddedInlineSurface(reason) {
        disableInlinePreviewSurfaces(reason)
        logPreviewSurfaceTransition("preview_surface_route_arm", "target=embedded reason=" + reason)
        embeddedInlineSurfaceActivateTimer.restart()
    }

    function armFullscreenInlineSurface(reason) {
        disableInlinePreviewSurfaces(reason)
        logPreviewSurfaceTransition("preview_surface_route_arm", "target=fullscreen reason=" + reason)
        fullscreenInlineSurfaceActivateTimer.restart()
    }

    function previewPaneHandleWidth() {
        return metric("previewSplitterHandleWidth", 6)
    }

    function sidebarPaneWidth() {
        return metric("workspaceSidebarWidth", metric("outlineDockWidth", 190))
    }

    function contentPaneMinWidth() {
        return metric("workspaceContentMinWidth", 320)
    }

    function workspacePaneMinWidth() {
        return metric(
            "workspaceCompositeMinWidth",
            sidebarPaneWidth() + contentPaneMinWidth()
        )
    }

    function previewPaneMinWidth() {
        return metric("previewPanelMinWidth", 320)
    }

    function previewPaneAvailableWidth(totalWidth) {
        return Math.max(0, totalWidth - sidebarPaneWidth() - previewPaneHandleWidth())
    }

    function previewCanvasAspectRatio() {
        if (!controller || controller.previewCanvasAspectRatio === undefined)
            return metric("previewCanvasAspectRatio", 1.0)
        return Math.max(1.0, controller.previewCanvasAspectRatio)
    }

    function previewStatsMinimumHeightForRightWidth(rightWidth) {
        const statsHostWidth = Math.max(
            0,
            rightWidth - metric("previewPanelMarginX", 8) * 2 - 16
        )
        const itemCount = 6
        const wideCols = metric("previewStatsWideLayoutCols", 3)
        const narrowCols = metric("previewStatsNarrowLayoutCols", 2)
        const horizontalSpacing = metric("previewStatsHorizontalSpacing", 10)
        const verticalSpacing = metric("previewStatsVerticalSpacing", 6)
        const chipHeight = metric("previewStatsChipHeight", 30)
        const wideLayoutThreshold = wideCols * metric("previewStatsWideLayoutMinChipWidth", 118)
            + Math.max(0, wideCols - 1) * horizontalSpacing
        const useWideLayout = statsHostWidth >= wideLayoutThreshold
        const columns = Math.max(1, Math.min(itemCount, useWideLayout ? wideCols : narrowCols))
        const rows = Math.max(1, Math.ceil(itemCount / columns))
        return 16 + 2 + 2 + rows * chipHeight + Math.max(0, rows - 1) * verticalSpacing
    }

    function previewPaneMinimumRightWidth(totalWidth) {
        const availableWidth = Math.max(0, totalWidth - previewPaneHandleWidth())
        const leftMinWidth = workspacePaneMinWidth()
        const minimumCandidate = metric("previewControlStatsCardMinWidth", 280)
            + metric("previewPanelMarginX", 8) * 2
        return availableWidth >= leftMinWidth + minimumCandidate
            ? minimumCandidate
            : Math.min(availableWidth, minimumCandidate)
    }

    function previewPaneRightMaxWidth(totalWidth) {
        const availableWidth = Math.max(0, totalWidth - previewPaneHandleWidth())
        const leftMinWidth = workspacePaneMinWidth()
        const minimumRightWidth = previewPaneMinimumRightWidth(totalWidth)
        return availableWidth >= leftMinWidth + minimumRightWidth
            ? Math.min(metric("previewPanelMaxWidth", 900), availableWidth - leftMinWidth)
            : availableWidth
    }

    function previewPaneTargetWidth(totalWidth, totalHeight, minimumStatsHeight) {
        const availableWidth = Math.max(0, totalWidth - previewPaneHandleWidth())
        const minimumRightWidth = previewPaneMinimumRightWidth(totalWidth)
        const rightMaxWidth = previewPaneRightMaxWidth(totalWidth)
        const aspectRatio = previewCanvasAspectRatio()
        let targetRightWidth = Math.round(availableWidth * 0.5)
        targetRightWidth = Math.min(targetRightWidth, rightMaxWidth)
        targetRightWidth = Math.max(
            targetRightWidth,
            Math.min(metric("previewPanelMinWidth", 320), rightMaxWidth)
        )
        for (let iteration = 0; iteration < 3; ++iteration) {
            const availablePreviewHeight = Math.max(
                0,
                totalHeight
                    - metric("previewPanelMarginTop", 12)
                    - metric("previewCanvasControlGap", 10)
                    - Math.max(0, metric("previewControlsHeight", 200))
                    - metric("previewControlStatsGap", 10)
                    - Math.max(0, minimumStatsHeight)
                    - metric("previewStatsBottomGap", 12)
            )
            const heightLimitedWidth = Math.max(
                0,
                Math.round(availablePreviewHeight * aspectRatio)
                    + metric("previewPanelMarginX", 8) * 2
            )
            const nextRightWidth = Math.min(
                targetRightWidth,
                Math.max(minimumRightWidth, heightLimitedWidth)
            )
            if (nextRightWidth === targetRightWidth)
                break
            targetRightWidth = nextRightWidth
        }
        return Math.max(minimumRightWidth, Math.min(targetRightWidth, rightMaxWidth))
    }

    function previewPaneAdaptiveTargetWidth(totalWidth, totalHeight) {
        const minimumRightWidth = previewPaneMinimumRightWidth(totalWidth)
        const rightMaxWidth = previewPaneRightMaxWidth(totalWidth)
        if (rightMaxWidth <= 0)
            return 0
        let targetRightWidth = Math.max(
            minimumRightWidth,
            Math.min(
                previewPaneTargetWidth(
                    totalWidth,
                    totalHeight,
                    previewStatsMinimumHeightForRightWidth(rightMaxWidth)
                ),
                rightMaxWidth
            )
        )
        for (let iteration = 0; iteration < 4; ++iteration) {
            const nextRightWidth = Math.max(
                minimumRightWidth,
                Math.min(
                    previewPaneTargetWidth(
                        totalWidth,
                        totalHeight,
                        previewStatsMinimumHeightForRightWidth(targetRightWidth)
                    ),
                    rightMaxWidth
                )
            )
            if (nextRightWidth === targetRightWidth)
                break
            targetRightWidth = nextRightWidth
        }
        return targetRightWidth
    }

    function previewPaneMaxWidth(totalWidth, totalHeight) {
        const minWidth = previewPaneMinWidth()
        const maxByWindow = previewPaneRightMaxWidth(totalWidth)
        return Math.max(minWidth, Math.min(metric("previewPanelMaxWidth", 900), maxByWindow))
    }

    function clampPreviewPaneWidth(candidate, totalWidth, totalHeight) {
        const minWidth = previewPaneMinWidth()
        const maxWidth = previewPaneMaxWidth(totalWidth, totalHeight)
        if (maxWidth <= minWidth)
            return minWidth
        return Math.max(minWidth, Math.min(candidate, maxWidth))
    }

    function previewPaneDefaultWidth(totalWidth, totalHeight) {
        return previewPaneAdaptiveTargetWidth(totalWidth, totalHeight)
    }

    function previewPaneInitialWidth(totalWidth, totalHeight) {
        const workspaceMinWidth = workspacePaneMinWidth()
        const availableWidth = Math.max(0, totalWidth - previewPaneHandleWidth())
        const initialWorkspaceWidth = Math.min(
            availableWidth,
            Math.max(workspaceMinWidth, Math.floor(availableWidth / 2))
        )
        return Math.max(0, availableWidth - initialWorkspaceWidth)
    }

    function fittedPreviewFrameWidth(hostWidth, hostHeight) {
        const safeWidth = Math.max(1, hostWidth)
        const safeHeight = Math.max(1, hostHeight)
        return Math.max(1, Math.min(safeWidth, safeHeight * previewCanvasAspectRatio()))
    }

    function fittedPreviewFrameHeight(hostWidth, hostHeight) {
        const frameWidth = fittedPreviewFrameWidth(hostWidth, hostHeight)
        return Math.max(1, Math.min(hostHeight, frameWidth / previewCanvasAspectRatio()))
    }

    function noteStartupLayoutActivity(totalWidth, totalHeight) {
        if (!startupLayoutLocked)
            return
        startupMinimumWindowWidth = metric("minimumWindowWidth", 960)
        startupMinimumWindowHeight = metric("minimumWindowHeight", 640)
        applyWindowMinimumSize()
        if (totalWidth > 0)
            pendingStartupLayoutWidth = totalWidth
        if (totalHeight > 0)
            pendingStartupLayoutHeight = totalHeight
        startupLayoutSettleTimer.restart()
    }

    function applyWindowMinimumSize() {
        const nextMinimumWidth = startupLayoutLocked ? startupMinimumWindowWidth : metric("minimumWindowWidth", 960)
        const nextMinimumHeight = startupLayoutLocked ? startupMinimumWindowHeight : metric("minimumWindowHeight", 640)
        root.minimumWidth = nextMinimumWidth
        root.minimumHeight = nextMinimumHeight
        if (!root.visible) {
            if (root.width < nextMinimumWidth) {
                root.x -= Math.round((nextMinimumWidth - root.width) / 2)
                root.width = nextMinimumWidth
            }
            if (root.height < nextMinimumHeight) {
                root.y -= Math.round((nextMinimumHeight - root.height) / 2)
                root.height = nextMinimumHeight
            }
        }
    }

    function finalizeStartupLayout() {
        if (!startupLayoutLocked)
            return
        const resolvedWidth = pendingStartupLayoutWidth > 0 ? pendingStartupLayoutWidth : workspaceRow.width
        const resolvedHeight = pendingStartupLayoutHeight > 0 ? pendingStartupLayoutHeight : workspaceRow.height
        if (!root.visible || resolvedWidth <= 0 || resolvedHeight <= 0) {
            startupLayoutSettleTimer.restart()
            return
        }
        previewPaneUserResized = false
        previewPaneStartupBalancePending = true
        startupMinimumWindowWidth = metric("minimumWindowWidth", 960)
        startupMinimumWindowHeight = metric("minimumWindowHeight", 640)
        applyWindowMinimumSize()
        previewPaneWidth = clampPreviewPaneWidth(
            previewPaneInitialWidth(resolvedWidth, resolvedHeight),
            resolvedWidth,
            resolvedHeight
        )
        previewPaneStartupBalancePending = false
        startupLayoutLocked = false
        startupContentReady = true
        applyWindowMinimumSize()
        styleBridge.refreshNow()
        controller.refresh()
    }

    function syncPreviewPaneWidth(totalWidth, totalHeight, preserveUserChoice, preserveCurrentWidth) {
        if (totalWidth <= 0)
            return
        noteStartupLayoutActivity(totalWidth, totalHeight)
        if (startupLayoutLocked) {
            previewPaneWidth = clampPreviewPaneWidth(
                previewPaneInitialWidth(totalWidth, totalHeight),
                totalWidth,
                totalHeight
            )
            return
        }
        const defaultWidth = previewPaneStartupBalancePending
            ? previewPaneInitialWidth(totalWidth, totalHeight)
            : previewPaneDefaultWidth(totalWidth, totalHeight)
        const fallbackWidth = previewPaneStartupBalancePending
            ? defaultWidth
            : clampPreviewPaneWidth(defaultWidth, totalWidth, totalHeight)
        if (preserveCurrentWidth && previewPaneWidth > 0) {
            previewPaneWidth = clampPreviewPaneWidth(previewPaneWidth, totalWidth, totalHeight)
            previewPaneStartupBalancePending = false
            return
        }
        if (!preserveUserChoice || !previewPaneUserResized) {
            previewPaneWidth = fallbackWidth
            if (!preserveUserChoice)
                previewPaneUserResized = false
            previewPaneStartupBalancePending = false
            return
        }
        if (previewPaneWidth <= 0) {
            previewPaneWidth = fallbackWidth
            previewPaneStartupBalancePending = false
            return
        }
        previewPaneWidth = clampPreviewPaneWidth(previewPaneWidth, totalWidth, totalHeight)
        previewPaneStartupBalancePending = false
    }

    visible: true
    title: controller.windowTitle
    color: tone("windowBg", "#f8fafd")
    onMetricsMapChanged: {
        Qt.callLater(function() {
            applyWindowMinimumSize()
            syncPreviewPaneWidth(workspaceRow.width, workspaceRow.height, true, !startupLayoutLocked)
        })
    }
    onVisibleChanged: {
        if (visible)
            noteStartupLayoutActivity(width, Math.max(1, height - metric("topChromeHeight", 106) - metric("statusHeight", 28)))
    }

    Timer {
        id: startupLayoutSettleTimer
        interval: 120
        repeat: false
        onTriggered: finalizeStartupLayout()
    }

    palette.window: tone("windowBg", "#f8fafd")
    palette.base: tone("inputBg", "#ffffff")
    palette.button: tone("cardBg", "#ffffff")
    palette.windowText: tone("textPrimary", "#203040")
    palette.text: tone("textPrimary", "#203040")
    palette.buttonText: tone("textPrimary", "#203040")
    palette.highlight: tone("accent", "#2e77d0")
    palette.highlightedText: tone("accentText", "#ffffff")

    Component.onCompleted: {
        syncStyleBridgeState()
        lastPreviewCanvasAspectRatio = previewCanvasAspectRatio()
        if (!initialGeometryApplied) {
            width = metric("initialWindowWidth", 1280)
            height = metric("initialWindowHeight", 800)
            x = metric("initialWindowX", 120)
            y = metric("initialWindowY", 120)
            initialGeometryApplied = true
        }
        startupMinimumWindowWidth = metric("minimumWindowWidth", 960)
        startupMinimumWindowHeight = metric("minimumWindowHeight", 640)
        applyWindowMinimumSize()
        styleBridge.syncWindowSize(width, height)
        controller.refresh()
        noteStartupLayoutActivity(width, Math.max(1, height - metric("topChromeHeight", 106) - metric("statusHeight", 28)))
    }
    onWidthChanged: styleBridge.syncWindowSize(width, height)
    onHeightChanged: styleBridge.syncWindowSize(width, height)

    onClosing: function(close) {
        if (!controller.confirmClose())
            close.accepted = false
    }

    Connections {
        target: styleBridge

        function onAppearanceChanged() {
            root.paletteMap = styleBridge.palette
            if (embeddedTransport)
                embeddedTransport.paletteRefreshRequested()
            if (fullscreenTransport)
                fullscreenTransport.paletteRefreshRequested()
        }

        function onMetricsChanged() {
            root.metricsMap = styleBridge.metrics
        }
    }

    Connections {
        target: controller

        function onShellStateChanged() {
            const nextSpeedLabel = controller ? controller.previewSpeedLabel : ""
            if (!root.previewSpeedToastInitialized) {
                root.previewSpeedToastInitialized = true
                root.previewSpeedToastLastLabel = nextSpeedLabel
            } else if (nextSpeedLabel !== root.previewSpeedToastLastLabel) {
                root.previewSpeedToastLastLabel = nextSpeedLabel
                root.showPreviewSpeedToast(nextSpeedLabel)
            }

            const nextRestoreGeneration = controller.previewPaneRestoreGeneration
            if (nextRestoreGeneration !== root.lastPreviewPaneRestoreGeneration) {
                root.lastPreviewPaneRestoreGeneration = nextRestoreGeneration
                if (Math.abs(root.lastPreviewCanvasAspectRatio - 1.0) <= 0.0001 && previewPaneWidth > 0) {
                    root.previewPaneWidthBeforeExport = previewPaneWidth
                    root.previewPaneWidthBeforeExportUserResized = previewPaneUserResized
                    root.previewPaneWidthRestorePending = true
                }
            }
            const nextAspectRatio = previewCanvasAspectRatio()
            const previousAspectRatio = root.lastPreviewCanvasAspectRatio
            if (Math.abs(nextAspectRatio - previousAspectRatio) <= 0.0001)
                return
            const restoringToSquare = Math.abs(nextAspectRatio - 1.0) <= 0.0001
                && previousAspectRatio > 1.0001
            root.lastPreviewCanvasAspectRatio = nextAspectRatio
            if (restoringToSquare && root.previewPaneWidthRestorePending && root.previewPaneWidthBeforeExport > 0) {
                previewPaneWidth = clampPreviewPaneWidth(
                    root.previewPaneWidthBeforeExport,
                    workspaceRow.width,
                    workspaceRow.height
                )
                previewPaneUserResized = root.previewPaneWidthBeforeExportUserResized
                root.previewPaneWidthRestorePending = false
                return
            }
            previewPaneUserResized = false
            previewPaneWidth = clampPreviewPaneWidth(
                previewPaneAdaptiveTargetWidth(workspaceRow.width, workspaceRow.height),
                workspaceRow.width,
                workspaceRow.height
            )
        }

        function onPreviewFullscreenChanged() {
            logPreviewSurfaceTransition("preview_surface_fullscreen_changed")
            if (!controller.previewFullscreen) {
                armEmbeddedInlineSurface("fullscreen_disabled")
                fullscreenControlsVisible = false
                fullscreenHintVisible = false
                fullscreenControlsHideTimer.stop()
                fullscreenHintHideTimer.stop()
                fullscreenHoveringRevealZone = false
                fullscreenHoveringControls = false
                embeddedSeparateSurfaceReady = true
                styleBridge.refreshNow()
                controller.refresh()
                root.requestActivate()
                embeddedPreviewInteractionRoot.forceActiveFocus()
                return
            }
            armFullscreenInlineSurface("fullscreen_enabled")
            embeddedSeparateSurfaceReady = false
            fullscreenHoveringRevealZone = false
            fullscreenHoveringControls = false
            fullscreenControlsVisible = true
            fullscreenHintVisible = true
            fullscreenControlsHideTimer.restart()
            fullscreenHintHideTimer.restart()
        }
    }

    Timer {
        id: fullscreenControlsHideTimer
        interval: metric("fullscreenControlsAutoHideDelayMs", 1200)
        repeat: false
        onTriggered: {
            if (fullscreenHoveringRevealZone || fullscreenHoveringControls) {
                restart()
                return
            }
            fullscreenControlsVisible = false
        }
    }

    Timer {
        id: fullscreenHintHideTimer
        interval: metric("fullscreenHintAutoHideDelayMs", 2200)
        repeat: false
        onTriggered: fullscreenHintVisible = false
    }

    Timer {
        id: previewSpeedToastHideTimer
        interval: 900
        repeat: false
        onTriggered: root.previewSpeedToastVisible = false
    }

    Timer {
        id: embeddedInlineSurfaceActivateTimer
        interval: 16
        repeat: false
        onTriggered: {
            const canActivate = !controller.previewFullscreen && !fullscreenPreviewWindow.visible
            logPreviewSurfaceTransition(
                "preview_surface_route_timer",
                "target=embedded can_activate=" + (canActivate ? 1 : 0)
            )
            if (!canActivate)
                return
            embeddedInlineSurfaceActive = true
            logPreviewSurfaceTransition("preview_surface_route_commit", "target=embedded")
        }
    }

    Timer {
        id: fullscreenInlineSurfaceActivateTimer
        interval: 16
        repeat: false
        onTriggered: {
            const canActivate = controller.previewFullscreen && fullscreenPreviewWindow.visible
            logPreviewSurfaceTransition(
                "preview_surface_route_timer",
                "target=fullscreen can_activate=" + (canActivate ? 1 : 0)
            )
            if (!canActivate)
                return
            fullscreenInlineSurfaceActive = true
            fullscreenPreviewWindow.requestActivate()
            fullscreenInteractionRoot.forceActiveFocus()
            logPreviewSurfaceTransition("preview_surface_route_commit", "target=fullscreen")
        }
    }

    Shortcut {
        sequence: "Esc"
        enabled: controller.previewFullscreen
        context: Qt.ApplicationShortcut
        onActivated: controller.previewFullscreen = false
    }

    Shortcut {
        sequence: "F11"
        context: Qt.ApplicationShortcut
        onActivated: controller.previewFullscreen = !controller.previewFullscreen
    }

    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        onActivated: controller.togglePreviewPlayback()
    }

    Menu {
        id: previewSpeedMenu
        popupType: Popup.Window
        padding: 4
        leftPadding: 4
        rightPadding: 4
        topPadding: 4
        bottomPadding: 4
        implicitWidth: 78
        width: implicitWidth
        palette.window: tone("menuBg", "#ffffff")
        palette.base: tone("menuBg", "#ffffff")
        palette.text: tone("textPrimary", "#203040")
        palette.windowText: tone("textPrimary", "#203040")
        palette.buttonText: tone("textPrimary", "#203040")
        palette.highlight: tone("accent", "#2e77d0")
        palette.highlightedText: tone("accentText", "#ffffff")
        background: Rectangle {
            color: tone("menuBg", "#ffffff")
            border.color: tone("menuBorder", "#d5e0ec")
            radius: 8
        }

        MenuItem {
            id: speed25Item
            readonly property bool selectedRate: controller && controller.previewSpeedLabel === text
            text: "0.25x"
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            implicitHeight: 32
            implicitWidth: previewSpeedMenu.width - previewSpeedMenu.leftPadding - previewSpeedMenu.rightPadding
            indicator: Item { implicitWidth: 0; implicitHeight: 0 }
            onTriggered: controller.setPreviewRate(0.25)
            background: Rectangle {
                radius: 6
                color: speed25Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed25Item.selectedRate ? tone("menuHoverBg", "#eef5ff") : "transparent")
                border.color: speed25Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed25Item.selectedRate ? tone("border", "#d5e0ec") : "transparent")
            }
            contentItem: Text {
                text: speed25Item.text
                color: speed25Item.highlighted ? tone("accentText", "#ffffff") : tone("textPrimary", "#203040")
                font.pixelSize: 13
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
        }
        MenuItem {
            id: speed50Item
            readonly property bool selectedRate: controller && controller.previewSpeedLabel === text
            text: "0.5x"
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            implicitHeight: 32
            implicitWidth: previewSpeedMenu.width - previewSpeedMenu.leftPadding - previewSpeedMenu.rightPadding
            indicator: Item { implicitWidth: 0; implicitHeight: 0 }
            onTriggered: controller.setPreviewRate(0.5)
            background: Rectangle {
                radius: 6
                color: speed50Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed50Item.selectedRate ? tone("menuHoverBg", "#eef5ff") : "transparent")
                border.color: speed50Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed50Item.selectedRate ? tone("border", "#d5e0ec") : "transparent")
            }
            contentItem: Text {
                text: speed50Item.text
                color: speed50Item.highlighted ? tone("accentText", "#ffffff") : tone("textPrimary", "#203040")
                font.pixelSize: 13
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
        }
        MenuItem {
            id: speed75Item
            readonly property bool selectedRate: controller && controller.previewSpeedLabel === text
            text: "0.75x"
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            implicitHeight: 32
            implicitWidth: previewSpeedMenu.width - previewSpeedMenu.leftPadding - previewSpeedMenu.rightPadding
            indicator: Item { implicitWidth: 0; implicitHeight: 0 }
            onTriggered: controller.setPreviewRate(0.75)
            background: Rectangle {
                radius: 6
                color: speed75Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed75Item.selectedRate ? tone("menuHoverBg", "#eef5ff") : "transparent")
                border.color: speed75Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed75Item.selectedRate ? tone("border", "#d5e0ec") : "transparent")
            }
            contentItem: Text {
                text: speed75Item.text
                color: speed75Item.highlighted ? tone("accentText", "#ffffff") : tone("textPrimary", "#203040")
                font.pixelSize: 13
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
        }
        MenuItem {
            id: speed100Item
            readonly property bool selectedRate: controller && controller.previewSpeedLabel === text
            text: "1x"
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            implicitHeight: 32
            implicitWidth: previewSpeedMenu.width - previewSpeedMenu.leftPadding - previewSpeedMenu.rightPadding
            indicator: Item { implicitWidth: 0; implicitHeight: 0 }
            onTriggered: controller.setPreviewRate(1.0)
            background: Rectangle {
                radius: 6
                color: speed100Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed100Item.selectedRate ? tone("menuHoverBg", "#eef5ff") : "transparent")
                border.color: speed100Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed100Item.selectedRate ? tone("border", "#d5e0ec") : "transparent")
            }
            contentItem: Text {
                text: speed100Item.text
                color: speed100Item.highlighted ? tone("accentText", "#ffffff") : tone("textPrimary", "#203040")
                font.pixelSize: 13
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
        }
        MenuItem {
            id: speed125Item
            readonly property bool selectedRate: controller && controller.previewSpeedLabel === text
            text: "1.25x"
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            implicitHeight: 32
            implicitWidth: previewSpeedMenu.width - previewSpeedMenu.leftPadding - previewSpeedMenu.rightPadding
            indicator: Item { implicitWidth: 0; implicitHeight: 0 }
            onTriggered: controller.setPreviewRate(1.25)
            background: Rectangle {
                radius: 6
                color: speed125Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed125Item.selectedRate ? tone("menuHoverBg", "#eef5ff") : "transparent")
                border.color: speed125Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed125Item.selectedRate ? tone("border", "#d5e0ec") : "transparent")
            }
            contentItem: Text {
                text: speed125Item.text
                color: speed125Item.highlighted ? tone("accentText", "#ffffff") : tone("textPrimary", "#203040")
                font.pixelSize: 13
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
        }
        MenuItem {
            id: speed200Item
            readonly property bool selectedRate: controller && controller.previewSpeedLabel === text
            text: "2x"
            leftPadding: 12
            rightPadding: 12
            topPadding: 0
            bottomPadding: 0
            implicitHeight: 32
            implicitWidth: previewSpeedMenu.width - previewSpeedMenu.leftPadding - previewSpeedMenu.rightPadding
            indicator: Item { implicitWidth: 0; implicitHeight: 0 }
            onTriggered: controller.setPreviewRate(2.0)
            background: Rectangle {
                radius: 6
                color: speed200Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed200Item.selectedRate ? tone("menuHoverBg", "#eef5ff") : "transparent")
                border.color: speed200Item.highlighted
                    ? tone("accent", "#2e77d0")
                    : (speed200Item.selectedRate ? tone("border", "#d5e0ec") : "transparent")
            }
            contentItem: Text {
                text: speed200Item.text
                color: speed200Item.highlighted ? tone("accentText", "#ffffff") : tone("textPrimary", "#203040")
                font.pixelSize: 13
                font.weight: Font.Medium
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        opacity: startupContentReady ? 1 : 0

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: metric("topChromeHeight", 106)
            Layout.minimumHeight: metric("topChromeHeight", 106)
            Layout.maximumHeight: metric("topChromeHeight", 106)

            WindowContainer {
                anchors.fill: parent
                window: controller.topChromeWindow
                Component.onCompleted: controller.syncTopChromeSurfaceSize(width, height)
                onWidthChanged: controller.syncTopChromeSurfaceSize(width, height)
                onHeightChanged: controller.syncTopChromeSurfaceSize(width, height)
            }
        }

        RowLayout {
            id: workspaceRow

            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            onWidthChanged: syncPreviewPaneWidth(width, height, true)
            onHeightChanged: syncPreviewPaneWidth(width, height, true)

            WindowContainer {
                Layout.preferredWidth: sidebarPaneWidth()
                Layout.minimumWidth: sidebarPaneWidth()
                Layout.maximumWidth: sidebarPaneWidth()
                Layout.fillHeight: true
                window: controller.sidebarWindow
                Component.onCompleted: controller.syncSidebarSurfaceSize(width, height)
                onWidthChanged: controller.syncSidebarSurfaceSize(width, height)
                onHeightChanged: controller.syncSidebarSurfaceSize(width, height)
            }

            RowLayout {
                id: workspaceContentRow

                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0
                layoutDirection: controller.workspacePanelsSwapped ? Qt.RightToLeft : Qt.LeftToRight

                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 0

                    WindowContainer {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        window: controller.workspaceWindow
                        Component.onCompleted: controller.syncWorkspaceSurfaceSize(width, height)
                        onWidthChanged: controller.syncWorkspaceSurfaceSize(width, height)
                        onHeightChanged: controller.syncWorkspaceSurfaceSize(width, height)
                    }

                    BottomTabsQuickHost {
                        Layout.fillWidth: true
                        Layout.preferredHeight: controller.bottomTabsVisible
                            ? metric("bottomTabsHostHeight", 260)
                            : 0
                        Layout.minimumHeight: controller.bottomTabsVisible
                            ? metric("bottomTabsHostHeight", 260)
                            : 0
                        Layout.maximumHeight: controller.bottomTabsVisible
                            ? metric("bottomTabsHostHeight", 260)
                            : 0
                        visible: controller.bottomTabsVisible
                        controller: root.shellController
                        paletteMap: root.paletteMap
                        metricsMap: root.metricsMap
                    }
                }

                Rectangle {
                    id: previewResizeHandle

                    Layout.preferredWidth: previewPaneHandleWidth()
                    Layout.minimumWidth: previewPaneHandleWidth()
                    Layout.maximumWidth: previewPaneHandleWidth()
                    Layout.fillHeight: true
                    color: previewResizeMouseArea.pressed
                        ? tone("accent", "#2e77d0")
                        : (previewResizeMouseArea.containsMouse
                            ? tone("menuHoverBg", "#eef5ff")
                            : tone("border", "#d5e0ec"))

                    Rectangle {
                        anchors.centerIn: parent
                        width: 2
                        height: 64
                        radius: 1
                        color: tone("borderSoft", "#ccd6e2")
                    }

                    MouseArea {
                        id: previewResizeMouseArea

                        anchors.fill: parent
                        hoverEnabled: true
                        cursorShape: Qt.SizeHorCursor

                        property real dragStartSceneX: 0
                        property real dragStartWidth: 0

                        onPressed: function(mouse) {
                            dragStartSceneX = previewResizeHandle.mapToItem(root.contentItem, mouse.x, mouse.y).x
                            dragStartWidth = previewPaneWidth
                        }

                        onPositionChanged: function(mouse) {
                            if (!pressed)
                                return
                            const sceneX = previewResizeHandle.mapToItem(root.contentItem, mouse.x, mouse.y).x
                            const deltaX = sceneX - dragStartSceneX
                            const signedDelta = controller.workspacePanelsSwapped ? deltaX : -deltaX
                            previewPaneWidth = clampPreviewPaneWidth(
                                dragStartWidth + signedDelta,
                                workspaceRow.width,
                                workspaceRow.height
                            )
                            previewPaneUserResized = true
                        }
                    }
                }

                Rectangle {
                    id: previewPaneFrame
                    Layout.preferredWidth: previewPaneWidth
                    Layout.minimumWidth: previewPaneMinWidth()
                    Layout.maximumWidth: previewPaneMaxWidth(workspaceRow.width, workspaceRow.height)
                    Layout.fillHeight: true
                    color: tone("panelBg", "#f5f7fa")
                    border.color: tone("border", "#d5e0ec")

                    FocusScope {
                        id: embeddedPreviewInteractionRoot

                        anchors.fill: parent

                        Keys.onPressed: root.handlePreviewSeekPress(event)
                        Keys.onReleased: root.handlePreviewSeekRelease(event)

                        onActiveFocusChanged: controller.logPreviewInteraction(
                            "qml_preview_focus_changed",
                            "active=" + (activeFocus ? 1 : 0)
                        )

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            anchors.topMargin: 12
                            anchors.bottomMargin: 12
                            spacing: 10

                            Item {
                                Layout.fillWidth: true
                                Layout.fillHeight: true

                                Rectangle {
                                    id: embeddedPreviewFrame

                                    visible: !controller.previewFullscreen
                                    width: fittedPreviewFrameWidth(parent.width, parent.height)
                                    height: fittedPreviewFrameHeight(parent.width, parent.height)
                                    anchors.centerIn: parent
                                    color: tone("canvasBg", "#000000")
                                    border.color: tone("borderSoft", "#ccd6e2")
                                    clip: true

                                    Loader {
                                        id: embeddedInlineSurfaceLoader
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        active: !controller.previewFullscreen
                                            && embeddedInlineSurfaceActive
                                            && (!controller.previewUsesSeparateSurface
                                                || !embeddedSeparateSurfaceReady)
                                        onStatusChanged: logPreviewSurfaceTransition(
                                            "preview_surface_loader_status",
                                            "loader=embedded_inline status=" + previewLoaderStatusName(status)
                                                + " active=" + (active ? 1 : 0)
                                        )

                                        sourceComponent: QuickShellPreviewSurface {
                                            runtime: controller.previewRuntime
                                            mediaHost: controller.previewStageMediaHost
                                            logger: controller
                                            surfaceRole: "embedded_inline"
                                        }
                                    }

                                    Loader {
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        active: !controller.previewFullscreen
                                            && controller.previewUsesSeparateSurface
                                            && embeddedSeparateSurfaceReady

                                        sourceComponent: WindowContainer {
                                            anchors.fill: parent
                                            window: controller.previewCompositeWindow
                                        }
                                    }
                                }

                                TapHandler {
                                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                                    onTapped: {
                                        controller.logPreviewInteraction(
                                            "qml_preview_tapped",
                                            "x=" + point.position.x + " y=" + point.position.y
                                        )
                                        embeddedPreviewInteractionRoot.forceActiveFocus()
                                    }
                                }
                            }

                            Item {
                                id: embeddedTransportStackHost
                                Layout.fillWidth: true
                                implicitHeight: embeddedTransportStack.implicitHeight
                                Layout.preferredHeight: implicitHeight
                                Layout.minimumHeight: implicitHeight

                                ColumnLayout {
                                    id: embeddedTransportStack
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    spacing: metric("previewControlStatsGap", 10)

                                    QuickShellPreviewTransport {
                                        id: embeddedTransport
                                        Layout.fillWidth: true
                                        controller: root.shellController
                                        paletteMap: root.paletteMap
                                        metricsMap: root.metricsMap
                                        speedMenu: previewSpeedMenu
                                        fullscreenMode: false
                                        onFocusRequested: embeddedPreviewInteractionRoot.forceActiveFocus()
                                    }

                                    QuickShellPreviewStatsPanel {
                                        id: embeddedStatsPanel
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: implicitHeight
                                        controller: root.shellController
                                        paletteMap: root.paletteMap
                                        metricsMap: root.metricsMap
                                        onFocusRequested: embeddedPreviewInteractionRoot.forceActiveFocus()
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: metric("statusHeight", 28)
            Layout.minimumHeight: metric("statusHeight", 28)
            Layout.maximumHeight: metric("statusHeight", 28)

            WindowContainer {
                anchors.fill: parent
                window: controller.statusWindow
                Component.onCompleted: controller.syncStatusSurfaceSize(width, height)
                onWidthChanged: controller.syncStatusSurfaceSize(width, height)
                onHeightChanged: controller.syncStatusSurfaceSize(width, height)
            }
        }
    }

    Window {
        id: fullscreenPreviewWindow

        visibility: controller.previewFullscreen ? Window.FullScreen : Window.Hidden
        color: "black"
        title: root.title
        flags: Qt.Window | Qt.FramelessWindowHint

        Shortcut {
            sequence: "Esc"
            enabled: controller.previewFullscreen
            context: Qt.ApplicationShortcut
            onActivated: controller.previewFullscreen = false
        }

        Shortcut {
            sequence: "Space"
            enabled: controller.previewFullscreen
            context: Qt.ApplicationShortcut
            onActivated: controller.togglePreviewPlayback()
        }

        Shortcut {
            sequence: "Esc"
            enabled: controller.previewFullscreen
            context: Qt.WindowShortcut
            onActivated: controller.previewFullscreen = false
        }

        Shortcut {
            sequence: "Space"
            enabled: controller.previewFullscreen
            context: Qt.WindowShortcut
            onActivated: controller.togglePreviewPlayback()
        }

        onClosing: function(close) {
            close.accepted = false
            controller.previewFullscreen = false
        }

        onVisibleChanged: {
            logPreviewSurfaceTransition("preview_surface_fullscreen_window_visible")
            if (!visible) {
                if (!controller.previewFullscreen)
                    armEmbeddedInlineSurface("fullscreen_window_hidden")
                root.requestActivate()
                embeddedPreviewInteractionRoot.forceActiveFocus()
                Qt.callLater(function() {
                    styleBridge.refreshNow()
                    controller.refresh()
                })
                return
            }
            if (controller.previewFullscreen)
                armFullscreenInlineSurface("fullscreen_window_visible")
            requestActivate()
        }

        Rectangle {
            anchors.fill: parent
            color: "black"

            Loader {
                anchors.fill: parent
                active: controller.previewFullscreen

                sourceComponent: Item {
                    anchors.fill: parent

                    Loader {
                        id: fullscreenInlineSurfaceLoader
                        anchors.fill: parent
                        active: controller.previewFullscreen
                            && fullscreenInlineSurfaceActive
                            && !controller.previewUsesSeparateSurface
                        onStatusChanged: logPreviewSurfaceTransition(
                            "preview_surface_loader_status",
                            "loader=fullscreen_inline status=" + previewLoaderStatusName(status)
                                + " active=" + (active ? 1 : 0)
                        )

                        sourceComponent: QuickShellPreviewSurface {
                            runtime: controller.previewRuntime
                            mediaHost: controller.previewStageMediaHost
                            logger: controller
                            surfaceRole: "fullscreen_inline"
                        }
                    }

                    Loader {
                        anchors.fill: parent
                        active: controller.previewFullscreen && controller.previewUsesSeparateSurface

                        sourceComponent: WindowContainer {
                            anchors.fill: parent
                            window: controller.previewCompositeWindow
                        }
                    }
                }
            }
        }

        Item {
            id: fullscreenInteractionRoot
            anchors.fill: parent
            focus: controller.previewFullscreen

            Keys.onPressed: function(event) {
                if (!event.isAutoRepeat && event.modifiers === Qt.NoModifier && event.key === Qt.Key_Escape) {
                    event.accepted = true
                    controller.previewFullscreen = false
                    return
                }
                root.handlePreviewSeekPress(event)
            }

            Keys.onReleased: root.handlePreviewSeekRelease(event)
        }

        MouseArea {
            id: fullscreenHoverArea
            z: 10
            anchors.fill: parent
            hoverEnabled: true
            onExited: {
                fullscreenHoveringRevealZone = false
                if (!fullscreenHoveringControls)
                    fullscreenControlsHideTimer.restart()
            }
            onPressed: {
                fullscreenPreviewWindow.requestActivate()
                fullscreenInteractionRoot.forceActiveFocus()
                showFullscreenControls()
            }
            onPositionChanged: {
                fullscreenHoveringRevealZone =
                    mouseY >= height - metric("fullscreenControlsRevealHotzoneHeight", 120)
                if (fullscreenHoveringRevealZone)
                    showFullscreenControls()
                else if (!fullscreenHoveringControls)
                    fullscreenControlsHideTimer.restart()
            }
        }

        Rectangle {
            id: fullscreenControlsOverlay

            z: 20
            width: Math.min(
                metric("fullscreenOverlayMaxWidth", 10000),
                Math.max(0, fullscreenPreviewWindow.width - metric("fullscreenOverlaySideMargin", 18) * 2)
            )
            height: fullscreenTransport.implicitHeight
            x: metric("fullscreenOverlaySideMargin", 18)
            y: fullscreenPreviewWindow.height - height
                - 6
                + (fullscreenControlsVisible ? 0 : metric("fullscreenOverlayHideOffset", 20))
            opacity: fullscreenControlsVisible ? 1.0 : 0.0
            visible: controller.previewFullscreen && (fullscreenControlsVisible || opacity > 0.0)
            color: "transparent"

            Behavior on y {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }

            Behavior on opacity {
                NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
            }

            HoverHandler {
                id: fullscreenControlsHoverHandler
                acceptedDevices: PointerDevice.Mouse
                onHoveredChanged: {
                    fullscreenHoveringControls = hovered
                    if (hovered) {
                        fullscreenControlsVisible = true
                        fullscreenControlsHideTimer.stop()
                    } else if (!fullscreenHoveringRevealZone) {
                        fullscreenControlsHideTimer.restart()
                    }
                }
            }

            QuickShellPreviewTransport {
                id: fullscreenTransport
                anchors.fill: parent
                controller: root.shellController
                paletteMap: root.paletteMap
                metricsMap: root.metricsMap
                speedMenu: previewSpeedMenu
                fullscreenMode: true
                onFocusRequested: {
                    fullscreenPreviewWindow.requestActivate()
                    fullscreenInteractionRoot.forceActiveFocus()
                    showFullscreenControls()
                }
                onScrubActivityChanged: function(active) {
                    if (active) {
                        fullscreenControlsVisible = true
                        fullscreenControlsHideTimer.stop()
                        return
                    }
                    showFullscreenControls()
                }
            }
        }

        Rectangle {
            id: fullscreenHintOverlay

            z: 21
            anchors.top: parent.top
            anchors.topMargin: metric("fullscreenHintTopMargin", 28)
            anchors.horizontalCenter: parent.horizontalCenter
            radius: 12
            color: "#B0000000"
            border.color: "#40FFFFFF"
            visible: controller.previewFullscreen && fullscreenHintVisible

            Label {
                id: fullscreenHintLabel
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                anchors.topMargin: 8
                anchors.bottomMargin: 8
                text: qsTr("Press Esc to exit fullscreen")
                color: "white"
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }

        Rectangle {
            id: fullscreenPreviewSpeedToast

            z: 22
            anchors.centerIn: parent
            width: Math.min(220, Math.max(180, fullscreenPreviewSpeedToastColumn.implicitWidth + 48))
            height: Math.max(96, fullscreenPreviewSpeedToastColumn.implicitHeight + 36)
            radius: 18
            color: "#CC000000"
            border.width: 0
            opacity: controller.previewFullscreen && root.previewSpeedToastVisible ? 1.0 : 0.0
            visible: controller.previewFullscreen && (root.previewSpeedToastVisible || opacity > 0.0)
            enabled: false

            Behavior on opacity {
                NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
            }

            Column {
                id: fullscreenPreviewSpeedToastColumn
                anchors.centerIn: parent
                spacing: 6

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "\u5f53\u524d\u500d\u901f"
                    color: "#F8FAFC"
                    font.pixelSize: 14
                    font.weight: Font.DemiBold
                    horizontalAlignment: Text.AlignHCenter
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.previewSpeedToastPercentText
                    color: "#F8FAFC"
                    font.pixelSize: 28
                    font.weight: Font.Bold
                    horizontalAlignment: Text.AlignHCenter
                }
            }
        }
    }
}
