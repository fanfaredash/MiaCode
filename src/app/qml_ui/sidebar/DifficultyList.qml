pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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

        ChromeRow {
            id: sectionButton
            anchors.left: parent.left
            anchors.right: actions.left
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            leftPadding: 8
            tone: "hover"
            onClicked: {
                root.viewState.difficultySectionExpanded
                    = !root.viewState.difficultySectionExpanded
            }

            contentItem: Text {
                text: (root.viewState.difficultySectionExpanded ? "▾  " : "▸  ")
                    + qsTr("难度")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.secondaryFontSize
                font.bold: true
                verticalAlignment: Text.AlignVCenter
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
            // `bookmarkGeneration` is an explicit QML binding dependency;
            // invokable return values alone cannot observe document edits.
            readonly property var bookmarks: {
                const generation = root.documentSession.bookmarkGeneration
                return root.documentSession.bookmarksForDifficulty(modelData.id)
            }
            readonly property bool bookmarksExpanded:
                root.viewState.bookmarkGroupExpanded(modelData.id)

            width: root.width
            visible: root.viewState.difficultySectionExpanded

            Item {
                width: parent.width
                height: root.viewState.difficultySectionExpanded ? 30 : 0

                // The fold control sits BESIDE the navigation row, never inside
                // its contentItem: the row's highlight spans the whole row and
                // would otherwise run underneath the chevron.
                NavRow {
                    id: difficultyButton
                    anchors.left: parent.left
                    anchors.right: foldButton.visible ? foldButton.left : parent.right
                    height: parent.height
                    textLeftPadding: 26
                    text: difficultyGroup.modelData.label
                    selected: difficultyGroup.activeEditor
                    onClicked: root.viewState.openDifficultyEditor(difficultyGroup.modelData.id)

                    // Difficulty colour block, same palette as v1's badge icon.
                    Rectangle {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        width: 10
                        height: 10
                        radius: 3
                        color: Theme.difficultyColor(difficultyGroup.modelData.id)
                    }
                }

                IconButton {
                    id: foldButton
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    visible: difficultyGroup.bookmarks.length > 0
                             && root.viewState.difficultySectionExpanded
                    glyph: difficultyGroup.bookmarksExpanded ? "▾" : "▸"
                    tooltip: difficultyGroup.bookmarksExpanded
                        ? qsTr("折叠书签") : qsTr("展开书签")
                    onClicked: root.viewState.setBookmarkGroupExpanded(
                        difficultyGroup.modelData.id, !difficultyGroup.bookmarksExpanded)
                }
            }

            Repeater {
                model: difficultyGroup.bookmarksExpanded ? difficultyGroup.bookmarks : []

                delegate: NavRow {
                    required property var modelData
                    width: parent.width
                    height: root.viewState.difficultySectionExpanded ? 26 : 0
                    textLeftPadding: 30
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
        footer: DialogFooter {
            acceptText: qsTr("确定")
            cancelText: qsTr("取消")
            onAccepted: removeDifficultyDialog.accept()
            onRejected: removeDifficultyDialog.reject()
        }
        onAccepted: root.commands.removeDifficulty(root.documentSession.currentDifficultyId)

        Label {
            text: qsTr("当前难度及其正文将从文档中删除。")
            color: Theme.colors.text.primary
        }
    }
}
