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
    property real pendingStartupLayoutWidth: 0
    property real pendingStartupLayoutHeight: 0
    property real startupMinimumWindowWidth: 960
    property real startupMinimumWindowHeight: 640
    property bool embeddedSeparateSurfaceReady: true
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

    function previewPaneMaxWidth(totalWidth, totalHeight) {
        const minWidth = previewPaneMinWidth()
        const textMinWidth = contentPaneMinWidth()
        const maxByWindow = Math.max(
            minWidth,
            Math.floor(totalWidth - sidebarPaneWidth() - textMinWidth - previewPaneHandleWidth())
        )
        const maxBySquare = Math.max(minWidth, Math.floor(totalHeight))
        return Math.max(
            minWidth,
            Math.min(metric("previewPanelMaxWidth", 900), maxByWindow, maxBySquare)
        )
    }

    function clampPreviewPaneWidth(candidate, totalWidth, totalHeight) {
        const minWidth = previewPaneMinWidth()
        const maxWidth = previewPaneMaxWidth(totalWidth, totalHeight)
        if (maxWidth <= minWidth)
            return minWidth
        return Math.max(minWidth, Math.min(candidate, maxWidth))
    }

    function previewPaneDefaultWidth(totalWidth, totalHeight) {
        return Math.max(0, previewPaneAvailableWidth(totalWidth) - contentPaneMinWidth())
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

    visible: false
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

        function onPreviewFullscreenChanged() {
            if (!controller.previewFullscreen) {
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

                WindowContainer {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    window: controller.workspaceWindow
                    Component.onCompleted: controller.syncWorkspaceSurfaceSize(width, height)
                    onWidthChanged: controller.syncWorkspaceSurfaceSize(width, height)
                    onHeightChanged: controller.syncWorkspaceSurfaceSize(width, height)
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

                                    readonly property real canvasSide: Math.max(1, Math.min(parent.width, parent.height))

                                    visible: !controller.previewFullscreen
                                    width: canvasSide
                                    height: canvasSide
                                    anchors.centerIn: parent
                                    color: tone("canvasBg", "#000000")
                                    border.color: tone("borderSoft", "#ccd6e2")
                                    clip: true

                                    Loader {
                                        anchors.fill: parent
                                        anchors.margins: 1
                                        active: !controller.previewFullscreen
                                            && (!controller.previewUsesSeparateSurface
                                                || !embeddedSeparateSurfaceReady)

                                        sourceComponent: QuickShellPreviewSurface {
                                            runtime: controller.previewRuntime
                                            mediaHost: controller.previewStageMediaHost
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
            if (!visible) {
                root.requestActivate()
                embeddedPreviewInteractionRoot.forceActiveFocus()
                Qt.callLater(function() {
                    styleBridge.refreshNow()
                    controller.refresh()
                })
                return
            }
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
                        anchors.fill: parent
                        active: controller.previewFullscreen && !controller.previewUsesSeparateSurface

                        sourceComponent: QuickShellPreviewSurface {
                            runtime: controller.previewRuntime
                            mediaHost: controller.previewStageMediaHost
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
    }
}
