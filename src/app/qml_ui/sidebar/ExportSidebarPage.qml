import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var pages

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
                text: qsTr("视频导出")
                selected: root.pages.activePageId === "export"
                onClicked: root.pages.openVideoExportPage()
            }
            NavRow {
                width: parent.width
                text: qsTr("封面导出")
                onClicked: root.pages.openCoverExport()
            }
            NavRow {
                width: parent.width
                text: qsTr("打包 ZIP")
                onClicked: root.pages.packAsZip()
            }
        }

        ScrollBar.vertical: ScrollBar {}
    }
}
