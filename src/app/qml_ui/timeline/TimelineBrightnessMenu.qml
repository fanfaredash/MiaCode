import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Popup {
    id: root

    required property var stateBridge

    readonly property int brightnessPercentMin: 20
    readonly property int brightnessPercentMax: 200
    readonly property int brightnessPercentStep: 5

    padding: Theme.menuPadding
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    property var anchorItem: null

    function percentFromBrightness(value) {
        const percent = Math.round(value * 100)
        const stepped = Math.round(percent / root.brightnessPercentStep) * root.brightnessPercentStep
        return Math.max(root.brightnessPercentMin, Math.min(root.brightnessPercentMax, stepped))
    }

    function brightnessFromPercent(percent) {
        const stepped = Math.round(percent / root.brightnessPercentStep) * root.brightnessPercentStep
        return Math.max(root.brightnessPercentMin, Math.min(root.brightnessPercentMax, stepped)) / 100
    }

    function openAt(anchor) {
        if (!anchor || Overlay.overlay === null || !root.stateBridge)
            return
        parent = Overlay.overlay
        root.anchorItem = anchor
        reposition()
        open()
    }

    function reposition() {
        if (!root.anchorItem || Overlay.overlay === null)
            return
        const above = root.anchorItem.mapToItem(Overlay.overlay, 0, 0)
        x = Math.max(0, above.x + root.anchorItem.width - width)
        y = Math.max(0, above.y - height)
    }

    onImplicitWidthChanged: if (visible)
        reposition()
    onImplicitHeightChanged: if (visible)
        reposition()

    contentItem: ColumnLayout {
        spacing: 10
        width: 250

        component BrightnessRow: ColumnLayout {
            id: row

            required property string title
            required property real brightness
            signal brightnessEdited(real value)

            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                Text {
                    Layout.fillWidth: true
                    text: row.title
                    color: Theme.colors.text.primary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
                    font.weight: Font.DemiBold
                }

                Text {
                    text: qsTr("%1%").arg(Math.round(row.brightness * 100))
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
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
            title: qsTr("波形图亮度")
            brightness: root.stateBridge ? root.stateBridge.waveformBrightness : 0.5
            onBrightnessEdited: value => root.stateBridge.waveformBrightness = value
        }

        BrightnessRow {
            Layout.fillWidth: true
            title: qsTr("网格线亮度")
            brightness: root.stateBridge ? root.stateBridge.measureLineBrightness : 1.0
            onBrightnessEdited: value => root.stateBridge.measureLineBrightness = value
        }
    }

    background: Rectangle {
        implicitWidth: 180
        color: Theme.colors.background.elevated
        border.width: Theme.controlBorderWidth
        border.color: Theme.colors.border.normal
        radius: Theme.controlRadius
    }
}
