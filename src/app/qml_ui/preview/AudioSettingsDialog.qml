import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 音频设置. Every change writes straight through and is applied to the running
// preview, matching the Widgets dialog: there is no OK/Apply.
//
// Edits also audition the channel they touched, so a level can be judged without
// starting playback. The model owns the sample runtime and the settle delay; the
// page only has to say when a handle is down (audition waits for the release)
// and when the panel is gone (the runtime goes with it).
Dialog {
    id: root

    required property var audioSettings

    // The channel set is fixed, so the Repeater is driven by the key list and
    // never resets. Levels arrive separately, through `levels`: feeding the
    // Repeater the value rows instead would rebuild every delegate on every
    // change — including the slider under the user's finger, which ends the drag.
    property var keys: []
    property var levels: ({})

    function refresh() {
        var rows = root.audioSettings.channels()
        var byKey = {}
        var keyList = []
        for (var i = 0; i < rows.length; ++i) {
            byKey[rows[i].key] = rows[i]
            keyList.push(rows[i].key)
        }
        // Levels first: the delegates read them the moment the key list appears.
        root.levels = byKey
        if (root.keys.length !== keyList.length) {
            root.keys = keyList
        }
    }

    onAboutToShow: root.refresh()
    onClosed: root.audioSettings.releaseAudition()

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
            model: root.keys
            delegate: RowLayout {
                id: channelRow
                required property string modelData
                readonly property var channel: root.levels[modelData]
                Layout.fillWidth: true
                spacing: 8

                Text {
                    Layout.preferredWidth: 96
                    text: channelRow.channel.label
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    elide: Text.ElideRight
                }
                IconButton {
                    glyph: channelRow.channel.muted ? "🔇" : "🔊"
                    tooltip: channelRow.channel.muted ? qsTr("取消静音") : qsTr("静音")
                    onClicked: root.audioSettings.toggleChannelMuted(channelRow.modelData)
                }
                AppSlider {
                    id: levelSlider
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    stepSize: 1
                    onMoved: root.audioSettings.setChannelPercent(
                                 channelRow.modelData, Math.round(value))
                    onPressedChanged: root.audioSettings.setAuditionHeld(pressed)

                    // Dragging writes `value` imperatively, which would kill a
                    // plain binding for good — after one drag the handle would
                    // stop answering 恢复本地默认. A Binding re-applies instead,
                    // and stands down while the handle is held so it cannot
                    // fight the drag.
                    Binding {
                        target: levelSlider
                        property: "value"
                        value: channelRow.channel.percent
                        when: !levelSlider.pressed
                        restoreMode: Binding.RestoreNone
                    }
                }
                // Double-clicking the percentage types it. A short handle
                // cannot land on an exact number, and these are numbers people
                // want exactly.
                EditableValue {
                    Layout.preferredWidth: 44
                    Layout.preferredHeight: 22
                    text: channelRow.channel.percent + "%"
                    value: channelRow.channel.percent
                    from: 0
                    to: 100
                    onCommitted: function(v) {
                        root.audioSettings.setChannelPercent(channelRow.modelData, Math.round(v))
                    }
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
