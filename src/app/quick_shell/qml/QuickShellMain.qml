import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window
import MiaCode.Preview

ApplicationWindow {
    id: root

    property var paletteMap: styleBridge.palette
    property var metricsMap: styleBridge.metrics
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

    function metric(key, fallback) {
        return metricsMap && metricsMap[key] !== undefined ? metricsMap[key] : fallback
    }

    function tone(key, fallback) {
        return paletteMap && paletteMap[key] !== undefined ? paletteMap[key] : fallback
    }

    function showFullscreenControls() {
        if (!controller.previewFullscreen)
            return
        fullscreenControlsVisible = true
        fullscreenControlsHideTimer.restart()
    }

    function previewPaneHandleWidth() {
        return metric("previewSplitterHandleWidth", 6)
    }

    function previewPaneMinWidth() {
        return metric("previewPanelMinWidth", 320)
    }

    function previewPaneAvailableWidth(totalWidth) {
        return Math.max(0, totalWidth - previewPaneHandleWidth())
    }

    function previewPaneMaxWidth(totalWidth, totalHeight) {
        const minWidth = previewPaneMinWidth()
        const leftMinWidth = metric("leftColumnMinWidth", 320)
        const maxByWindow = Math.max(
            minWidth,
            Math.floor(totalWidth - leftMinWidth - previewPaneHandleWidth())
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
        const leftMinWidth = metric("leftColumnMinWidth", 320)
        return Math.max(0, previewPaneAvailableWidth(totalWidth) - leftMinWidth)
    }

    function previewPaneInitialWidth(totalWidth, totalHeight) {
        const leftMinWidth = metric("leftColumnMinWidth", 320)
        const availableWidth = previewPaneAvailableWidth(totalWidth)
        const initialLeftWidth = Math.min(
            availableWidth,
            Math.max(leftMinWidth, Math.floor(availableWidth / 2))
        )
        return Math.max(0, availableWidth - initialLeftWidth)
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

    function syncPreviewPaneWidth(totalWidth, totalHeight, preserveUserChoice) {
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
        if (!preserveUserChoice || !previewPaneUserResized) {
            previewPaneWidth = previewPaneStartupBalancePending
                ? defaultWidth
                : clampPreviewPaneWidth(defaultWidth, totalWidth, totalHeight)
            if (!preserveUserChoice)
                previewPaneUserResized = false
            previewPaneStartupBalancePending = false
            return
        }
        if (previewPaneWidth <= 0) {
            previewPaneWidth = previewPaneStartupBalancePending
                ? defaultWidth
                : clampPreviewPaneWidth(defaultWidth, totalWidth, totalHeight)
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
        applyWindowMinimumSize()
        syncPreviewPaneWidth(workspaceRow.width, workspaceRow.height, true)
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
        target: controller

        function onPreviewFullscreenChanged() {
            if (!controller.previewFullscreen) {
                fullscreenControlsVisible = false
                fullscreenHintVisible = false
                fullscreenControlsHideTimer.stop()
                fullscreenHintHideTimer.stop()
                return
            }
            fullscreenControlsVisible = true
            fullscreenHintVisible = true
            fullscreenControlsHideTimer.restart()
            fullscreenHintHideTimer.restart()
        }
    }

    Timer {
        id: fullscreenControlsHideTimer
        interval: metric("fullscreenControlsAutoHideDelayMs", 1600)
        repeat: false
        onTriggered: fullscreenControlsVisible = false
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

        MenuItem { text: "0.25x"; onTriggered: controller.setPreviewRate(0.25) }
        MenuItem { text: "0.5x"; onTriggered: controller.setPreviewRate(0.5) }
        MenuItem { text: "0.75x"; onTriggered: controller.setPreviewRate(0.75) }
        MenuItem { text: "1x"; onTriggered: controller.setPreviewRate(1.0) }
        MenuItem { text: "1.25x"; onTriggered: controller.setPreviewRate(1.25) }
        MenuItem { text: "2x"; onTriggered: controller.setPreviewRate(2.0) }
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
            layoutDirection: controller.workspacePanelsSwapped ? Qt.RightToLeft : Qt.LeftToRight

            onWidthChanged: syncPreviewPaneWidth(width, height, true)
            onHeightChanged: syncPreviewPaneWidth(width, height, true)

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
                Layout.preferredWidth: previewPaneWidth
                Layout.minimumWidth: previewPaneMinWidth()
                Layout.maximumWidth: previewPaneMaxWidth(workspaceRow.width, workspaceRow.height)
                Layout.fillHeight: true
                color: tone("panelBg", "#f5f7fa")
                border.color: tone("border", "#d5e0ec")

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

                            QuickShellPreviewSurface {
                                anchors.fill: parent
                                anchors.margins: 1
                                runtime: controller.previewRuntime
                                mediaHost: controller.previewStageMediaHost
                            }
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.preferredHeight: metric("previewControlsHeight", 220)
                        Layout.minimumHeight: metric("previewControlsHeight", 220)

                        WindowContainer {
                            anchors.fill: parent
                            window: controller.previewControlsWindow
                            Component.onCompleted: controller.syncPreviewControlsSurfaceSize(width, height)
                            onWidthChanged: controller.syncPreviewControlsSurfaceSize(width, height)
                            onHeightChanged: controller.syncPreviewControlsSurfaceSize(width, height)
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

        visible: controller.previewFullscreen
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
            if (!visible)
                return
            requestActivate()
            fullscreenInteractionRoot.forceActiveFocus()
            showFullscreenControls()
        }

        Item {
            id: fullscreenInteractionRoot
            anchors.fill: parent
            focus: controller.previewFullscreen

            Keys.onEscapePressed: function(event) {
                event.accepted = true
                controller.previewFullscreen = false
            }

            Keys.onSpacePressed: function(event) {
                event.accepted = true
                controller.togglePreviewPlayback()
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onPressed: {
                fullscreenPreviewWindow.requestActivate()
                fullscreenInteractionRoot.forceActiveFocus()
                showFullscreenControls()
            }
            onPositionChanged: {
                if (mouseY >= height - metric("fullscreenControlsRevealHotzoneHeight", 120))
                    showFullscreenControls()
            }
        }

        Rectangle {
            anchors.fill: parent
            color: "black"

            Loader {
                anchors.fill: parent
                active: controller.previewFullscreen

                sourceComponent: Item {
                    anchors.fill: parent

                    QuickShellPreviewSurface {
                        anchors.fill: parent
                        runtime: controller.previewRuntime
                        mediaHost: controller.previewStageMediaHost
                    }
                }
            }
        }
    }

    Window {
        id: fullscreenControlsWindow

        transientParent: fullscreenPreviewWindow
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.NoDropShadowWindowHint | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
        width: Math.min(
            metric("fullscreenOverlayMaxWidth", 10000),
            Math.max(0, fullscreenPreviewWindow.width - metric("fullscreenOverlaySideMargin", 18) * 2)
        )
        height: Math.max(
            metric("previewControlButtonMinHeight", 28) + 16,
            fullscreenControlsCard.implicitHeight
        )
        x: fullscreenPreviewWindow.x + metric("fullscreenOverlaySideMargin", 18)
        y: fullscreenPreviewWindow.y + fullscreenPreviewWindow.height - height
            - metric("fullscreenOverlayBottomMargin", 24)
            + (fullscreenControlsVisible ? 0 : metric("fullscreenOverlayHideOffset", 20))
        opacity: fullscreenControlsVisible ? 1.0 : 0.0
        visible: controller.previewFullscreen && (fullscreenControlsVisible || opacity > 0.0)

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

        onVisibleChanged: {
            if (visible)
                fullscreenPreviewWindow.requestActivate()
        }

        Behavior on y {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        Behavior on opacity {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        Rectangle {
            id: fullscreenControlsCard
            implicitHeight: fullscreenControlsRow.implicitHeight + 16
            anchors.fill: parent
            color: tone("cardAltBg", "#edf2f8")
            border.color: tone("border", "#d5e0ec")
            radius: 10

            RowLayout {
                id: fullscreenControlsRow
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8

                ToolButton {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: metric("previewControlButtonMinHeight", 28)
                    text: "\u25a0"
                    onClicked: controller.stopPreview()
                    padding: 0
                    background: Rectangle {
                        color: parent.down ? tone("menuHoverBg", "#eef5ff") : "transparent"
                        border.color: tone("border", "#d5e0ec")
                        radius: 6
                    }
                    contentItem: Item {
                        implicitWidth: 18
                        implicitHeight: 18

                        Rectangle {
                            width: 10
                            height: 10
                            radius: 1
                            color: tone("textPrimary", "#203040")
                            anchors.centerIn: parent
                        }
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: metric("previewControlButtonMinHeight", 28)
                    text: controller.previewPlaying ? "\u23f8" : "\u25b6"
                    onClicked: controller.togglePreviewPlayback()
                    padding: 0
                    background: Rectangle {
                        color: parent.down ? tone("menuHoverBg", "#eef5ff") : "transparent"
                        border.color: tone("border", "#d5e0ec")
                        radius: 6
                    }
                    contentItem: Item {
                        implicitWidth: 18
                        implicitHeight: 18

                        Canvas {
                            anchors.fill: parent
                            visible: !controller.previewPlaying
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = tone("textPrimary", "#203040")
                                ctx.beginPath()
                                ctx.moveTo(width * 0.34, height * 0.22)
                                ctx.lineTo(width * 0.34, height * 0.78)
                                ctx.lineTo(width * 0.76, height * 0.50)
                                ctx.closePath()
                                ctx.fill()
                            }
                        }

                        Row {
                            anchors.centerIn: parent
                            spacing: 3
                            visible: controller.previewPlaying

                            Rectangle {
                                width: 4
                                height: 12
                                radius: 1
                                color: tone("textPrimary", "#203040")
                            }

                            Rectangle {
                                width: 4
                                height: 12
                                radius: 1
                                color: tone("textPrimary", "#203040")
                            }
                        }
                    }
                }

                Slider {
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(controller.previewDurationSeconds, 0.001)
                    value: controller.previewPositionSeconds
                    onMoved: controller.seekPreview(value)
                }

                Button {
                    text: controller.previewSpeedLabel
                    Layout.preferredWidth: metric("previewSpeedButtonWidth", 72)
                    onClicked: previewSpeedMenu.popup()
                    background: Rectangle {
                        color: parent.down ? tone("menuHoverBg", "#eef5ff") : "transparent"
                        border.color: tone("border", "#d5e0ec")
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: tone("textPrimary", "#203040")
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: metric("previewControlButtonMinHeight", 28)
                    text: "\u26f6"
                    onClicked: controller.previewFullscreen = false
                    padding: 0
                    background: Rectangle {
                        color: parent.down ? tone("menuHoverBg", "#eef5ff") : "transparent"
                        border.color: tone("border", "#d5e0ec")
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        color: tone("textPrimary", "#203040")
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }

    Window {
        id: fullscreenHintWindow

        transientParent: fullscreenPreviewWindow
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.NoDropShadowWindowHint | Qt.WindowDoesNotAcceptFocus
        color: "transparent"
        width: fullscreenHintLabel.implicitWidth
        height: fullscreenHintLabel.implicitHeight
        x: fullscreenPreviewWindow.x + Math.round((fullscreenPreviewWindow.width - width) / 2)
        y: fullscreenPreviewWindow.y + metric("fullscreenHintTopMargin", 28)
        visible: controller.previewFullscreen && fullscreenHintVisible

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

        Rectangle {
            anchors.fill: parent
            radius: 12
            color: "#B0000000"
            border.color: "#40FFFFFF"

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
