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
        // The follow checkboxes used to live here too and bounded the
        // header band on the right; they moved to BottomTabsQuickHost
        // tab strip. Without them, the band can use the full viewport
        // width minus a small right pad for the scrollbar / scroll
        // gutter.
        headerRightLimit: width - 8

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
        implicitHeight: Math.max(1, Math.round(22 * root.headerScale))
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

        onClicked: timelineItem.cycleZoomPreset()
    }

    // Settings gear at the top-right of the timeline header band,
    // mirroring the zoom button on the left. Click opens an upward
    // popup containing the View Lock / Cursor Follow / Progress
    // Follow toggles, which the BottomTabsQuickHost tab strip now
    // hides behind a single "Progress Follow" inline checkbox.
    // Draft revision \u2014 geometry / open-direction up for review.
    ToolButton {
        id: settingsGearButton

        anchors.right: parent.right
        anchors.rightMargin: Math.round(8 * root.headerScale)
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        implicitWidth: Math.max(1, Math.round(28 * root.headerScale))
        implicitHeight: Math.max(1, Math.round(22 * root.headerScale))
        padding: 1
        hoverEnabled: true
        checkable: true
        checked: settingsMenu.opened

        background: Rectangle {
            radius: 6
            color: settingsGearButton.down || settingsGearButton.checked
                ? root.tone("accentPressed", "#2563eb")
                : (settingsGearButton.hovered
                    ? root.tone("menuHoverBg", "#334155")
                    : root.tone("cardBg", "#1f2937"))
            border.width: 1
            border.color: (settingsGearButton.hovered && !settingsGearButton.down)
                || settingsGearButton.checked
                ? root.tone("accent", "#60a5fa")
                : root.tone("borderStrong", "#475569")
        }

        contentItem: Canvas {
            id: gearGlyph

            property color strokeColor: settingsGearButton.down || settingsGearButton.checked
                ? root.tone("accentText", "#ffffff")
                : root.tone("timelineLabel", "#d4dce8")

            onStrokeColorChanged: requestPaint()

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                const cx = width / 2
                const cy = height / 2
                const minDim = Math.min(width, height)
                const outerR = minDim * 0.40
                const innerR = outerR * 0.62
                const hubR = innerR * 0.55
                const toothCount = 6
                ctx.fillStyle = strokeColor
                ctx.beginPath()
                for (let i = 0; i < toothCount * 2; ++i) {
                    const angle = (i * Math.PI / toothCount) - Math.PI / 2
                    const r = (i % 2 === 0) ? outerR : innerR
                    const px = cx + Math.cos(angle) * r
                    const py = cy + Math.sin(angle) * r
                    if (i === 0)
                        ctx.moveTo(px, py)
                    else
                        ctx.lineTo(px, py)
                }
                ctx.closePath()
                ctx.fill()
                // Punch the central hub.
                ctx.globalCompositeOperation = "destination-out"
                ctx.beginPath()
                ctx.ellipse(cx - hubR, cy - hubR, hubR * 2, hubR * 2, 0, 0, Math.PI * 2)
                ctx.fill()
                ctx.globalCompositeOperation = "source-over"
            }
        }

        onClicked: {
            if (settingsMenu.opened)
                settingsMenu.close()
            else
                settingsMenu.open()
        }
    }

    Popup {
        id: settingsMenu

        parent: settingsGearButton
        // Open upward so the popup covers the Progress Follow chip in
        // the bottom-tab strip above (deliberate \u2014 matches the spec
        // "neatly obscure the retained Progress Follow option").
        // The popup is rendered on the application overlay layer so
        // it isn't clipped to the timeline surface bounds.
        x: settingsGearButton.width - width
        y: -height - 4
        width: Math.max(160, settingsContent.implicitWidth + 16)
        height: settingsContent.implicitHeight + 16
        padding: 8
        modal: false
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        background: Rectangle {
            radius: 8
            color: root.tone("inputBg", "#1f2937")
            border.width: 1
            border.color: root.tone("borderStrong", "#475569")
        }

        contentItem: Column {
            id: settingsContent

            spacing: Math.round(6 * root.headerScale)

            CheckBox {
                id: menuViewLockCheck

                width: parent.width
                hoverEnabled: true
                spacing: Math.round(6 * root.headerScale)
                text: root.isChineseUi() ? "\u5149\u6807\u5c45\u4e2d" : "View Lock"
                checked: controller && controller.timelineStateBridge
                    ? controller.timelineStateBridge.viewportLockEnabled
                    : false

                indicator: Rectangle {
                    implicitWidth: Math.max(1, Math.round(14 * root.headerScale))
                    implicitHeight: Math.max(1, Math.round(14 * root.headerScale))
                    x: 0
                    y: (menuViewLockCheck.height - height) / 2
                    radius: 3
                    color: menuViewLockCheck.checked
                        ? root.tone("accent", "#60a5fa")
                        : root.tone("cardBg", "#1f2937")
                    border.width: 1
                    border.color: menuViewLockCheck.checked
                        ? root.tone("accent", "#60a5fa")
                        : (menuViewLockCheck.hovered
                            ? root.tone("accent", "#60a5fa")
                            : root.tone("border", "#475569"))

                    Canvas {
                        anchors.fill: parent
                        visible: menuViewLockCheck.checked
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
                    text: menuViewLockCheck.text
                    color: root.tone("textPrimary", "#e5e7eb")
                    font.pixelSize: Math.max(1, Math.round(12 * root.headerScale))
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: menuViewLockCheck.indicator.width + menuViewLockCheck.spacing
                }

                onClicked: {
                    if (controller && controller.timelineStateBridge)
                        controller.timelineStateBridge.viewportLockEnabled = checked
                    if (controller)
                        controller.timelineViewportLockToggled(checked)
                }
            }

            CheckBox {
                id: menuCursorFollowCheck

                width: parent.width
                hoverEnabled: true
                spacing: Math.round(6 * root.headerScale)
                text: root.isChineseUi() ? "\u4ee3\u7801\u8ddf\u968f" : "Cursor Follow"
                checked: controller && controller.timelineStateBridge
                    ? controller.timelineStateBridge.followPreviewEnabled
                    : false

                indicator: Rectangle {
                    implicitWidth: Math.max(1, Math.round(14 * root.headerScale))
                    implicitHeight: Math.max(1, Math.round(14 * root.headerScale))
                    x: 0
                    y: (menuCursorFollowCheck.height - height) / 2
                    radius: 3
                    color: menuCursorFollowCheck.checked
                        ? root.tone("accent", "#60a5fa")
                        : root.tone("cardBg", "#1f2937")
                    border.width: 1
                    border.color: menuCursorFollowCheck.checked
                        ? root.tone("accent", "#60a5fa")
                        : (menuCursorFollowCheck.hovered
                            ? root.tone("accent", "#60a5fa")
                            : root.tone("border", "#475569"))

                    Canvas {
                        anchors.fill: parent
                        visible: menuCursorFollowCheck.checked
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
                    text: menuCursorFollowCheck.text
                    color: root.tone("textPrimary", "#e5e7eb")
                    font.pixelSize: Math.max(1, Math.round(12 * root.headerScale))
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: menuCursorFollowCheck.indicator.width + menuCursorFollowCheck.spacing
                }

                onClicked: {
                    if (controller && controller.timelineStateBridge)
                        controller.timelineStateBridge.followPreviewEnabled = checked
                    if (controller)
                        controller.timelineFollowPreviewToggled(checked)
                }
            }

            CheckBox {
                id: menuProgressFollowCheck

                width: parent.width
                hoverEnabled: true
                spacing: Math.round(6 * root.headerScale)
                text: root.isChineseUi() ? "\u8fdb\u5ea6\u8ddf\u968f" : "Progress Follow"
                checked: controller && controller.timelineStateBridge
                    ? controller.timelineStateBridge.followProgressEnabled
                    : false

                indicator: Rectangle {
                    implicitWidth: Math.max(1, Math.round(14 * root.headerScale))
                    implicitHeight: Math.max(1, Math.round(14 * root.headerScale))
                    x: 0
                    y: (menuProgressFollowCheck.height - height) / 2
                    radius: 3
                    color: menuProgressFollowCheck.checked
                        ? root.tone("accent", "#60a5fa")
                        : root.tone("cardBg", "#1f2937")
                    border.width: 1
                    border.color: menuProgressFollowCheck.checked
                        ? root.tone("accent", "#60a5fa")
                        : (menuProgressFollowCheck.hovered
                            ? root.tone("accent", "#60a5fa")
                            : root.tone("border", "#475569"))

                    Canvas {
                        anchors.fill: parent
                        visible: menuProgressFollowCheck.checked
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
                    text: menuProgressFollowCheck.text
                    color: root.tone("textPrimary", "#e5e7eb")
                    font.pixelSize: Math.max(1, Math.round(12 * root.headerScale))
                    font.weight: Font.DemiBold
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: menuProgressFollowCheck.indicator.width + menuProgressFollowCheck.spacing
                }

                onClicked: {
                    if (controller && controller.timelineStateBridge)
                        controller.timelineStateBridge.followProgressEnabled = checked
                    if (controller)
                        controller.timelineFollowProgressToggled(checked)
                }
            }
        }
    }

    // View Lock / Code Follow / Progress Follow checkboxes moved to
    // BottomTabsQuickHost.qml's tab strip \u2014 see the long comment in
    // TimelineSceneStateBuilder.cpp where the matching scene-state
    // emit was retired. The deletion below removes the QML controls
    // that were anchored to this surface's right edge.
    /*
    CheckBox {
        id: followPreviewCheck

        anchors.right: followProgressCheck.left
        anchors.rightMargin: 10
        y: Math.max(0, (timelineItem.timelineTop - height) / 2)
        hoverEnabled: true
        spacing: 4
        text: root.isChineseUi() ? "\u4ee3\u7801\u8ddf\u968f" : "Cursor Follow"
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
        text: root.isChineseUi() ? "\u8fdb\u5ea6\u8ddf\u968f" : "Progress Follow"
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
