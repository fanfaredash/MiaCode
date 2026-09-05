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

    title: UiText.text("media_tools.audio_video_processing")
    preferredWidth: 560
    preferredHeight: implicitHeight
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
            label: UiText.text("转换采样率")
            description: UiText.text("将 track.mp3 的采样率转换为 44100 Hz，并自动备份原文件。")
            onClicked: root.mediaTools.convertTrackTo44100Hz()
        }
        ToolRow {
            objectName: "mediaToolPrependTrack"
            label: UiText.text("音频前置空白")
            description: UiText.text("在 track.mp3 开头插入指定时长的空白音频，并自动备份原文件。")
            onClicked: root.prependRequested(true)
        }
        ToolRow {
            objectName: "mediaToolPrependPv"
            label: UiText.text("视频前置黑幕")
            description: UiText.text("在背景视频开头插入指定时长的黑幕，并自动备份原文件。")
            onClicked: root.prependRequested(false)
        }
        ToolRow {
            objectName: "mediaToolCompressVideo"
            label: UiText.text("压缩视频")
            description: UiText.text("将背景视频压缩至 20 MiB 以内，并自动备份原文件。")
            onClicked: root.mediaTools.compressBackgroundVideo()
        }
        ToolRow {
            objectName: "mediaToolBatchPv"
            label: UiText.text("批量压缩视频")
            description: UiText.text("选择目录并批量压缩其中的背景视频。")
            onClicked: batchPage.open()
        }
    }

    PvBatchCompressionDialog {
        id: batchPage
        objectName: "pvBatchCompressionDialog"
        mediaTools: root.mediaTools
    }
}
