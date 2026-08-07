import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var pages
    property string selectedAction: ""

    color: Theme.colors.background.surface
    clip: true

    PanelHeader {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: qsTr("工具")
        showMore: false
    }

    Flickable {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: parent.bottom
        contentHeight: list.implicitHeight + 12
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        Column {
            id: list
            x: 6
            y: 6
            width: parent.width - 12
            spacing: 2

            NavRow {
                width: parent.width
                text: qsTr("延迟校准")
                selected: root.pages.activePageId === "latency"
                onClicked: {
                    root.selectedAction = "latency"
                    root.pages.openLatencyPage()
                }
            }
            NavRow {
                width: parent.width
                text: qsTr("音视频处理")
                selected: root.selectedAction === "media"
                onClicked: {
                    root.selectedAction = "media"
                    root.pages.openMediaProcessingTools()
                }
            }
            NavRow {
                width: parent.width
                text: qsTr("整谱规范化")
                selected: root.selectedAction === "normalize"
                onClicked: {
                    root.selectedAction = "normalize"
                    root.pages.openNormalizeWholeChart()
                }
            }
            NavRow {
                width: parent.width
                text: qsTr("Net 批量下载")
                selected: root.selectedAction === "net-download"
                onClicked: {
                    root.selectedAction = "net-download"
                    root.pages.openNetBatchDownload()
                }
            }
            NavRow {
                width: parent.width
                text: qsTr("Net 批量上传")
                selected: root.selectedAction === "net-upload"
                onClicked: {
                    root.selectedAction = "net-upload"
                    root.pages.openNetBatchUpload()
                }
            }
        }

        ScrollBar.vertical: ScrollBar {}
    }
}
