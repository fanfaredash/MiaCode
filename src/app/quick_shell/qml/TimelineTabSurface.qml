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
        if (brightnessControl)
            brightnessControl.syncFromBridge()
    }
    onMetricsMapChanged: {
        if (timelineItem)
            timelineItem.refreshTheme()
    }

    function zoomWrapsToMin(scale) {
        return scale >= 1.999
    }

    // Inline three-way localizer mirroring UiText::localized(en, zh, ja).
    function localized(en, zh, ja) {
        const lang = paletteMap ? paletteMap["uiLanguage"] : "en"
        if (lang === "zh")
            return zh
        if (lang === "ja")
            return ja !== undefined && ja !== "" ? ja : en
        return en
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
        headerLeftLimit: brightnessControl.x + brightnessControl.width + 2
        headerRightLimit: width - 8
        headerMarkerLeftLimit: zoomButton.x + zoomButton.width + 2
        headerMarkerRightLimit: width - 8

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
            height: Math.ceil(parent.height / 2)
            cursorShape: Qt.PointingHandCursor
            onPressed: timelineItem.setZoomControlPressedPart(2)
            onReleased: timelineItem.setZoomControlPressedPart(0)
            onCanceled: timelineItem.setZoomControlPressedPart(0)
            onClicked: timelineItem.stepZoomPreset(1)
        }

        MouseArea {
            z: 10
            anchors.left: parent.left
            anchors.leftMargin: zoomButton.bodyWidth
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.floor(parent.height / 2)
            cursorShape: Qt.PointingHandCursor
            onPressed: timelineItem.setZoomControlPressedPart(-2)
            onReleased: timelineItem.setZoomControlPressedPart(0)
            onCanceled: timelineItem.setZoomControlPressedPart(0)
            onClicked: timelineItem.stepZoomPreset(-1)
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

    Item {
        id: brightnessControl

        anchors.left: zoomButton.right
        anchors.leftMargin: Math.round(6 * root.headerScale)
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        width: Math.max(1, Math.round(128 * root.headerScale))
        height: Math.max(1, Math.round(22 * root.headerScale))
        visible: timelineItem.stateBridge !== null
        property int sliderActiveHeight: Math.max(1, Math.round(12 * root.headerScale))
        property int sliderHandleSize: Math.max(6, Math.round(8 * root.headerScale))
        property int sliderTrackHeight: Math.max(2, Math.round(3 * root.headerScale))

        function isInvertedForTheme() {
            return !(root.paletteMap && root.paletteMap["dark"] === true)
        }

        function sliderValueFromBrightness(brightness) {
            const percent = brightness * 100
            return isInvertedForTheme()
                ? brightnessSlider.from + brightnessSlider.to - percent
                : percent
        }

        function brightnessFromSliderValue(value) {
            const percent = isInvertedForTheme()
                ? brightnessSlider.from + brightnessSlider.to - value
                : value
            return percent / 100
        }

        function syncFromBridge() {
            if (timelineItem.stateBridge && !brightnessSlider.pressed)
                brightnessSlider.value = sliderValueFromBrightness(timelineItem.stateBridge.waveformBrightness)
        }

        Connections {
            target: timelineItem

            function onStateBridgeChanged() {
                brightnessControl.syncFromBridge()
            }
        }

        Connections {
            target: timelineItem.stateBridge
            ignoreUnknownSignals: true
        }

        Canvas {
            id: brightnessGlyph

            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(1, Math.round(18 * root.headerScale))
            height: Math.max(1, Math.round(18 * root.headerScale))

            property color strokeColor: root.tone("timelineLabel", "#d4dce8")

            onStrokeColorChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2
                const cy = height / 2
                const r = Math.max(2, Math.min(width, height) * 0.22)
                const rayInner = r + 2
                const rayOuter = Math.min(width, height) * 0.46
                ctx.strokeStyle = strokeColor
                ctx.fillStyle = strokeColor
                ctx.lineWidth = Math.max(1, root.headerScale)
                ctx.beginPath()
                ctx.ellipse(cx - r, cy - r, r * 2, r * 2, 0, 0, Math.PI * 2)
                ctx.fill()
                for (let i = 0; i < 8; ++i) {
                    const angle = i * Math.PI / 4
                    ctx.beginPath()
                    ctx.moveTo(cx + Math.cos(angle) * rayInner, cy + Math.sin(angle) * rayInner)
                    ctx.lineTo(cx + Math.cos(angle) * rayOuter, cy + Math.sin(angle) * rayOuter)
                    ctx.stroke()
                }
            }
        }

        Slider {
            id: brightnessSlider

            anchors.left: brightnessGlyph.right
            anchors.leftMargin: Math.round(4 * root.headerScale)
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: brightnessControl.sliderActiveHeight
            from: 20
            to: 200
            stepSize: 5
            live: true
            value: 100

            onMoved: {
                if (timelineItem.stateBridge)
                    timelineItem.stateBridge.waveformBrightness = brightnessControl.brightnessFromSliderValue(value)
            }

            background: Item {
                x: brightnessSlider.leftPadding
                y: Math.round((brightnessSlider.height - height) / 2)
                width: brightnessSlider.availableWidth
                height: brightnessControl.sliderTrackHeight

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: root.tone("border", "#d5e0ec")
                }

                Rectangle {
                    width: Math.max(height, brightnessSlider.visualPosition * parent.width)
                    height: parent.height
                    radius: height / 2
                    color: root.tone("accent", "#2e77d0")
                }
            }

            handle: Rectangle {
                x: brightnessSlider.leftPadding
                    + brightnessSlider.visualPosition * (brightnessSlider.availableWidth - width)
                y: Math.round((brightnessSlider.height - height) / 2)
                width: brightnessControl.sliderHandleSize
                height: width
                radius: width / 2
                color: brightnessSlider.pressed
                    ? root.tone("accentPressed", "#2668b9")
                    : root.tone("cardBg", "#ffffff")
                border.width: 1
                border.color: brightnessSlider.hovered || brightnessSlider.pressed
                    ? root.tone("accent", "#2e77d0")
                    : root.tone("borderStrong", "#b8c7da")
            }

            Component.onCompleted: brightnessControl.syncFromBridge()
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
        text: root.localized("Cursor Follow", "\u4ee3\u7801\u8ddf\u968f", "\u30ab\u30fc\u30bd\u30eb\u8ffd\u5f93")
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
        text: root.localized("Progress Follow", "\u8fdb\u5ea6\u8ddf\u968f", "\u9032\u6357\u8ffd\u5f93")
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
