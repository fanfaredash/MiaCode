import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var workbenchState
    required property var documentSession
    required property var commands

    color: Theme.colors.background.workbench
    clip: true

    PanelHeader {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: qsTr("谱面")
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

            AbstractButton {
                id: infoButton
                width: parent.width
                height: 30
                hoverEnabled: true
                onClicked: root.workbenchState.openMetadataEditor()
                contentItem: Text {
                    leftPadding: 20
                    text: qsTr("元数据")
                    color: Theme.colors.text.primary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: root.workbenchState.metadataEditorActive
                           ? Theme.colors.state.menuSelection
                           : infoButton.hovered ? Theme.colors.state.hover : "transparent"
                }
            }

            DifficultyList {
                width: parent.width
                workbenchState: root.workbenchState
                documentSession: root.documentSession
                commands: root.commands
            }
        }

        ScrollBar.vertical: ScrollBar {}
    }
}

