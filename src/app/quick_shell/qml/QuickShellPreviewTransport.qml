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
    property bool inputBlocked: false

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

    function transportSurfaceColor() {
        return fullscreenMode ? "#C8141B22" : tone("cardBg", "#ffffff")
    }

    function transportBorderColor() {
        return fullscreenMode ? "#3AFFFFFF" : tone("border", "#d5e0ec")
    }

    function transportPrimaryTextColor() {
        return fullscreenMode ? "#F2F7FF" : tone("textPrimary", "#203040")
    }

    function transportTrackColor() {
        return fullscreenMode ? "#34404D" : tone("inputDisabledBg", "#e3e8ef")
    }

    function transportHandleFillColor() {
        return fullscreenMode ? "#F7FBFF" : "white"
    }

    function transportHandleBorderColor() {
        return fullscreenMode ? "#5D748E" : tone("borderSoft", "#ccd6e2")
    }

    function transportButtonFillColor(down) {
        if (!fullscreenMode)
            return down ? tone("menuHoverBg", "#eef5ff") : "transparent"
        return down ? "#2A3542" : "#1A222B"
    }

    function transportButtonBorderColor() {
        return fullscreenMode ? "#40FFFFFF" : tone("border", "#d5e0ec")
    }

    // Single source of truth for the on-screen handle centre (slider-local x).
    // Handle, fill, tooltip and the seek target all derive from displayedProgress
    // so the visible thumb can never disagree with where the seek actually lands.
    function handleCenterX() {
        return transportSlider.leftPadding
            + root.displayedProgress * (transportSlider.availableWidth - root.handleDiameter)
            + root.handleDiameter / 2
    }

    function tooltipGlobalX() {
        if (!preciseHintWindow.visible)
            return 0
        const handlePoint = transportSlider.mapToGlobal(
            root.handleCenterX(),
            0
        )
        return Math.round(handlePoint.x - preciseHintWindow.width / 2)
    }

    function tooltipGlobalY() {
        if (!preciseHintWindow.visible)
            return 0
        const handlePoint = transportSlider.mapToGlobal(
            root.handleCenterX(),
            0
        )
        return Math.round(handlePoint.y - preciseHintWindow.height - 14)
    }

    readonly property real durationSeconds: controller ? Math.max(controller.previewDurationSeconds, 0.001) : 0.001
    readonly property real positionSeconds: controller ? controller.previewPositionSeconds : 0
    readonly property real displayedSeconds: scrubActive ? activeScrubSecond : positionSeconds
    readonly property real displayedProgress: Math.max(0, Math.min(1, displayedSeconds / durationSeconds))
    readonly property string timeSummary: formatDisplayTime(displayedSeconds) + " / " + formatDisplayTime(durationSeconds)
    readonly property int controlButtonHeight: metric("previewControlButtonMinHeight", 28)
    // Visual handle diameter (kept in sync with the `handle:` Rectangle below).
    readonly property int handleDiameter: 18
    // Interactive (clickable/draggable) height of the scrubber. Deliberately
    // smaller than the row so a click clearly below the track line — but still
    // inside the row rectangle — no longer triggers a seek. Tunable; keep < the
    // handle diameter when you want the hit band to hug the track line.
    readonly property int scrubInteractiveHeight: fullscreenMode ? 18 : 14
    property real preciseHintSecond: positionSeconds
    property bool preciseHintVisible: false
    property bool scrubActive: false
    property real activeScrubSecond: positionSeconds

    implicitHeight: transportLayout.implicitHeight + 16
    radius: metric("previewControlCardRadius", 10)
    color: transportSurfaceColor()
    border.color: transportBorderColor()

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

    TextMetrics {
        id: timeSummaryTextMetrics
        text: root.timeSummary
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

                // Hit area hugs the track line instead of filling the whole row,
                // so clicks below the line (but inside the row) no longer seek.
                // The handle still renders at full size and may visually overflow
                // this band — only interaction is constrained.
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                height: root.scrubInteractiveHeight
                from: 0
                to: root.durationSeconds
                live: true
                enabled: !root.inputBlocked
                focusPolicy: Qt.NoFocus

                onPressedChanged: {
                    root.focusRequested()
                    if (pressed) {
                        root.scrubActive = true
                        root.activeScrubSecond = value
                        root.preciseHintSecond = root.activeScrubSecond
                        root.preciseHintVisible = true
                        root.scrubActivityChanged(true)
                        if (controller)
                            controller.beginPreviewScrub()
                        return
                    }
                    const releaseSecond = root.activeScrubSecond
                    root.preciseHintSecond = releaseSecond
                    root.showPreciseHint(releaseSecond)
                    root.scrubActive = false
                    root.scrubActivityChanged(false)
                    if (controller)
                        controller.endPreviewScrub(releaseSecond, true)
                }

                onMoved: {
                    root.focusRequested()
                    root.activeScrubSecond = value
                    root.preciseHintSecond = root.activeScrubSecond
                    root.preciseHintVisible = true
                    if (controller)
                        controller.updatePreviewScrub(root.activeScrubSecond, true)
                }

                background: Item {
                    x: transportSlider.leftPadding
                    y: Math.round((parent.height - 6) / 2)
                    width: transportSlider.availableWidth
                    height: 6

                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: root.transportTrackColor()
                    }

                    Rectangle {
                        width: Math.max(6, root.displayedProgress * parent.width)
                        height: parent.height
                        radius: height / 2
                        color: tone("accent", "#2e77d0")
                    }
                }

                handle: Rectangle {
                    x: transportSlider.leftPadding
                        + root.displayedProgress * (transportSlider.availableWidth - width)
                    y: Math.round((transportSlider.height - height) / 2)
                    width: 18
                    height: 18
                    radius: 9
                    color: root.transportHandleFillColor()
                    border.color: root.transportHandleBorderColor()
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
                        root.handleCenterX() - width / 2
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

        Item {
            Layout.fillWidth: true
            implicitHeight: Math.max(
                root.controlButtonHeight,
                Math.max(defaultLeftRow.implicitHeight, Math.max(defaultRightRow.implicitHeight, Math.max(swappedLeftRow.implicitHeight, swappedRightRow.implicitHeight)))
            )

            RowLayout {
                anchors.fill: parent
                spacing: 8

                RowLayout {
                    id: defaultLeftRow
                    visible: !(controller && controller.workspacePanelsSwapped)
                    Layout.alignment: Qt.AlignLeft
                    spacing: 8

                    ToolButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: root.controlButtonHeight
                        enabled: !root.inputBlocked
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        onPressed: root.focusRequested()
                        onClicked: {
                            root.focusRequested()
                            if (controller)
                                controller.stopPreview()
                        }
                        background: Rectangle {
                            color: root.transportButtonFillColor(parent.down)
                            border.color: root.transportButtonBorderColor()
                            radius: 6
                        }
                        contentItem: Item {
                            implicitWidth: 18
                            implicitHeight: 18

                            Rectangle {
                                width: 10
                                height: 10
                                radius: 1
                                color: root.transportPrimaryTextColor()
                                anchors.centerIn: parent
                            }
                        }
                    }

                    ToolButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: root.controlButtonHeight
                        enabled: !root.inputBlocked
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        onPressed: root.focusRequested()
                        onClicked: {
                            root.focusRequested()
                            if (controller)
                                controller.togglePreviewPlayback()
                        }
                        background: Rectangle {
                            color: root.transportButtonFillColor(parent.down)
                            border.color: root.transportButtonBorderColor()
                            radius: 6
                        }
                        contentItem: Item {
                            implicitWidth: 18
                            implicitHeight: 18

                            Canvas {
                                id: defaultPlayIconCanvas
                                anchors.fill: parent
                                visible: !(controller && controller.previewPlaying)
                                onVisibleChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.fillStyle = root.transportPrimaryTextColor()
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
                                        defaultPlayIconCanvas.requestPaint()
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
                                    color: root.transportPrimaryTextColor()
                                }

                                Rectangle {
                                    width: 4
                                    height: 12
                                    radius: 1
                                    color: root.transportPrimaryTextColor()
                                }
                            }
                        }
                    }

                    Label {
                        text: root.timeSummary
                        color: root.transportPrimaryTextColor()
                        font.pixelSize: root.transportTextPixelSize
                        font.weight: root.transportTextWeight
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                RowLayout {
                    id: swappedLeftRow
                    visible: controller && controller.workspacePanelsSwapped
                    Layout.alignment: Qt.AlignLeft
                    spacing: 8

                    ToolButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: root.controlButtonHeight
                        enabled: !root.inputBlocked
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        onPressed: root.focusRequested()
                        onClicked: {
                            root.focusRequested()
                            if (controller)
                                controller.previewFullscreen = !fullscreenMode
                        }
                        background: Rectangle {
                            color: root.transportButtonFillColor(parent.down)
                            border.color: root.transportButtonBorderColor()
                            radius: 6
                        }
                        contentItem: Text {
                            text: "\u26f6"
                            color: root.transportPrimaryTextColor()
                            font.pixelSize: 16
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }

                    Button {
                        text: controller ? controller.previewSpeedLabel : "1x"
                        Layout.preferredWidth: root.metric("previewSpeedButtonWidth", 72)
                        Layout.minimumWidth: root.metric("previewSpeedButtonWidth", 72)
                        Layout.maximumWidth: root.metric("previewSpeedButtonWidth", 72)
                        Layout.preferredHeight: root.controlButtonHeight
                        Layout.minimumHeight: root.controlButtonHeight
                        Layout.maximumHeight: root.controlButtonHeight
                        enabled: !root.inputBlocked
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        onPressed: root.focusRequested()
                        onClicked: root.openSpeedMenu()
                        background: Rectangle {
                            color: root.transportButtonFillColor(parent.down)
                            border.color: root.transportButtonBorderColor()
                            radius: 6
                        }
                        contentItem: Text {
                            text: parent.text
                            anchors.fill: parent
                            anchors.leftMargin: 9
                            anchors.rightMargin: 9
                            color: root.transportPrimaryTextColor()
                            font.pixelSize: root.transportTextPixelSize
                            font.weight: root.transportTextWeight
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }

                    Label {
                        Layout.preferredWidth: Math.max(
                            timeSummaryTextMetrics.advanceWidth + 2,
                            root.metric("previewTimeSummaryMinWidth", 108)
                        )
                        Layout.minimumWidth: Layout.preferredWidth
                        text: root.timeSummary
                        color: root.transportPrimaryTextColor()
                        font.pixelSize: root.transportTextPixelSize
                        font.weight: root.transportTextWeight
                        horizontalAlignment: Text.AlignLeft
                        verticalAlignment: Text.AlignVCenter
                    }

                }

                Item { Layout.fillWidth: true }

                RowLayout {
                    id: defaultRightRow
                    visible: !(controller && controller.workspacePanelsSwapped)
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
                        enabled: !root.inputBlocked
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        onPressed: root.focusRequested()
                        onClicked: root.openSpeedMenu()
                        background: Rectangle {
                            color: root.transportButtonFillColor(parent.down)
                            border.color: root.transportButtonBorderColor()
                            radius: 6
                        }
                        contentItem: Text {
                            text: parent.text
                            anchors.fill: parent
                            anchors.leftMargin: 9
                            anchors.rightMargin: 9
                            color: root.transportPrimaryTextColor()
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
                        enabled: !root.inputBlocked
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        onPressed: root.focusRequested()
                        onClicked: {
                            root.focusRequested()
                            if (controller)
                                controller.previewFullscreen = !fullscreenMode
                        }
                        background: Rectangle {
                            color: root.transportButtonFillColor(parent.down)
                            border.color: root.transportButtonBorderColor()
                            radius: 6
                        }
                        contentItem: Text {
                            text: "\u26f6"
                            color: root.transportPrimaryTextColor()
                            font.pixelSize: 16
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                RowLayout {
                    id: swappedRightRow
                    visible: controller && controller.workspacePanelsSwapped
                    Layout.alignment: Qt.AlignRight
                    spacing: 8

                    ToolButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: root.controlButtonHeight
                        enabled: !root.inputBlocked
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        onPressed: root.focusRequested()
                        onClicked: {
                            root.focusRequested()
                            if (controller)
                                controller.stopPreview()
                        }
                        background: Rectangle {
                            color: root.transportButtonFillColor(parent.down)
                            border.color: root.transportButtonBorderColor()
                            radius: 6
                        }
                        contentItem: Item {
                            implicitWidth: 18
                            implicitHeight: 18

                            Rectangle {
                                width: 10
                                height: 10
                                radius: 1
                                color: root.transportPrimaryTextColor()
                                anchors.centerIn: parent
                            }
                        }
                    }

                    ToolButton {
                        Layout.preferredWidth: 30
                        Layout.preferredHeight: root.controlButtonHeight
                        enabled: !root.inputBlocked
                        focusPolicy: Qt.NoFocus
                        padding: 0
                        onPressed: root.focusRequested()
                        onClicked: {
                            root.focusRequested()
                            if (controller)
                                controller.togglePreviewPlayback()
                        }
                        background: Rectangle {
                            color: root.transportButtonFillColor(parent.down)
                            border.color: root.transportButtonBorderColor()
                            radius: 6
                        }
                        contentItem: Item {
                            implicitWidth: 18
                            implicitHeight: 18

                            Canvas {
                                id: swappedPlayIconCanvas
                                anchors.fill: parent
                                visible: !(controller && controller.previewPlaying)
                                onVisibleChanged: requestPaint()
                                onPaint: {
                                    const ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.fillStyle = root.transportPrimaryTextColor()
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
                                        swappedPlayIconCanvas.requestPaint()
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
                                    color: root.transportPrimaryTextColor()
                                }

                                Rectangle {
                                    width: 4
                                    height: 12
                                    radius: 1
                                    color: root.transportPrimaryTextColor()
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
