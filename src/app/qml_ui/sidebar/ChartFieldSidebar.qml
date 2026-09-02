import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var viewState
    required property var documentSession
    required property var commands
    property var pages

    color: Theme.surfaceColor("panel", Theme.colors.background.panel)
    topLeftRadius: 10
    clip: true

    PanelHeader {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: UiText.text("谱面")
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

            NavRow {
                width: parent.width
                text: UiText.text("元数据")
                selected: root.viewState.metadataEditorActive
                onClicked: {
                    if (root.pages && root.pages.overlayActive)
                        root.pages.leaveOverlayPage()
                    root.viewState.activeSidebarView = "chart"
                    root.viewState.openMetadataEditor()
                }
            }

            DifficultyList {
                width: parent.width
                viewState: root.viewState
                documentSession: root.documentSession
                commands: root.commands
            }
        }

        ScrollBar.vertical: AppScrollBar {}
    }
}
