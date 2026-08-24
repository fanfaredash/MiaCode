pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI

Column {
    id: root

    required property var viewState
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
                root.viewState.difficultySectionExpanded
                    = !root.viewState.difficultySectionExpanded
            }

            contentItem: Text {
                leftPadding: 8
                text: (root.viewState.difficultySectionExpanded ? "▾  " : "▸  ")
                    + qsTr("难度")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.secondaryFontSize
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }

            background: HoverChrome {
                hovered: sectionButton.hovered
                tone: "hover"
            }

            Tooltip {
                visible: sectionButton.hovered
                text: root.viewState.difficultySectionExpanded
                    ? qsTr("折叠难度") : qsTr("展开难度")
            }
        }

        Row {
            id: actions
            anchors.right: parent.right
            anchors.rightMargin: 4
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2

            IconButton {
                glyph: "+"
                tooltip: qsTr("添加难度")
                enabled: root.documentSession.availableDifficulties.length > 0
                onClicked: addDifficultyMenu.open()
            }
            IconButton {
                glyph: "−"
                tooltip: qsTr("删除当前难度")
                enabled: root.documentSession.currentDifficultyId > 0
                onClicked: removeDifficultyDialog.open()
            }
        }
    }

    Repeater {
        model: root.documentSession.difficulties

        delegate: Column {
            id: difficultyGroup
            required property var modelData
            // 侧边栏只反映编辑器会话中的活动标签。文档模型的
            // currentDifficultyId 负责正文数据源，不参与导航选中状态。
            readonly property bool activeEditor: root.viewState.activeEditorKey
                === root.viewState.difficultyEditorKey(modelData.id)

            width: root.width
            visible: root.viewState.difficultySectionExpanded

            NavRow {
                id: difficultyButton
                width: parent.width
                height: root.viewState.difficultySectionExpanded ? 30 : 0
                text: difficultyGroup.modelData.label
                selected: difficultyGroup.activeEditor
                onClicked: root.viewState.openDifficultyEditor(difficultyGroup.modelData.id)
            }

            Repeater {
                // `bookmarkGeneration` is an explicit QML binding dependency;
                // invokable return values alone cannot observe document edits.
                model: {
                    const generation = root.documentSession.bookmarkGeneration
                    return root.documentSession.bookmarksForDifficulty(
                        difficultyGroup.modelData.id)
                }

                delegate: NavRow {
                    required property var modelData
                    width: parent.width
                    height: root.viewState.difficultySectionExpanded ? 26 : 0
                    textLeftPadding: 24
                    text: qsTr("书签 %1：%2").arg(modelData.line).arg(modelData.title)
                    Accessible.name: qsTr("%1，第 %2 行").arg(modelData.title).arg(modelData.line)
                    onClicked: {
                        root.viewState.openDifficultyEditor(difficultyGroup.modelData.id)
                        root.documentSession.navigateToBookmark(
                            difficultyGroup.modelData.id, modelData.line)
                    }
                }
            }
        }
    }

    AppMenu {
        id: addDifficultyMenu

        Repeater {
            model: root.documentSession.availableDifficulties

            delegate: AppMenuItem {
                required property var modelData
                text: modelData.label
                onTriggered: {
                    if (root.commands.addDifficulty(modelData.id))
                        root.viewState.openDifficultyEditor(modelData.id)
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
}
