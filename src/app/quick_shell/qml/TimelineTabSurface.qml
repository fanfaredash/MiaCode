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

    function isChineseUi() {
        return paletteMap && paletteMap["isChineseUi"] === true
    }

    function tone(key, fallback) {
        return paletteMap && paletteMap[key] !== undefined ? paletteMap[key] : fallback
    }

    TimelineQuickItem {
        id: timelineItem

        anchors.fill: parent
        // Phase 4-perf experiment — when MIACODE_DISABLE_TIMELINE is
        // set, hide the timeline entirely so its QSG rendering load
        // (note rasters, waveform, grid lines) doesn't compete with
        // DComp for GPU/CPU. Used in conjunction with
        // QT_QUICK_BACKEND=software to test the dual-swap-chain
        // contention hypothesis.
        visible: !(controller && controller.disableTimeline)
        stateBridge: !(controller && controller.disableTimeline)
            ? (controller ? controller.timelineStateBridge : null)
            : null
        headerLeftLimit: zoomButton.x + zoomButton.width + 2
        headerRightLimit: followCheck.x - 2

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
        onPreviewPlayPauseRequested: {
            if (controller)
                controller.togglePreviewPlayback()
        }
    }

    ToolButton {
        id: zoomButton

        anchors.left: parent.left
        anchors.leftMargin: 4
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        implicitHeight: 22
        padding: 1
        leftPadding: 2
        rightPadding: 8
        spacing: 6
        hoverEnabled: true
        text: Math.round(timelineItem.zoomScale * 100) + "%"

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

                    width: 18
                    height: 18

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
                    font.pixelSize: 12
                    height: zoomGlyph.height
                    verticalAlignment: Text.AlignVCenter
                }
            }
        }

        onClicked: timelineItem.cycleZoomPreset()
    }

    CheckBox {
        id: followCheck

        anchors.right: parent.right
        anchors.rightMargin: 8
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        hoverEnabled: true
        spacing: 4
        text: root.isChineseUi() ? "跟随预览" : "Follow Preview"
        checked: timelineItem.followPreviewEnabled

        indicator: Rectangle {
            implicitWidth: 14
            implicitHeight: 14
            x: 0
            y: (followCheck.height - height) / 2
            radius: 3
            color: followCheck.checked
                ? root.tone("accent", "#60a5fa")
                : root.tone("cardBg", "#1f2937")
            border.width: 1
            border.color: followCheck.checked
                ? root.tone("accent", "#60a5fa")
                : (followCheck.hovered
                    ? root.tone("accent", "#60a5fa")
                    : root.tone("border", "#475569"))

            Canvas {
                anchors.fill: parent
                visible: followCheck.checked

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
            text: followCheck.text
            color: root.tone("textPrimary", "#e5e7eb")
            font.weight: Font.DemiBold
            font.pixelSize: 12
            verticalAlignment: Text.AlignVCenter
            leftPadding: followCheck.indicator.width + followCheck.spacing
        }

        onClicked: timelineItem.followPreviewEnabled = checked
    }
}
