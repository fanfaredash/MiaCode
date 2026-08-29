import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 音频设置. Every change writes straight through and is applied to the running
// preview, matching the Widgets dialog: there is no OK/Apply.
Dialog {
    id: root

    required property var audioSettings

    property var rows: []

    function refresh() {
        root.rows = root.audioSettings.channels()
    }

    onAboutToShow: root.refresh()

    Connections {
        target: root.audioSettings
        function onChanged() { root.refresh() }
    }

    title: qsTr("音频设置")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(520, Overlay.overlay ? Overlay.overlay.width - 48 : 520)
    footer: DialogFooter {
        cancelText: qsTr("关闭")
        onRejected: root.reject()
    }
    closePolicy: Popup.CloseOnEscape

    contentItem: ColumnLayout {
        spacing: 8

        Repeater {
            model: root.rows
            delegate: RowLayout {
                id: channelRow
                required property var modelData
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.preferredWidth: 96
                    text: channelRow.modelData.label
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    elide: Text.ElideRight
                }
                IconButton {
                    glyph: channelRow.modelData.muted ? "🔇" : "🔊"
                    tooltip: channelRow.modelData.muted ? qsTr("取消静音") : qsTr("静音")
                    onClicked: root.audioSettings.toggleChannelMuted(channelRow.modelData.key)
                }
                AppSlider {
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    stepSize: 1
                    value: channelRow.modelData.percent
                    onMoved: root.audioSettings.setChannelPercent(
                                 channelRow.modelData.key, Math.round(value))
                }
                Text {
                    Layout.preferredWidth: 40
                    horizontalAlignment: Text.AlignRight
                    text: channelRow.modelData.percent + "%"
                    color: Theme.colors.text.active
                    font.family: Theme.uiFont
                }
            }
        }

        AppSwitch {
            objectName: "audioBreakSlideTailCheerSwitch"
            text: qsTr("静音 Break 星星尾判音")
            checked: root.audioSettings.breakSlideTailCheerMuted
            onToggled: root.audioSettings.breakSlideTailCheerMuted = checked
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            AppButton {
                text: qsTr("设为本地默认")
                onClicked: root.audioSettings.saveAsSoftwareDefault()
            }
            AppButton {
                text: qsTr("恢复本地默认")
                onClicked: root.audioSettings.restoreSoftwareDefault()
            }
            Item { Layout.fillWidth: true }
        }
    }
}
