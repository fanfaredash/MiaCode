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

    component FoldIndicator: Text {
        property bool expanded: false

        width: 10
        text: expanded ? "▾" : "▸"
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

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
            onClicked: {
                root.viewState.difficultySectionExpanded
                    = !root.viewState.difficultySectionExpanded
            }

            contentItem: Item {
                FoldIndicator {
                    id: sectionFoldIndicator
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    expanded: root.viewState.difficultySectionExpanded
                }

                Text {
                    anchors.left: sectionFoldIndicator.right
                    anchors.leftMargin: 8
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    text: UiText.text("难度")
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.secondaryFontSize
                    font.bold: true
                    verticalAlignment: Text.AlignVCenter
                }
            }

            Tooltip {
                visible: sectionButton.hovered
                text: root.viewState.difficultySectionExpanded
                    ? UiText.text("折叠难度") : UiText.text("展开难度")
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
                tooltip: UiText.text("添加难度")
                enabled: root.documentSession.availableDifficulties.length > 0
                onClicked: addDifficultyMenu.open()
            }
            IconButton {
                glyph: "−"
                tooltip: UiText.text("删除当前难度")
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
            // Only the current difficulty shows its bookmarks. The row is the
            // fold control now, and a row that is not current switches instead
            // of folding — so an expanded state left behind on some other
            // difficulty would be showing a list nothing on screen can close.
            readonly property bool bookmarksExpanded: activeEditor
                && root.viewState.bookmarkGroupExpanded(modelData.id)
            // What a click on this row means: fold when it is already the one
            // being edited and it has bookmarks, switch to it otherwise.
            readonly property bool foldsBookmarks: activeEditor && bookmarks.length > 0

            width: root.width
            visible: root.viewState.difficultySectionExpanded

            Item {
                width: parent.width
                height: root.viewState.difficultySectionExpanded ? 30 : 0

                // The row IS the fold control: clicking a difficulty that is not
                // the one being edited switches to it, exactly as before, and
                // clicking the one already being edited folds its bookmarks.
                // A separate chevron button on the right was the first attempt
                // and read backwards — the thing being folded is below and to
                // the left of what you press.
                NavRow {
                    id: difficultyButton
                    anchors.left: parent.left
                    anchors.right: parent.right
                    height: parent.height
                    textLeftPadding: 38
                    text: difficultyGroup.modelData.label
                    selected: difficultyGroup.activeEditor
                    onClicked: {
                        if (difficultyGroup.foldsBookmarks)
                            root.viewState.setBookmarkGroupExpanded(
                                difficultyGroup.modelData.id,
                                !difficultyGroup.bookmarksExpanded)
                        else
                            root.viewState.openDifficultyEditor(difficultyGroup.modelData.id)
                    }

                    // Fold indicator, left of the colour block and aligned with
                    // the 难度 section header's own chevron. The slot is always
                    // reserved so every colour block lines up; only the glyph is
                    // conditional.
                    FoldIndicator {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        visible: difficultyGroup.bookmarks.length > 0
                        expanded: difficultyGroup.bookmarksExpanded
                        color: difficultyGroup.activeEditor
                               ? Theme.colors.text.active
                               : Theme.colors.text.secondary
                    }

                    DifficultySwatch {
                        anchors.left: parent.left
                        anchors.leftMargin: 22
                        anchors.verticalCenter: parent.verticalCenter
                        difficultyId: difficultyGroup.modelData.id
                    }

                    Tooltip {
                        visible: difficultyButton.hovered
                        text: difficultyGroup.foldsBookmarks
                            ? (difficultyGroup.bookmarksExpanded
                               ? UiText.text("折叠书签") : UiText.text("展开书签"))
                            : difficultyGroup.modelData.label
                    }
                }
            }

            Repeater {
                model: difficultyGroup.bookmarksExpanded ? difficultyGroup.bookmarks : []

                delegate: NavRow {
                    id: bookmarkRow
                    required property var modelData
                    width: parent.width
                    height: root.viewState.difficultySectionExpanded ? 26 : 0
                    // Past the difficulty label's own 38, so a bookmark reads as
                    // belonging to the row above it rather than sitting level
                    // with it.
                    textLeftPadding: 38
                    Accessible.name: UiText.text("%1，第 %2 行").arg(modelData.title).arg(modelData.line)


                    contentItem: Item {
                        Rectangle {
                            id: lineBadge
                            anchors.left: parent.left
                            anchors.verticalCenter: parent.verticalCenter
                            width: Math.max(18, lineNumber.implicitWidth + 10)
                            height: 16
                            radius: 4
                            color: {
                                const accent = Qt.color(Theme.colors.accent.primary)
                                return Qt.rgba(accent.r, accent.g, accent.b,
                                               Theme.darkTheme ? 56 / 255 : 32 / 255)
                            }

                            Text {
                                id: lineNumber
                                anchors.centerIn: parent
                                text: String(bookmarkRow.modelData.line)
                                color: Theme.colors.text.primary
                                font.family: Theme.uiFont
                                font.pixelSize: Math.max(9, Theme.uiFontSize - 2)
                            }
                        }

                        Text {
                            anchors.left: lineBadge.right
                            anchors.leftMargin: 6
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            text: bookmarkRow.modelData.title
                            color: !bookmarkRow.enabled ? Theme.colors.text.disabled
                                   : bookmarkRow.selected ? Theme.colors.text.active
                                   : Theme.colors.text.secondary
                            font.family: Theme.uiFont
                            font.pixelSize: Math.max(1, Theme.uiFontSize - 1)
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                        }
                    }
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
                difficultyId: modelData.id
                onTriggered: {
                    if (root.commands.addDifficulty(modelData.id))
                        root.viewState.openDifficultyEditor(modelData.id)
                }
            }
        }
    }

    Dialog {
        id: removeDifficultyDialog

        enter: FadeTransition {}
        exit: FadeTransition { appearing: false }
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        parent: Overlay.overlay
        anchors.centerIn: parent
        modal: true
        title: UiText.text("删除当前难度")
        footer: DialogFooter {
            acceptText: UiText.text("确定")
            cancelText: UiText.text("取消")
            onAccepted: removeDifficultyDialog.accept()
            onRejected: removeDifficultyDialog.reject()
        }
        onAccepted: root.commands.removeDifficulty(root.documentSession.currentDifficultyId)

        Label {
            text: UiText.text("当前难度及其正文将从文档中删除。")
            color: Theme.colors.text.primary
        }
    }
}
