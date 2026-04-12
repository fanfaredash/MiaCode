import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var controller: null
    property var paletteMap: ({})
    property var metricsMap: ({})
    property var speedMenu: null
    property bool fullscreenMode: false

    signal focusRequested()
    signal scrubActivityChanged(bool active)
    signal paletteRefreshRequested()

    readonly property int transportTextPixelSize: 13
    readonly property int transportTextWeight: Font.Medium

    function metric(key, fallback) {
        return metricsMap && metricsMap[key] !== undefined ? metricsMap[key] : fallback
    }

    function tone(key, fallback) {
        return paletteMap && paletteMap[key] !== undefined ? paletteMap[key] : fallback
    }

    function formatDisplayTime(secondsValue) {
        const safeSeconds = Math.max(0, Math.floor(secondsValue + 0.0001))
        const minutes = Math.floor(safeSeconds / 60)
        const seconds = safeSeconds % 60
        return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0")
    }

    function openSpeedMenu() {
        focusRequested()
        if (speedMenu)
            speedMenu.popup()
    }

    function showPreciseHint(secondValue) {
        preciseHintSecond = secondValue
        preciseHintVisible = true
        preciseHintHideTimer.restart()
    }

    function tooltipGlobalX() {
        if (!preciseHintWindow.visible)
            return 0
        const handlePoint = transportSlider.mapToGlobal(
            transportSlider.leftPadding + transportSlider.visualPosition * transportSlider.availableWidth,
            0
        )
        return Math.round(handlePoint.x - preciseHintWindow.width / 2)
    }

    function tooltipGlobalY() {
        if (!preciseHintWindow.visible)
            return 0
        const handlePoint = transportSlider.mapToGlobal(
            transportSlider.leftPadding + transportSlider.visualPosition * transportSlider.availableWidth,
            0
        )
        return Math.round(handlePoint.y - preciseHintWindow.height - 14)
    }

    readonly property real durationSeconds: controller ? Math.max(controller.previewDurationSeconds, 0.001) : 0.001
    readonly property real positionSeconds: controller ? controller.previewPositionSeconds : 0
    readonly property real displayedSeconds: transportSlider.pressed ? transportSlider.value : positionSeconds
    readonly property string timeSummary: formatDisplayTime(displayedSeconds) + " / " + formatDisplayTime(durationSeconds)
    readonly property int controlButtonHeight: metric("previewControlButtonMinHeight", 28)
    property real preciseHintSecond: positionSeconds
    property bool preciseHintVisible: false

    implicitHeight: transportLayout.implicitHeight + 16
    radius: metric("previewControlCardRadius", 10)
    color: fullscreenMode ? tone("cardAltBg", "#edf2f8") : tone("cardBg", "#ffffff")
    border.color: fullscreenMode ? "transparent" : tone("border", "#d5e0ec")

    Timer {
        id: preciseHintHideTimer
        interval: 600
        repeat: false
        onTriggered: root.preciseHintVisible = false
    }

    Window {
        id: preciseHintWindow

        transientParent: root.Window.window
        flags: Qt.ToolTip
            | Qt.FramelessWindowHint
            | Qt.WindowStaysOnTopHint
            | Qt.WindowDoesNotAcceptFocus
            | Qt.WindowTransparentForInput
            | Qt.NoDropShadowWindowHint
        color: "transparent"
        visible: root.preciseHintVisible && !root.fullscreenMode
        width: preciseHintBubble.implicitWidth
        height: preciseHintBubble.implicitHeight
        x: root.tooltipGlobalX()
        y: root.tooltipGlobalY()

        Rectangle {
            id: preciseHintBubble

            implicitWidth: preciseHintLabel.implicitWidth + 16
            implicitHeight: preciseHintLabel.implicitHeight + 8
            anchors.fill: parent
            radius: 8
            color: "#CC10151C"
            border.color: "#40FFFFFF"

            Label {
                id: preciseHintLabel
                anchors.centerIn: parent
                text: controller ? controller.formatPreviewTimestamp(root.preciseHintSecond) : "00:00.00"
                color: "white"
            }
        }
    }

    TextMetrics {
        id: speedTextMetrics
        text: controller ? controller.previewSpeedLabel : "1x"
        font.pixelSize: root.transportTextPixelSize
        font.weight: root.transportTextWeight
    }

    Binding {
        target: transportSlider
        property: "value"
        value: root.positionSeconds
        when: !transportSlider.pressed
    }

    ColumnLayout {
        id: transportLayout
        anchors.fill: parent
        anchors.margins: fullscreenMode ? 14 : 8
        spacing: metric("previewStatsVerticalSpacing", 6)

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: fullscreenMode ? 32 : 24

            Slider {
                id: transportSlider

                anchors.fill: parent
                from: 0
                to: root.durationSeconds
                live: true
                focusPolicy: Qt.NoFocus

                onPressedChanged: {
                    root.focusRequested()
                    if (pressed) {
                        root.preciseHintSecond = value
                        root.preciseHintVisible = true
                        root.scrubActivityChanged(true)
                        if (controller)
                            controller.beginPreviewScrub()
                        return
                    }
                    root.preciseHintSecond = value
                    root.showPreciseHint(value)
                    root.scrubActivityChanged(false)
                    if (controller)
                        controller.endPreviewScrub(value, true)
                }

                onMoved: {
                    root.focusRequested()
                    root.preciseHintSecond = value
                    root.preciseHintVisible = true
                    if (controller)
                        controller.updatePreviewScrub(value, true)
                }

                background: Item {
                    x: transportSlider.leftPadding
                    y: Math.round((parent.height - 6) / 2)
                    width: transportSlider.availableWidth
                    height: 6

                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: tone("inputDisabledBg", "#e3e8ef")
                    }

                    Rectangle {
                        width: Math.max(6, transportSlider.visualPosition * parent.width)
                        height: parent.height
                        radius: height / 2
                        color: tone("accent", "#2e77d0")
                    }
                }

                handle: Rectangle {
                    x: transportSlider.leftPadding
                        + transportSlider.visualPosition * (transportSlider.availableWidth - width)
                    y: Math.round((transportSlider.height - height) / 2)
                    width: 18
                    height: 18
                    radius: 9
                    color: "white"
                    border.color: root.tone("borderSoft", "#ccd6e2")
                    border.width: 1
                }
            }

            Rectangle {
                visible: root.preciseHintVisible && root.fullscreenMode
                z: 3
                radius: 8
                color: "#CC10151C"
                border.color: "#40FFFFFF"
                anchors.bottom: transportSlider.top
                anchors.bottomMargin: 6
                implicitWidth: fullscreenPreciseHintLabel.implicitWidth + 16
                implicitHeight: fullscreenPreciseHintLabel.implicitHeight + 8
                x: Math.max(
                    0,
                    Math.min(
                        parent.width - width,
                        transportSlider.leftPadding
                            + transportSlider.visualPosition * transportSlider.availableWidth
                            - width / 2
                    )
                )

                Label {
                    id: fullscreenPreciseHintLabel
                    anchors.centerIn: parent
                    text: controller ? controller.formatPreviewTimestamp(root.preciseHintSecond) : "00:00.00"
                    color: "white"
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            RowLayout {
                Layout.alignment: Qt.AlignLeft
                spacing: 8

                ToolButton {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: root.controlButtonHeight
                    focusPolicy: Qt.NoFocus
                    padding: 0
                    onPressed: root.focusRequested()
                    onClicked: {
                        root.focusRequested()
                        if (controller)
                            controller.stopPreview()
                    }
                    background: Rectangle {
                        color: parent.down ? root.tone("menuHoverBg", "#eef5ff") : "transparent"
                        border.color: root.tone("border", "#d5e0ec")
                        radius: 6
                    }
                    contentItem: Item {
                        implicitWidth: 18
                        implicitHeight: 18

                        Rectangle {
                            width: 10
                            height: 10
                            radius: 1
                            color: root.tone("textPrimary", "#203040")
                            anchors.centerIn: parent
                        }
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: root.controlButtonHeight
                    focusPolicy: Qt.NoFocus
                    padding: 0
                    onPressed: root.focusRequested()
                    onClicked: {
                        root.focusRequested()
                        if (controller)
                            controller.togglePreviewPlayback()
                    }
                    background: Rectangle {
                        color: parent.down ? root.tone("menuHoverBg", "#eef5ff") : "transparent"
                        border.color: root.tone("border", "#d5e0ec")
                        radius: 6
                    }
                    contentItem: Item {
                        implicitWidth: 18
                        implicitHeight: 18

                        Canvas {
                            id: playIconCanvas
                            anchors.fill: parent
                            visible: !(controller && controller.previewPlaying)
                            onVisibleChanged: requestPaint()
                            onPaint: {
                                const ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = root.tone("textPrimary", "#203040")
                                ctx.beginPath()
                                ctx.moveTo(width * 0.39, height * 0.28)
                                ctx.lineTo(width * 0.39, height * 0.72)
                                ctx.lineTo(width * 0.71, height * 0.50)
                                ctx.closePath()
                                ctx.fill()
                            }

                            Connections {
                                target: root

                                function onPaletteRefreshRequested() {
                                    playIconCanvas.requestPaint()
                                }
                            }
                        }

                        Row {
                            anchors.centerIn: parent
                            spacing: 3
                            visible: !!(controller && controller.previewPlaying)

                            Rectangle {
                                width: 4
                                height: 12
                                radius: 1
                                color: root.tone("textPrimary", "#203040")
                            }

                            Rectangle {
                                width: 4
                                height: 12
                                radius: 1
                                color: root.tone("textPrimary", "#203040")
                            }
                        }
                    }
                }

                Label {
                    text: root.timeSummary
                    color: root.tone("textPrimary", "#203040")
                    font.pixelSize: root.transportTextPixelSize
                    font.weight: root.transportTextWeight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Item { Layout.fillWidth: true }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                spacing: 8

                Button {
                    text: controller ? controller.previewSpeedLabel : "1x"
                    Layout.preferredWidth: root.metric("previewSpeedButtonWidth", 72)
                    Layout.minimumWidth: root.metric("previewSpeedButtonWidth", 72)
                    Layout.maximumWidth: root.metric("previewSpeedButtonWidth", 72)
                    Layout.preferredHeight: root.controlButtonHeight
                    Layout.minimumHeight: root.controlButtonHeight
                    Layout.maximumHeight: root.controlButtonHeight
                    focusPolicy: Qt.NoFocus
                    padding: 0
                    onPressed: root.focusRequested()
                    onClicked: root.openSpeedMenu()
                    background: Rectangle {
                        color: parent.down ? root.tone("menuHoverBg", "#eef5ff") : "transparent"
                        border.color: root.tone("border", "#d5e0ec")
                        radius: 6
                    }
                    contentItem: Text {
                        text: parent.text
                        anchors.fill: parent
                        anchors.leftMargin: 9
                        anchors.rightMargin: 9
                        color: root.tone("textPrimary", "#203040")
                        font.pixelSize: root.transportTextPixelSize
                        font.weight: root.transportTextWeight
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                    }
                }

                ToolButton {
                    Layout.preferredWidth: 30
                    Layout.preferredHeight: root.controlButtonHeight
                    focusPolicy: Qt.NoFocus
                    padding: 0
                    onPressed: root.focusRequested()
                    onClicked: {
                        root.focusRequested()
                        if (controller)
                            controller.previewFullscreen = !fullscreenMode
                    }
                    background: Rectangle {
                        color: parent.down ? root.tone("menuHoverBg", "#eef5ff") : "transparent"
                        border.color: root.tone("border", "#d5e0ec")
                        radius: 6
                    }
                    contentItem: Text {
                        text: "\u26f6"
                        color: root.tone("textPrimary", "#203040")
                        font.pixelSize: 16
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }
    }
}
