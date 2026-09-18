import QtQuick
import QtQuick.Controls
import MiaCode.UI

AppStickyPopup {
    id: root

    required property var stateBridge
    openAbove: true

    readonly property int brightnessPercentMin: 20
    readonly property int brightnessPercentMax: 200
    readonly property int brightnessPercentStep: 5

    function percentFromBrightness(value) {
        const percent = Math.round(value * 100)
        const stepped = Math.round(percent / root.brightnessPercentStep) * root.brightnessPercentStep
        return Math.max(root.brightnessPercentMin, Math.min(root.brightnessPercentMax, stepped))
    }

    function brightnessFromPercent(percent) {
        const stepped = Math.round(percent / root.brightnessPercentStep) * root.brightnessPercentStep
        return Math.max(root.brightnessPercentMin, Math.min(root.brightnessPercentMax, stepped)) / 100
    }

    contentItem: Column {
        spacing: 8

        component BrightnessRow: Column {
            id: row

            required property string title
            required property real brightness
            signal brightnessEdited(real value)

            width: parent.width
            spacing: 4

            TextMetrics {
                id: titleMetrics
                font.family: Theme.uiFont
                font.pixelSize: Theme.compactFontSize
                font.weight: Font.DemiBold
                text: row.title
            }

            TextMetrics {
                id: valueMetrics
                font.family: Theme.uiFont
                font.pixelSize: Theme.compactFontSize
                text: valueLabel.text
            }

            Item {
                width: parent.width
                implicitWidth: titleMetrics.advanceWidth + 8 + valueMetrics.advanceWidth
                implicitHeight: Math.max(titleLabel.implicitHeight, valueLabel.implicitHeight)

                Text {
                    id: titleLabel
                    anchors.left: parent.left
                    anchors.right: valueLabel.left
                    anchors.rightMargin: 8
                    text: row.title
                    elide: Text.ElideRight
                    color: Theme.colors.text.primary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.compactFontSize
                    font.weight: Font.DemiBold
                }

                Text {
                    id: valueLabel
                    anchors.right: parent.right
                    text: qsTrId("qml.1").arg(Math.round(row.brightness * 100))
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.compactFontSize
                    horizontalAlignment: Text.AlignRight
                }
            }

            AppSlider {
                id: slider
                width: parent.width
                from: root.brightnessPercentMin
                to: root.brightnessPercentMax
                stepSize: root.brightnessPercentStep
                snapMode: Slider.SnapAlways
                value: root.percentFromBrightness(row.brightness)
                onMoved: row.brightnessEdited(root.brightnessFromPercent(value))

                Connections {
                    target: root.stateBridge
                    function onWaveformBrightnessChanged() {
                        if (slider.pressed)
                            return
                        slider.value = root.percentFromBrightness(row.brightness)
                    }
                    function onMeasureLineBrightnessChanged() {
                        if (slider.pressed)
                            return
                        slider.value = root.percentFromBrightness(row.brightness)
                    }
                }
            }
        }

        BrightnessRow {
            title: qsTrId("shell.timeline_waveform_brightness")
            brightness: root.stateBridge ? root.stateBridge.waveformBrightness : 0.5
            onBrightnessEdited: value => root.stateBridge.waveformBrightness = value
        }

        BrightnessRow {
            title: qsTrId("shell.timeline_measure_line_brightness")
            brightness: root.stateBridge ? root.stateBridge.measureLineBrightness : 1.0
            onBrightnessEdited: value => root.stateBridge.measureLineBrightness = value
        }
    }
}
