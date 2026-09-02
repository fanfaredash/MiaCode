import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// The PV batch queue. Progress and cancellation live on the shell's shared
// overlay, so this page shows only what the overlay cannot: which folder each
// job came from and how each one ended.
Dialog {
    id: root

    enter: FadeTransition {}
    exit: FadeTransition { appearing: false }
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    required property var mediaTools

    title: UiText.text("批量压缩 PV")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(680, Overlay.overlay ? Overlay.overlay.width - 48 : 680)
    height: Math.min(520, Overlay.overlay ? Overlay.overlay.height - 48 : 520)
    footer: DialogFooter {
        cancelText: UiText.text("关闭")
        onRejected: root.reject()
    }
    closePolicy: Popup.CloseOnEscape

    readonly property bool busy: !!root.mediaTools && root.mediaTools.batchRunning

    contentItem: ColumnLayout {
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            AppTextField {
                objectName: "pvBatchDirectoryField"
                Layout.fillWidth: true
                readOnly: true
                text: root.mediaTools ? root.mediaTools.batchDirectory : ""
                placeholderText: UiText.text("选择要扫描的目录")
            }
            AppButton {
                objectName: "pvBatchBrowseButton"
                text: UiText.text("浏览...")
                enabled: !root.busy
                onClicked: root.mediaTools.chooseBatchDirectory()
            }
            AppButton {
                objectName: "pvBatchAddButton"
                text: UiText.text("添加")
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
                    text: UiText.text("移除")
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
                text: UiText.text("清空队列")
                enabled: !root.busy
                onClicked: root.mediaTools.clearBatchQueue()
            }
            Item { Layout.fillWidth: true }
            AppButton {
                objectName: "pvBatchStartButton"
                text: UiText.text("开始压缩")
                emphasized: true
                enabled: !root.busy && !!root.mediaTools
                         && root.mediaTools.batchJobs.length > 0
                onClicked: root.mediaTools.startBatchCompression()
            }
        }
    }
}
