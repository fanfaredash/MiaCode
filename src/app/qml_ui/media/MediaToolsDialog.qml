import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 音视频处理. A launcher for the single-file tools plus the PV batch queue;
// every tool reports through the shell's shared notice and progress surfaces,
// so this dialog owns no result UI of its own.
Dialog {
    id: root

    required property var mediaTools

    title: qsTr("音视频处理")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, Overlay.overlay ? Overlay.overlay.width - 48 : 560)
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape

    signal prependRequested(bool isTrack)

    contentItem: ColumnLayout {
        spacing: 4

        component ToolRow: AbstractButton {
            id: toolRow
            required property string label
            required property string description
            Layout.fillWidth: true
            implicitHeight: rowLayout.implicitHeight + 16
            contentItem: ColumnLayout {
                id: rowLayout
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: toolRow.label
                    color: Theme.colors.text.active
                    font.family: Theme.uiFont
                    font.bold: true
                }
                Text {
                    Layout.fillWidth: true
                    text: toolRow.description
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.secondaryFontSize
                    wrapMode: Text.WordWrap
                }
            }
            background: HoverChrome {
                hovered: toolRow.hovered
                pressed: toolRow.down
                tone: "nav"
            }
        }

        ToolRow {
            objectName: "mediaToolSampleRate"
            label: qsTr("采样率")
            description: qsTr("把 track.mp3 转换为 44100 Hz。")
            onClicked: {
                root.close()
                root.mediaTools.convertTrackTo44100Hz()
            }
        }
        ToolRow {
            objectName: "mediaToolCompressVideo"
            label: qsTr("压缩视频")
            description: qsTr("把背景视频压缩到 20 MiB 以内。")
            onClicked: {
                root.close()
                root.mediaTools.compressBackgroundVideo()
            }
        }
        ToolRow {
            objectName: "mediaToolBatchPv"
            label: qsTr("批量压缩 PV")
            description: qsTr("扫描一个目录，批量压缩其中的背景视频。")
            onClicked: {
                root.close()
                batchPage.open()
            }
        }
        ToolRow {
            objectName: "mediaToolPrependTrack"
            label: qsTr("音轨前置静音")
            description: qsTr("在 track.mp3 开头插入一段静音。")
            onClicked: {
                root.close()
                root.prependRequested(true)
            }
        }
        ToolRow {
            objectName: "mediaToolPrependPv"
            label: qsTr("PV 前置黑屏")
            description: qsTr("在背景视频开头插入一段黑屏。")
            onClicked: {
                root.close()
                root.prependRequested(false)
            }
        }
    }

    PvBatchCompressionDialog {
        id: batchPage
        objectName: "pvBatchCompressionDialog"
        mediaTools: root.mediaTools
    }
}
