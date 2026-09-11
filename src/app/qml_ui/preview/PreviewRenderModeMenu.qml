import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Sticky extras live in this popup (a Menu would dismiss on item click).
AppStickyPopup {
    id: root

    required property var previewSession
    minimumWidth: 180

    // The parameter sliders need the timeline brightness menu's 220 px of track.
    readonly property int parameterRowWidth: 220
    readonly property real rowWidth: Math.max(
        180,
        regularRow.implicitWidth,
        muriRow.implicitWidth,
        smoothSwitch.implicitWidth,
        root.previewSession.muriCheckEnabled
            ? root.parameterRowWidth + muriParameters.leftPadding + muriParameters.rightPadding
            : 0
    )

    contentItem: Column {
        id: body
        width: root.rowWidth
        spacing: 0

        component ModeRow: ChromeRow {
            id: modeRow
            stateColors: Theme.colors.popupState

            required property string label
            required property bool active

            implicitHeight: 28
            implicitWidth: 12 + 10 + modeLabel.implicitWidth + leftPadding + rightPadding
            leftPadding: 12
            rightPadding: 16
            selected: modeRow.active
            Accessible.name: modeRow.label
            Accessible.checkable: true
            Accessible.checked: modeRow.active

            contentItem: Item {
                Text {
                    id: checkMark
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12
                    text: modeRow.active ? "✓" : ""
                    color: Theme.colors.text.active
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    id: modeLabel
                    anchors.left: checkMark.right
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: modeRow.label
                    color: !modeRow.enabled ? Theme.colors.text.disabled
                         : (modeRow.active || modeRow.hovered || modeRow.down) ? Theme.colors.text.active
                         : Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
                }
            }

        }

        // Same layout as the timeline brightness menu's rows: title and live value
        // on one line, the slider under it.
        component ParameterRow: Column {
            id: parameterRow

            required property string title
            required property string valueText
            required property real from
            required property real to
            required property real stepSize
            required property real value
            signal edited(int newValue)

            spacing: 4

            Item {
                width: parent.width
                implicitHeight: Math.max(parameterTitle.implicitHeight, parameterValue.implicitHeight)

                Text {
                    id: parameterTitle
                    anchors.left: parent.left
                    anchors.right: parameterValue.left
                    anchors.rightMargin: 8
                    text: parameterRow.title
                    elide: Text.ElideRight
                    color: Theme.colors.text.primary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.compactFontSize
                    font.weight: Font.DemiBold
                }

                Text {
                    id: parameterValue
                    anchors.right: parent.right
                    text: parameterRow.valueText
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.compactFontSize
                    horizontalAlignment: Text.AlignRight
                }
            }

            AppSlider {
                id: parameterSlider
                width: parent.width
                from: parameterRow.from
                to: parameterRow.to
                stepSize: parameterRow.stepSize
                snapMode: Slider.SnapAlways
                value: parameterRow.value
                onMoved: parameterRow.edited(Math.round(value))

                // Dragging replaces the value binding; take the stored (possibly
                // clamped) value back once the handle is released.
                Connections {
                    target: root.previewSession
                    function onMuriParametersChanged() {
                        if (parameterSlider.pressed)
                            return
                        parameterSlider.value = parameterRow.value
                    }
                }
            }
        }

        ModeRow {
            id: regularRow
            width: body.width
            label: UiText.text("常规渲染")
            active: !root.previewSession.muriCheckEnabled
            onClicked: root.previewSession.setMuriCheckEnabled(false)
        }

        ModeRow {
            id: muriRow
            width: body.width
            label: UiText.text("无理检测")
            active: root.previewSession.muriCheckEnabled
            onClicked: root.previewSession.setMuriCheckEnabled(true)
        }

        Item {
            width: parent.width
            implicitHeight: 15

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                height: 1
                color: Theme.colors.border.normal
            }
        }

        AppSwitch {
            id: smoothSwitch
            visible: !root.previewSession.muriCheckEnabled
            width: body.width
            leftPadding: 12
            rightPadding: 16
            text: UiText.text("平滑星星消去动画")
            checked: root.previewSession.smoothStarErase
            onToggled: {
                if (checked === root.previewSession.smoothStarErase)
                    return
                root.previewSession.setSmoothStarErase(checked)
            }

            Connections {
                target: root.previewSession
                function onRenderModeChanged() {
                    if (smoothSwitch.checked !== root.previewSession.smoothStarErase)
                        smoothSwitch.checked = root.previewSession.smoothStarErase
                }
            }
        }

        Column {
            id: muriParameters
            visible: root.previewSession.muriCheckEnabled
            width: body.width
            spacing: 8
            leftPadding: 12
            rightPadding: 16
            topPadding: 4
            bottomPadding: 8

            readonly property var ranges: root.previewSession.muriParameterRanges
            readonly property real rowContentWidth: width - leftPadding - rightPadding

            ParameterRow {
                width: muriParameters.rowContentWidth
                title: UiText.text("手部半径")
                // Shown as a share of the default hand (30 px = 100%).
                valueText: UiText.text("%1%").arg(Math.round(
                    root.previewSession.muriHandRadiusPx * 100 / muriParameters.ranges.handRadiusDefault))
                from: muriParameters.ranges.handRadiusMin
                to: muriParameters.ranges.handRadiusMax
                stepSize: muriParameters.ranges.handRadiusStep
                value: root.previewSession.muriHandRadiusPx
                onEdited: value => root.previewSession.setMuriHandRadiusPx(value)
            }

            ParameterRow {
                width: muriParameters.rowContentWidth
                title: UiText.text("撞尾阈值")
                valueText: UiText.text("%1 ms").arg(root.previewSession.muriTapOnSlideThresholdMs)
                from: muriParameters.ranges.tapOnSlideThresholdMin
                to: muriParameters.ranges.tapOnSlideThresholdMax
                stepSize: muriParameters.ranges.tapOnSlideThresholdStep
                value: root.previewSession.muriTapOnSlideThresholdMs
                onEdited: value => root.previewSession.setMuriTapOnSlideThresholdMs(value)
            }
        }
    }
}
