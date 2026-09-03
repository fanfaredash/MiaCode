import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// Beats/BPM for the blank prepended to track.mp3 or the background video. The
// summary is the point of the dialog: beats and BPM only mean something once
// they are turned into the length that will actually be inserted.
AppDialog {
    id: root

    required property var mediaTools

    property bool isTrack: true
    property string inputName: ""
    property string backupName: ""
    property bool hasBackup: false

    readonly property real beats: beatsField.value
    readonly property real bpm: bpmField.value
    readonly property real blankSeconds: bpm > 0 ? beats * 60 / bpm : 0

    preferredWidth: 440
    footer: DialogFooter {
        acceptText: UiText.text("确定")
        cancelText: UiText.text("取消")
        onAccepted: root.accept()
        onRejected: root.reject()
    }

    function loadContext(context) {
        root.isTrack = context.isTrack
        root.title = context.title
        root.inputName = context.inputName
        root.backupName = context.backupName
        root.hasBackup = context.hasBackup
        beatsField.value = context.beats
        bpmField.value = context.bpm
    }

    function formatNumber(value) {
        return Number(value.toFixed(3)).toString()
    }

    onAccepted: root.mediaTools.applyPrepend(root.isTrack, root.beats, root.bpm)

    body: ColumnLayout {
        spacing: 12

        Text {
            objectName: "prependBlankSummary"
            Layout.fillWidth: true
            text: root.isTrack
                  ? UiText.text("在 %1 开头插入 %2 拍（BPM %3）静音，约 %4 秒。")
                        .arg(root.inputName)
                        .arg(root.formatNumber(root.beats))
                        .arg(root.formatNumber(root.bpm))
                        .arg(root.formatNumber(root.blankSeconds))
                  : UiText.text("在 %1 开头插入 %2 拍（BPM %3）黑屏，约 %4 秒。")
                        .arg(root.inputName)
                        .arg(root.formatNumber(root.beats))
                        .arg(root.formatNumber(root.bpm))
                        .arg(root.formatNumber(root.blankSeconds))
            color: Theme.colors.text.secondary
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.colors.border.normal
        }

        component NumberRow: RowLayout {
            id: numberRow
            required property string label
            property alias value: spin.realValue
            property real minimum: 0
            property real maximum: 1000
            Layout.fillWidth: true
            Text {
                text: numberRow.label
                color: Theme.colors.text.secondary
            }
            AppTextField {
                id: spin
                property real realValue: 0
                Layout.fillWidth: true
                text: Number(spin.realValue.toFixed(3)).toString()
                onEditingFinished: {
                    const parsed = parseFloat(text)
                    if (!isNaN(parsed))
                        spin.realValue = Math.min(numberRow.maximum,
                                                  Math.max(numberRow.minimum, parsed))
                    text = Number(spin.realValue.toFixed(3)).toString()
                }
            }
            AppButton {
                text: UiText.text("检测")
                onClicked: {
                    const detected = root.mediaTools.detectPrependTiming(root.isTrack)
                    numberRow.detected(detected)
                }
            }
            signal detected(var values)
        }

        NumberRow {
            id: beatsField
            objectName: "prependBlankBeatsRow"
            label: UiText.text("拍数")
            minimum: 0.125
            maximum: 512
            value: 4
            onDetected: function(values) {
                if (values.beats !== undefined)
                    beatsField.value = values.beats
            }
        }

        NumberRow {
            id: bpmField
            objectName: "prependBlankBpmRow"
            label: "BPM"
            minimum: 1
            maximum: 999
            value: 120
            onDetected: function(values) {
                if (values.bpm !== undefined)
                    bpmField.value = values.bpm
            }
        }

        AppButton {
            objectName: "prependBlankRestoreButton"
            Layout.alignment: Qt.AlignLeft
            visible: root.hasBackup
            text: UiText.text("恢复备份 (%1)").arg(root.backupName)
            onClicked: {
                root.close()
                root.mediaTools.restorePrependBackup(root.isTrack)
            }
        }
    }
}
