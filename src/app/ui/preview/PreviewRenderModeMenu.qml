import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Sticky extras live in this popup (a Menu would dismiss on item click).
AppStickyPopup {
    id: root

    required property var previewSession

    contentItem: Column {
        spacing: 0

        component ModeRow: ChromeRow {
            id: modeRow
            stateColors: Theme.colors.popupState

            required property string label
            required property bool active

            implicitHeight: 28
            implicitWidth: Math.ceil(12 + 10 + labelMetrics.advanceWidth + leftPadding + rightPadding)
            leftPadding: 12
            rightPadding: 16
            selected: modeRow.active
            Accessible.name: modeRow.label
            Accessible.checkable: true
            Accessible.checked: modeRow.active

            TextMetrics {
                id: labelMetrics
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                text: modeRow.label
            }

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

            TextMetrics {
                id: titleMetrics
                font.family: Theme.uiFont
                font.pixelSize: Theme.compactFontSize
                font.weight: Font.DemiBold
                text: parameterRow.title
            }

            TextMetrics {
                id: valueMetrics
                font.family: Theme.uiFont
                font.pixelSize: Theme.compactFontSize
                text: parameterRow.valueText
            }

            Item {
                width: parent.width
                implicitWidth: titleMetrics.advanceWidth + 8 + valueMetrics.advanceWidth
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
            width: parent.width
            label: qsTrId("qml.normal_rendering")
            active: !root.previewSession.muriCheckEnabled
            onClicked: root.previewSession.setMuriCheckEnabled(false)
        }

        ModeRow {
            width: parent.width
            label: qsTrId("qml.muri_analysis")
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
            width: parent.width
            leftPadding: 12
            rightPadding: 16
            text: qsTrId("qml.smooth_star_clear_animation")
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
            width: parent.width
            spacing: 8
            leftPadding: 12
            rightPadding: 16
            topPadding: 4
            bottomPadding: 8

            readonly property var ranges: root.previewSession.muriParameterRanges
            readonly property real rowContentWidth: Math.max(0, width - leftPadding - rightPadding)

            ParameterRow {
                id: handRow
                width: muriParameters.rowContentWidth
                title: qsTrId("qml.hand_radius")
                valueText: qsTrId("qml.1").arg(Math.round(
                    root.previewSession.muriHandRadiusPx * 100 / muriParameters.ranges.handRadiusDefault))
                from: muriParameters.ranges.handRadiusMin
                to: muriParameters.ranges.handRadiusMax
                stepSize: muriParameters.ranges.handRadiusStep
                value: root.previewSession.muriHandRadiusPx
                onEdited: value => root.previewSession.setMuriHandRadiusPx(value)
            }

            ParameterRow {
                id: tapRow
                width: muriParameters.rowContentWidth
                title: qsTrId("validation.tap_on_slide_threshold")
                valueText: qsTrId("qml.milliseconds_value").arg(root.previewSession.muriTapOnSlideThresholdMs)
                from: muriParameters.ranges.tapOnSlideThresholdMin
                to: muriParameters.ranges.tapOnSlideThresholdMax
                stepSize: muriParameters.ranges.tapOnSlideThresholdStep
                value: root.previewSession.muriTapOnSlideThresholdMs
                onEdited: value => root.previewSession.setMuriTapOnSlideThresholdMs(value)
            }
        }
    }
}
