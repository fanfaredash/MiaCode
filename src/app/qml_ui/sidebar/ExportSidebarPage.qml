import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var pages
    property string selectedAction: ""

    color: Theme.colors.background.workbench
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

            SidebarListButton {
                width: parent.width
                text: qsTr("导出中心")
                selected: root.pages.activePageId === "export"
                          && root.selectedAction !== "batch"
                onClicked: {
                    root.selectedAction = "export"
                    root.pages.openExportPage()
                }
            }
            SidebarListButton {
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

    component SidebarListButton: AbstractButton {
        id: button
        property bool selected: false
        height: 30
        hoverEnabled: true
        contentItem: Text {
            leftPadding: 20
            text: button.text
            color: Theme.colors.text.primary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            color: button.selected ? Theme.colors.state.menuSelection
                  : button.hovered ? Theme.colors.state.hover
                  : "transparent"
        }
    }
}
