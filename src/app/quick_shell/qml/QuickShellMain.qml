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

    visible: true
    width: metric("initialWindowWidth", 1280)
    height: metric("initialWindowHeight", 800)
    minimumWidth: metric("minimumWindowWidth", 960)
    minimumHeight: metric("minimumWindowHeight", 640)
    title: controller.windowTitle
    color: tone("windowBg", "#f8fafd")

    palette.window: tone("windowBg", "#f8fafd")
    palette.base: tone("inputBg", "#ffffff")
    palette.button: tone("cardBg", "#ffffff")
    palette.windowText: tone("textPrimary", "#203040")
    palette.text: tone("textPrimary", "#203040")
    palette.buttonText: tone("textPrimary", "#203040")
    palette.highlight: tone("accent", "#2e77d0")
    palette.highlightedText: tone("accentText", "#ffffff")

    Component.onCompleted: {
        styleBridge.syncWindowSize(width, height)
        controller.refresh()
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
        palette.window: tone("cardBg", "#ffffff")
        palette.base: tone("cardBg", "#ffffff")
        palette.text: tone("textPrimary", "#203040")
        palette.windowText: tone("textPrimary", "#203040")
        palette.buttonText: tone("textPrimary", "#203040")
        palette.highlight: tone("accent", "#2e77d0")
        palette.highlightedText: tone("accentText", "#ffffff")
        background: Rectangle {
            color: tone("cardBg", "#ffffff")
            border.color: tone("border", "#d5e0ec")
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
                Layout.preferredWidth: Math.max(metric("previewShellWidth", 360), 320)
                Layout.minimumWidth: Math.max(metric("previewShellWidth", 360), 320)
                Layout.maximumWidth: Math.max(metric("previewShellWidth", 360), 320)
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
                        Layout.maximumHeight: metric("previewControlsHeight", 220)

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

        onClosing: function(close) {
            close.accepted = false
            controller.previewFullscreen = false
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
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
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.NoDropShadowWindowHint
        color: "transparent"
        width: Math.min(
            metric("fullscreenOverlayMaxWidth", 10000),
            Math.max(0, fullscreenPreviewWindow.width - metric("fullscreenOverlaySideMargin", 18) * 2)
        )
        height: fullscreenControlsCard.implicitHeight
        x: fullscreenPreviewWindow.x + metric("fullscreenOverlaySideMargin", 18)
        y: fullscreenPreviewWindow.y + fullscreenPreviewWindow.height - height
            - metric("fullscreenOverlayBottomMargin", 24)
            + (fullscreenControlsVisible ? 0 : metric("fullscreenOverlayHideOffset", 20))
        opacity: fullscreenControlsVisible ? 1.0 : 0.0
        visible: controller.previewFullscreen && (fullscreenControlsVisible || opacity > 0.0)

        Behavior on y {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        Behavior on opacity {
            NumberAnimation { duration: 180; easing.type: Easing.OutCubic }
        }

        Rectangle {
            id: fullscreenControlsCard
            anchors.fill: parent
            color: tone("cardAltBg", "#edf2f8")
            border.color: tone("border", "#d5e0ec")
            radius: 10

            RowLayout {
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
                    contentItem: Text {
                        text: parent.text
                        color: tone("textPrimary", "#203040")
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: metric("previewControlButtonMinHeight", 28)
                    text: controller.previewPlaying ? "\u2161" : "\u25b6"
                    onClicked: controller.togglePreviewPlayback()
                    padding: 0
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
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: metric("previewControlButtonMinHeight", 28)
                    text: qsTr("Exit")
                    onClicked: controller.previewFullscreen = false
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
                    }
                }
            }
        }
    }

    Window {
        id: fullscreenHintWindow

        transientParent: fullscreenPreviewWindow
        flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.NoDropShadowWindowHint
        color: "transparent"
        width: fullscreenHintLabel.implicitWidth
        height: fullscreenHintLabel.implicitHeight
        x: fullscreenPreviewWindow.x + Math.round((fullscreenPreviewWindow.width - width) / 2)
        y: fullscreenPreviewWindow.y + metric("fullscreenHintTopMargin", 28)
        visible: controller.previewFullscreen && fullscreenHintVisible

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
