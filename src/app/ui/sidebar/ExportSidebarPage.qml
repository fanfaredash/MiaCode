import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var pages

    color: Theme.surfaceColor(Theme.colors.background.panel)
    clip: true

    PanelHeader {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: qsTrId("sidebar.export")
        sidebarTitle: true
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
                text: qsTrId("export_page.export_video")
                selected: root.pages.activePageId === "export"
                onClicked: root.pages.openVideoExportPage()
            }
            NavRow {
                width: parent.width
                text: qsTrId("export_page.export_cover")
                onClicked: root.pages.openCoverExport()
            }
            NavRow {
                width: parent.width
                text: qsTrId("export_page.pack_as_zip")
                onClicked: root.pages.packAsZip()
            }
        }

        ScrollBar.vertical: AppScrollBar {}
    }
}
