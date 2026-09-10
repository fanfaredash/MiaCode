import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// The PV batch queue. Progress and cancellation live on the shell's shared
// overlay, so this page shows only what the overlay cannot: which folder each
// job came from and how each one ended.
AppDialog {
    id: root

    required property var mediaTools

    title: qsTrId("qml.batch_compress_videos")
    preferredWidth: 680
    preferredHeight: Theme.dialogHeight
    fillBody: true
    footer: DialogFooter {
        cancelText: qsTrId("qml.back")
        onRejected: root.reject()
    }

    readonly property bool busy: !!root.mediaTools && root.mediaTools.batchRunning

    body: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            AppTextField {
                objectName: "pvBatchDirectoryField"
                Layout.fillWidth: true
                readOnly: true
                text: root.mediaTools ? root.mediaTools.batchDirectory : ""
                placeholderText: qsTrId("qml.choose_a_folder_to_scan")
            }
            AppButton {
                objectName: "pvBatchBrowseButton"
                text: qsTrId("action.browse")
                enabled: !root.busy
                onClicked: root.mediaTools.chooseBatchDirectory()
            }
            AppButton {
                objectName: "pvBatchAddButton"
                text: qsTrId("qml.add")
                enabled: !root.busy && !!root.mediaTools
                         && root.mediaTools.batchDirectory.length > 0
                onClicked: root.mediaTools.addBatchFolder()
            }
        }

        ListView {
            objectName: "pvBatchQueue"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: root.mediaTools ? root.mediaTools.batchJobs : []
            ScrollBar.vertical: AppScrollBar {}
            delegate: RowLayout {
                width: ListView.view.width
                required property int index
                required property var modelData
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 0
                    Text {
                        Layout.fillWidth: true
                        text: modelData.displayName
                        elide: Text.ElideMiddle
                        color: modelData.hasVideo
                               ? Theme.colors.text.active
                               : Theme.colors.text.disabled
                    }
                    Text {
                        Layout.fillWidth: true
                        text: modelData.status.length > 0 ? modelData.status : modelData.size
                        elide: Text.ElideRight
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                    }
                }
                AppButton {
                    text: qsTrId("qml.remove")
                    enabled: !root.busy
                    onClicked: root.mediaTools.removeBatchJob(index)
                }
            }
        }

        Text {
            objectName: "pvBatchSummary"
            Layout.fillWidth: true
            text: root.mediaTools ? root.mediaTools.batchSummary : ""
            color: Theme.colors.text.secondary
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            AppButton {
                text: qsTrId("media_tools.batch_pv_clear_queue")
                enabled: !root.busy
                onClicked: root.mediaTools.clearBatchQueue()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "pvBatchStartButton"
                text: qsTrId("qml.start_compression")
                emphasized: true
                enabled: !root.busy && !!root.mediaTools
                         && root.mediaTools.batchJobs.length > 0
                onClicked: root.mediaTools.startBatchCompression()
            }
        }
    }
}
