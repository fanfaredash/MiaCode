import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.Timeline

Item {
    id: root

    property var controller: null
    property var paletteMap: ({})
    property var metricsMap: ({})

    onPaletteMapChanged: {
        if (timelineItem)
            timelineItem.refreshTheme()
    }
    onMetricsMapChanged: {
        if (timelineItem)
            timelineItem.refreshTheme()
    }

    function zoomWrapsToMin(scale) {
        return scale >= 1.999
    }

    function tone(key, fallback) {
        return paletteMap && paletteMap[key] !== undefined ? paletteMap[key] : fallback
    }

    readonly property real headerScale: controller ? controller.bottomTabsHeaderScale : 1.0

    // Phase 9d reverted — separating the band visually didn't match
    // user expectation (buttons + line markers should occupy the
    // SAME header band, not stack vertically). The proper fix is
    // either (a) render zoom/follow buttons natively in the DComp
    // pipeline so they paint on the popup's composition plane, or
    // (b) host the QML buttons in a separate Window with its own
    // HWND so DWM can put them above the popup. Both are larger
    // changes; deferring to a follow-up Phase 9d-native.
    TimelineQuickItem {
        id: timelineItem

        anchors.fill: parent
        stateBridge: controller ? controller.timelineStateBridge : null
        headerLeftLimit: zoomButton.x + zoomButton.width + 2
        headerRightLimit: Math.max(0, settingsButton.x - 2)
        headerMarkerLeftLimit: zoomButton.x + zoomButton.width + 2
        headerMarkerRightLimit: Math.max(0, settingsButton.x - 2)

        onHeaderNavigateRequested: function(second) {
            if (controller)
                controller.timelineHeaderNavigate(second)
        }
        onTimelineWheelNavigateRequested: function(second) {
            if (controller)
                controller.timelineWheelNavigate(second)
        }
        onCenterNavigateRequested: function(second) {
            if (controller)
                controller.timelineCenterNavigate(second)
        }
        onTimelineDragStarted: {
            if (controller)
                controller.timelineDragStarted()
        }
        onTimelineDragFinished: function(second) {
            if (controller)
                controller.timelineDragFinished(second)
        }
        onTimelineUserInteractionStarted: {
            if (controller)
                controller.timelineUserInteractionStarted()
        }
        onTimelineSurfaceReady: {
            if (controller)
                controller.noteTimelineSurfaceReady()
        }
        onFollowPreviewToggled: function(enabled) {
            if (controller)
                controller.timelineFollowPreviewToggled(enabled)
        }
        onFollowProgressToggled: function(enabled) {
            if (controller)
                controller.timelineFollowProgressToggled(enabled)
        }
        onPreviewPlayPauseRequested: {
            if (controller)
                controller.togglePreviewPlayback()
        }
    }

    ToolButton {
        id: zoomButton

        anchors.left: parent.left
        anchors.leftMargin: Math.round(4 * root.headerScale)
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        width: bodyWidth + stepperWidth
        implicitHeight: Math.max(1, Math.round(22 * root.headerScale))
        property int bodyWidth: Math.max(42, Math.round(54 * root.headerScale))
        property int stepperWidth: Math.max(14, Math.round(18 * root.headerScale))
        padding: 1
        leftPadding: 2
        rightPadding: Math.round(8 * root.headerScale)
        spacing: Math.round(6 * root.headerScale)
        hoverEnabled: true
        text: Math.round(timelineItem.zoomScale * 100) + "%"
        // Phase 9d-native — invisible to the eye (DComp pipeline
        // renders the button visually in the popup composition plane)
        // but still active for input. The DComp popup HWND is
        // WS_EX_TRANSPARENT so clicks pass through to this QQuickItem.
        opacity: 0

        background: Rectangle {
            radius: 6
            color: zoomButton.down
                ? root.tone("accentPressed", "#2563eb")
                : (zoomButton.hovered
                    ? root.tone("menuHoverBg", "#334155")
                    : root.tone("cardBg", "#1f2937"))
            border.width: 1
            border.color: zoomButton.hovered && !zoomButton.down
                ? root.tone("accent", "#60a5fa")
                : root.tone("borderStrong", "#475569")
        }

        contentItem: Item {
            implicitWidth: zoomRow.implicitWidth
            implicitHeight: zoomRow.implicitHeight

            Row {
                id: zoomRow

                anchors.centerIn: parent
                spacing: zoomButton.spacing

                Canvas {
                    id: zoomGlyph

                    width: Math.max(1, Math.round(18 * root.headerScale))
                    height: Math.max(1, Math.round(18 * root.headerScale))

                    property color strokeColor: zoomButton.down
                        ? root.tone("accentText", "#ffffff")
                        : root.tone("timelineLabel", "#d4dce8")
                    property string glyph: root.zoomWrapsToMin(timelineItem.zoomScale) ? "-" : "+"

                    onStrokeColorChanged: requestPaint()
                    onGlyphChanged: requestPaint()

                    onPaint: {
                        const ctx = getContext("2d")
                        const lensCx = 6.5
                        const lensCy = 6.5
                        const lineWidth = 1.5
                        const lensR = 5.35
                        const handleStart = (lensR + lineWidth * 0.75 + 0.2) / Math.sqrt(2)
                        ctx.reset()
                        ctx.strokeStyle = strokeColor
                        ctx.fillStyle = strokeColor
                        ctx.lineWidth = lineWidth
                        ctx.lineCap = "square"
                        ctx.lineJoin = "miter"
                        ctx.beginPath()
                        ctx.ellipse(lensCx, lensCy, lensR, lensR, 0, 0, Math.PI * 2)
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.moveTo(lensCx + handleStart, lensCy + handleStart)
                        ctx.lineTo(14.05, 14.05)
                        ctx.stroke()
                        ctx.font = "bold 7pt sans-serif"
                        ctx.textAlign = "center"
                        ctx.textBaseline = "middle"
                        ctx.fillText(glyph, 15.0, 5.5)
                    }
                }

                Text {
                    text: zoomButton.text
                    color: zoomButton.down
                        ? root.tone("accentText", "#ffffff")
                        : root.tone("textPrimary", "#e5e7eb")
                    font.weight: Font.DemiBold
                    font.pixelSize: Math.max(1, Math.round(12 * root.headerScale))
                    height: zoomGlyph.height
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        MouseArea {
            z: 10
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: zoomButton.bodyWidth
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true
            onEntered: timelineItem.setZoomControlHoveredPart(1)
            onExited: timelineItem.setZoomControlHoveredPart(0)
            onPressed: timelineItem.setZoomControlPressedPart(1)
            onReleased: timelineItem.setZoomControlPressedPart(0)
            onCanceled: timelineItem.setZoomControlPressedPart(0)
            onClicked: {
                if (!controller)
                    return
                const point = zoomButton.mapToGlobal(0, 0)
                controller.openTimelineZoomMenu(
                    Math.round(point.x),
                    Math.round(point.y),
                    Math.round(zoomButton.width))
            }
        }

        MouseArea {
            z: 10
            anchors.left: parent.left
            anchors.leftMargin: zoomButton.bodyWidth
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            cursorShape: Qt.PointingHandCursor
            hoverEnabled: true

            function partForY(y) {
                return y < height / 2 ? 2 : -2
            }

            onEntered: timelineItem.setZoomControlHoveredPart(partForY(mouseY))
            onPositionChanged: timelineItem.setZoomControlHoveredPart(partForY(mouseY))
            onExited: timelineItem.setZoomControlHoveredPart(0)
            onPressed: function(mouse) {
                timelineItem.setZoomControlPressedPart(partForY(mouse.y))
            }
            onReleased: timelineItem.setZoomControlPressedPart(0)
            onCanceled: timelineItem.setZoomControlPressedPart(0)
            onClicked: function(mouse) {
                timelineItem.stepZoomPreset(partForY(mouse.y) > 0 ? 1 : -1)
            }
        }

        onClicked: {
            if (!controller)
                return
            const point = zoomButton.mapToGlobal(0, 0)
            controller.openTimelineZoomMenu(
                Math.round(point.x),
                Math.round(point.y),
                Math.round(zoomButton.width))
        }
    }

    ToolButton {
        id: settingsButton

        width: Math.max(1, Math.round(28 * root.headerScale))
        height: Math.max(1, Math.round(22 * root.headerScale))
        x: Math.max(
            zoomButton.x + zoomButton.width + Math.round(8 * root.headerScale),
            parent.width - Math.round(8 * root.headerScale) - width)
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        padding: 1
        hoverEnabled: true
        enabled: timelineItem.stateBridge !== null
        opacity: 0

        onHoveredChanged: timelineItem.setSettingsControlHovered(hovered)
        onPressedChanged: timelineItem.setSettingsControlPressed(pressed)
        onClicked: {
            if (!controller)
                return
            const topRight = settingsButton.mapToGlobal(settingsButton.width, 0)
            controller.openTimelineBrightnessMenu(Math.round(topRight.x), Math.round(topRight.y))
        }
    }

    // Follow controls no longer live in this header surface. Code
    // Follow is exposed in BottomTabsQuickHost.qml; View Lock and
    // Progress Follow use fixed default behavior.
    /*
    CheckBox {
        id: followPreviewCheck

        anchors.right: followProgressCheck.left
        anchors.rightMargin: 10
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        hoverEnabled: true
        spacing: 4
        text: "Cursor Follow"
        checked: timelineItem.followPreviewEnabled
        // Phase 9d-native — invisible (DComp renders natively in the
        // popup composition plane) but still receives input. DComp
        // popup is WS_EX_TRANSPARENT so clicks pass through.
        opacity: 0

        indicator: Rectangle {
            implicitWidth: 14
            implicitHeight: 14
            x: 0
            y: (followPreviewCheck.height - height) / 2
            radius: 3
            color: followPreviewCheck.checked
                ? root.tone("accent", "#60a5fa")
                : root.tone("cardBg", "#1f2937")
            border.width: 1
            border.color: followPreviewCheck.checked
                ? root.tone("accent", "#60a5fa")
                : (followPreviewCheck.hovered
                    ? root.tone("accent", "#60a5fa")
                    : root.tone("border", "#475569"))

            Canvas {
                anchors.fill: parent
                visible: followPreviewCheck.checked

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = root.tone("accentText", "#ffffff")
                    ctx.lineWidth = 1.8
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(width * 0.24, height * 0.55)
                    ctx.lineTo(width * 0.44, height * 0.74)
                    ctx.lineTo(width * 0.78, height * 0.28)
                    ctx.stroke()
                }
            }
        }

        contentItem: Text {
            text: followPreviewCheck.text
            color: root.tone("textPrimary", "#e5e7eb")
            font.weight: Font.DemiBold
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            leftPadding: followPreviewCheck.indicator.width + followPreviewCheck.spacing
        }

        onClicked: timelineItem.followPreviewEnabled = checked
    }

    CheckBox {
        id: followProgressCheck

        anchors.right: parent.right
        anchors.rightMargin: 8
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        hoverEnabled: true
        spacing: 4
        text: "Progress Follow"
        checked: timelineItem.followProgressEnabled
        opacity: 0

        indicator: Rectangle {
            implicitWidth: 14
            implicitHeight: 14
            x: 0
            y: (followProgressCheck.height - height) / 2
            radius: 3
            color: followProgressCheck.checked
                ? root.tone("accent", "#60a5fa")
                : root.tone("cardBg", "#1f2937")
            border.width: 1
            border.color: followProgressCheck.checked
                ? root.tone("accent", "#60a5fa")
                : (followProgressCheck.hovered
                    ? root.tone("accent", "#60a5fa")
                    : root.tone("border", "#475569"))

            Canvas {
                anchors.fill: parent
                visible: followProgressCheck.checked

                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = root.tone("accentText", "#ffffff")
                    ctx.lineWidth = 1.8
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"
                    ctx.beginPath()
                    ctx.moveTo(width * 0.24, height * 0.55)
                    ctx.lineTo(width * 0.44, height * 0.74)
                    ctx.lineTo(width * 0.78, height * 0.28)
                    ctx.stroke()
                }
            }
        }

        contentItem: Text {
            text: followProgressCheck.text
            color: root.tone("textPrimary", "#e5e7eb")
            font.weight: Font.DemiBold
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            leftPadding: followProgressCheck.indicator.width + followProgressCheck.spacing
        }

        onClicked: timelineItem.followProgressEnabled = checked
    }
    */
}
