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

    title: qsTrId("media_tools.audio_video_processing")
    preferredWidth: 560
    preferredHeight: implicitHeight
    footer: DialogFooter {
        cancelText: qsTrId("action.close")
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
            label: qsTrId("qml.convert_sample_rate")
            description: qsTrId("qml.convert_the_sample_rate_of_track_mp3_to_44100_hz_and_automatical")
            onClicked: root.mediaTools.convertTrackTo44100Hz()
        }
        ToolRow {
            objectName: "mediaToolPrependTrack"
            label: qsTrId("qml.prepend_blank_audio")
            description: qsTrId("qml.insert_blank_audio_of_the_specified_duration_at_the_beginning_of")
            onClicked: root.prependRequested(true)
        }
        ToolRow {
            objectName: "mediaToolPrependPv"
            label: qsTrId("qml.prepend_black_screen")
            description: qsTrId("qml.insert_a_black_screen_of_the_specified_duration_at_the_beginning")
            onClicked: root.prependRequested(false)
        }
        ToolRow {
            objectName: "mediaToolCompressVideo"
            label: qsTrId("media_tools.batch_pv_start")
            description: qsTrId("media_tools.compress_the_background_video_under")
            onClicked: root.mediaTools.compressBackgroundVideo()
        }
        ToolRow {
            objectName: "mediaToolBatchPv"
            label: qsTrId("qml.batch_compress_videos")
            description: qsTrId("media_tools.batch_pv_description")
            onClicked: batchPage.open()
        }
    }

    PvBatchCompressionDialog {
        id: batchPage
        objectName: "pvBatchCompressionDialog"
        mediaTools: root.mediaTools
    }
}
