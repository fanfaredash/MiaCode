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
        title: qsTr("导出")
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
                text: qsTr("导出中心")
                selected: root.pages.activePageId === "export"
                          && root.selectedAction !== "batch"
                onClicked: {
                    root.selectedAction = "export"
                    root.pages.openExportPage()
                }
            }
            NavRow {
                width: parent.width
                text: qsTr("批量导出")
                selected: root.selectedAction === "batch"
                onClicked: {
                    root.selectedAction = "batch"
                    root.pages.openBatchExport()
                }
            }
        }

        ScrollBar.vertical: ScrollBar {}
    }
}
