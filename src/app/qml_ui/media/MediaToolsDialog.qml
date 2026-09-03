import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 音视频处理. A launcher for the single-file tools plus the PV batch queue;
// every tool reports through the shell's shared notice and progress surfaces,
// so this dialog owns no result UI of its own.
AppDialog {
    id: root

    required property var mediaTools

    title: UiText.text("音视频处理")
    preferredWidth: 560
    preferredHeight: Theme.dialogHeight
    footer: DialogFooter {
        cancelText: UiText.text("关闭")
        onRejected: root.reject()
    }

    signal prependRequested(bool isTrack)

    body: ColumnLayout {
        spacing: 4

        component ToolRow: ChromeRow {
            id: toolRow
            required property string label
            required property string description
            Layout.fillWidth: true
            implicitHeight: rowLayout.implicitHeight + 16
            leftPadding: 12
            rightPadding: 12
            contentItem: ColumnLayout {
                id: rowLayout
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: toolRow.label
                    color: Theme.colors.text.active
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
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
        }

        ToolRow {
            objectName: "mediaToolSampleRate"
            label: UiText.text("采样率")
            description: UiText.text("将 track.mp3 转换为 44100Hz，并自动备份原文件。")
            onClicked: {
                root.close()
                root.mediaTools.convertTrackTo44100Hz()
            }
        }
        ToolRow {
            objectName: "mediaToolCompressVideo"
            label: UiText.text("压缩视频")
            description: UiText.text("将背景视频压缩到 20 MiB 以内，并自动备份原文件。")
            onClicked: {
                root.close()
                root.mediaTools.compressBackgroundVideo()
            }
        }
        ToolRow {
            objectName: "mediaToolBatchPv"
            label: UiText.text("批量压缩 PV")
            description: UiText.text("扫描一个目录，批量压缩其中的背景视频。")
            onClicked: {
                root.close()
                batchPage.open()
            }
        }
        ToolRow {
            objectName: "mediaToolPrependTrack"
            label: UiText.text("音轨前置静音")
            description: UiText.text("在 track.mp3 开头插入一段静音，并自动备份原文件。")
            onClicked: {
                root.close()
                root.prependRequested(true)
            }
        }
        ToolRow {
            objectName: "mediaToolPrependPv"
            label: UiText.text("PV 前置黑屏")
            description: UiText.text("在背景视频开头插入一段黑幕，并自动备份原文件。")
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
