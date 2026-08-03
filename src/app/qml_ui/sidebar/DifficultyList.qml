pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI

Column {
    id: root

    required property var workbenchState
    required property var documentSession
    required property var commands

    width: parent ? parent.width : implicitWidth

    Item {
        width: root.width
        height: 30

        AbstractButton {
            id: sectionButton
            anchors.left: parent.left
            anchors.right: actions.left
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            hoverEnabled: true
            onClicked: {
                root.workbenchState.difficultySectionExpanded
                    = !root.workbenchState.difficultySectionExpanded
            }

            contentItem: Text {
                leftPadding: 8
                text: (root.workbenchState.difficultySectionExpanded ? "▾  " : "▸  ")
                    + qsTr("难度")
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.secondaryFontSize
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: sectionButton.hovered ? Theme.colors.state.hover : "transparent"
            }

            Tooltip {
                visible: sectionButton.hovered
                text: root.workbenchState.difficultySectionExpanded
                    ? qsTr("折叠难度") : qsTr("展开难度")
            }
        }

        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            HeaderButton {
                text: "+"
                tooltip: qsTr("添加难度")
                enabled: root.documentSession.availableDifficulties.length > 0
                onClicked: addDifficultyMenu.open()
            }
            HeaderButton {
                text: "−"
                tooltip: qsTr("删除当前难度")
                enabled: root.documentSession.currentDifficultyId > 0
                onClicked: removeDifficultyDialog.open()
            }
        }
    }

    Repeater {
        model: root.documentSession.difficulties

        delegate: AbstractButton {
            id: difficultyButton
            required property var modelData
            // 侧边栏只反映编辑器会话中的活动标签。文档模型的
            // currentDifficultyId 负责正文数据源，不参与导航选中状态。
            readonly property bool activeEditor: root.workbenchState.activeEditorKey
                === root.workbenchState.difficultyEditorKey(modelData.id)

            width: root.width
            height: root.workbenchState.difficultySectionExpanded ? 30 : 0
            visible: root.workbenchState.difficultySectionExpanded
            hoverEnabled: true
            onClicked: {
                root.workbenchState.openDifficultyEditor(modelData.id)
            }

            contentItem: Text {
                leftPadding: 20
                text: difficultyButton.modelData.label
                color: difficultyButton.activeEditor
                       ? Theme.colors.text.primary
                       : Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                verticalAlignment: Text.AlignVCenter
            }

            background: Rectangle {
                color: difficultyButton.activeEditor
                       ? Theme.colors.state.menuSelection
                       : difficultyButton.hovered ? Theme.colors.state.hover : "transparent"
            }
        }
    }

    Menu {
        id: addDifficultyMenu

        Repeater {
            model: root.documentSession.availableDifficulties

            delegate: MenuItem {
                required property var modelData
                text: modelData.label
                onTriggered: {
                    if (root.commands.addDifficulty(modelData.id))
                        root.workbenchState.openDifficultyEditor(modelData.id)
                }
            }
        }
    }

    Dialog {
        id: removeDifficultyDialog
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: qsTr("删除当前难度")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.commands.removeDifficulty(root.documentSession.currentDifficultyId)

        Label {
            text: qsTr("当前难度及其正文将从文档中删除。")
            color: Theme.colors.text.primary
        }
    }

    component HeaderButton: AbstractButton {
        id: button

        required property string tooltip
        width: 24
        height: 24
        hoverEnabled: true

        contentItem: Text {
            text: button.text
            color: button.enabled ? Theme.colors.text.primary : Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize + 2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
        background: Rectangle {
            radius: 4
            color: button.down ? Theme.colors.state.pressed
                  : button.hovered ? Theme.colors.state.hover
                  : "transparent"
        }
        Tooltip {
            visible: button.hovered && button.enabled
            text: button.tooltip
        }
    }
}

