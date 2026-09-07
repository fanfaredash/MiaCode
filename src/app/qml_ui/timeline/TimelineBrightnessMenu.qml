import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

AppStickyPopup {
    id: root

    required property var stateBridge
    minimumWidth: 180
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

    contentItem: ColumnLayout {
        spacing: 8
        width: 220

        component BrightnessRow: ColumnLayout {
            id: row

            required property string title
            required property real brightness
            signal brightnessEdited(real value)

            spacing: 4

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.fillWidth: true
                    text: row.title
                    color: Theme.colors.text.primary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.compactFontSize
                    font.weight: Font.DemiBold
                }

                Text {
                    text: UiText.text("%1%").arg(Math.round(row.brightness * 100))
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.compactFontSize
                    horizontalAlignment: Text.AlignRight
                }
            }

            AppSlider {
                id: slider
                Layout.fillWidth: true
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
            Layout.fillWidth: true
            title: UiText.text("波形图亮度")
            brightness: root.stateBridge ? root.stateBridge.waveformBrightness : 0.5
            onBrightnessEdited: value => root.stateBridge.waveformBrightness = value
        }

        BrightnessRow {
            Layout.fillWidth: true
            title: UiText.text("网格线亮度")
            brightness: root.stateBridge ? root.stateBridge.measureLineBrightness : 1.0
            onBrightnessEdited: value => root.stateBridge.measureLineBrightness = value
        }
    }
}
