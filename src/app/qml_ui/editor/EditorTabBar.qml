pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var viewState
    required property var documentSession
    required property var commands

    readonly property int minimumTabWidth: 100
    readonly property int preferredTabWidth: 160
    readonly property int tabSpacing: 8
    readonly property int tabCount: viewState.openEditorTabs.length
    readonly property real availableTabWidth: width - 2 * (Theme.panelPadding - Theme.chromeInsetX)
        - Math.max(0, tabCount - 1) * (tabSpacing - 2 * Theme.chromeInsetX)
    readonly property bool tabsOverflow: tabCount * minimumTabWidth > availableTabWidth
    readonly property real tabWidth: tabCount === 0 ? preferredTabWidth
        : tabsOverflow ? minimumTabWidth
        : Math.min(preferredTabWidth, availableTabWidth / tabCount)

    function difficultyData(id) {
        const difficulties = root.documentSession.difficulties
        for (let index = 0; index < difficulties.length; ++index) {
            if (difficulties[index].id === id)
                return difficulties[index]
        }
        return null
    }

    // Closing any dirty editor view asks first. A difficulty tab discards only
    // that saved section; metadata is the whole-file view, so its discard uses
    // section id 0 and restores the complete document save point.
    function requestCloseTab(key) {
        const difficultyId = root.difficultyIdForKey(key)
        const sectionDirty = root.documentSession.dirtyEditorKeys.indexOf(key) >= 0
        if (!sectionDirty) {
            root.viewState.closeEditor(key)
            return
        }
        root.closingKey = key
        root.closingDifficultyId = difficultyId
        closeTabDialog.title = UiText.text("dialog.unsaved_tab_changes.title")
        closeTabDialog.message = UiText.text("dialog.unsaved_tab_changes.message")
            .arg(root.titleForKey(key))
        closeTabDialog.details = difficultyId > 0
            ? UiText.text("dialog.unsaved_tab_changes.details.difficulty")
            : UiText.text("dialog.unsaved_tab_changes.details.document")
        closeTabDialog.open()
    }

    function difficultyIdForKey(key) {
        return key.startsWith("difficulty:")
            ? Number(key.substring("difficulty:".length))
            : 0
    }

    function titleForKey(key) {
        if (key === viewState.metadataEditorKey)
            return UiText.text("dialog.unsaved_field_changes.field.metadata")
        const difficulty = difficultyData(difficultyIdForKey(key))
        return difficulty ? difficulty.label : UiText.text("难度")
    }

    function tooltipForKey(key) {
        if (key === viewState.metadataEditorKey)
            return ""
        const difficulty = difficultyData(difficultyIdForKey(key))
        if (!difficulty)
            return ""
        const fileIdentity = root.documentSession.currentFilePath.length > 0
            ? root.documentSession.currentFilePath
            : root.documentSession.currentFileName
        let result = fileIdentity + "\n" + difficulty.label
        if (difficulty.designer.length > 0)
            result += UiText.text(" · 谱师：%1").arg(difficulty.designer)
        return result
    }

    function activateTab(key) {
        viewState.activateEditor(key)
    }

    function editorKeyAt(rowX) {
        for (let index = 0; index < tabRepeater.count; ++index) {
            const key = root.viewState.openEditorTabs[index]
            const item = tabRepeater.itemAt(index)
            if (!item)
                continue
            if (rowX >= item.x && rowX < item.x + item.width)
                return key
        }
        return ""
    }

    function revealActiveTab() {
        const index = viewState.openEditorTabs.indexOf(viewState.activeEditorKey)
        const item = index >= 0 ? tabRepeater.itemAt(index) : null
        if (!item)
            return
        if (item.x < tabViewport.contentX)
            tabViewport.contentX = item.x
        else if (item.x + item.width > tabViewport.contentX + tabViewport.width)
            tabViewport.contentX = item.x + item.width - tabViewport.width
    }

    implicitHeight: 34
    color: Theme.surfaceColor(Theme.colors.background.panel)

    Flickable {
        id: tabViewport

        anchors.left: parent.left
        anchors.right: root.tabsOverflow ? overflowButton.left : parent.right
        anchors.leftMargin: Theme.panelPadding - Theme.chromeInsetX
        anchors.rightMargin: Theme.panelPadding - Theme.chromeInsetX
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        contentWidth: tabRow.width
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        interactive: root.draggingEditorKey.length === 0
        ScrollBar.horizontal: AppScrollBar {
            policy: root.tabsOverflow ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            height: 3
        }

        Row {
            id: tabRow

            width: childrenRect.width
            height: parent.height
            spacing: root.tabSpacing - 2 * Theme.chromeInsetX

            Repeater {
                id: tabRepeater
                model: root.viewState.openEditorTabs

                delegate: AppTab {
                    required property string modelData

                    property bool suppressClickAfterDrag: false

                    width: root.tabWidth
                    height: parent.height
                    preferredTabWidth: root.tabWidth
                    text: (root.documentSession.dirtyEditorKeys.indexOf(modelData) >= 0 ? "*" : "")
                          + root.titleForKey(modelData)
                    secondaryText: ""
                    iconSource: modelData === root.viewState.metadataEditorKey
                        ? Qt.resolvedUrl("icons/metadata.svg")
                        : ""
                    difficultyId: root.difficultyIdForKey(modelData)
                    tooltip: root.tooltipForKey(modelData)
                    active: root.viewState.activeEditorKey === modelData
                    closable: true
                    opacity: tabDrag.active ? 0.65 : 1
                    onClicked: {
                        if (!tabDrag.active && !suppressClickAfterDrag)
                            root.activateTab(modelData)
                    }
                    onCloseRequested: root.requestCloseTab(modelData)

                    DragHandler {
                        id: tabDrag

                        target: null
                        acceptedButtons: Qt.LeftButton

                        onActiveChanged: {
                            if (active) {
                                root.draggingEditorKey = modelData
                                return
                            }
                            if (root.draggingEditorKey !== modelData)
                                return
                            root.draggingEditorKey = ""
                            const positionInRow = tabRow.mapFromItem(
                                parent, centroid.position.x, centroid.position.y)
                            const targetKey = root.editorKeyAt(positionInRow.x)
                            if (targetKey.length > 0)
                                root.viewState.swapEditorTabs(modelData, targetKey)
                            suppressClickAfterDrag = true
                            Qt.callLater(function() { suppressClickAfterDrag = false })
                        }
                    }
                }
            }
        }
    }

    property string closingKey: ""
    property int closingDifficultyId: 0
    property string pendingSaveCloseKey: ""
    property int pendingSaveCloseDifficultyId: 0

    Connections {
        target: root.documentSession
        function onSectionSaveFinished(difficultyId, saved) {
            if (root.pendingSaveCloseKey.length === 0
                    || difficultyId !== root.pendingSaveCloseDifficultyId)
                return
            const key = root.pendingSaveCloseKey
            root.pendingSaveCloseKey = ""
            root.pendingSaveCloseDifficultyId = 0
            if (saved)
                root.viewState.closeEditor(key)
        }
    }
    property string draggingEditorKey: ""

    // 保存 writes the requested section through the file service. For
    // difficulty tabs it preserves every other dirty section; for metadata
    // (section id 0) it writes the whole document.
    ChoiceDialog {
        id: closeTabDialog
        objectName: "editorTabCloseDialog"
        details: ""
        choices: [
            { id: "save", label: UiText.text("保存"), role: "accept" },
            { id: "discard", label: UiText.text("放弃"), role: "destructive" },
            { id: "cancel", label: UiText.text("取消"), role: "reject" }
        ]
        dismissChoiceId: "cancel"

        onChosen: function(choiceId) {
            const key = root.closingKey
            const difficultyId = root.closingDifficultyId
            root.closingKey = ""
            root.closingDifficultyId = 0
            if (choiceId === "cancel" || key.length === 0)
                return
            if (choiceId === "save") {
                // Saves this difficulty, because that is the thing being
                // closed. A never-saved chart has no path yet, so the save may
                // have to ask for one — the answer arrives on
                // sectionSaveFinished, and only a save that landed closes the
                // tab. A failed or cancelled one keeps it: the edits are still
                // only in memory, and closing is the one thing that loses them.
                root.pendingSaveCloseKey = key
                root.pendingSaveCloseDifficultyId = difficultyId
                root.documentSession.requestSaveDifficultySection(difficultyId)
                return
            } else {
                root.documentSession.revertDifficultyChart(difficultyId)
            }
            root.viewState.closeEditor(key)
        }
    }

    IconButton {
        id: overflowButton

        anchors.right: parent.right
        anchors.rightMargin: Theme.panelPadding - horizontalInset
        anchors.top: parent.top
        width: 30
        height: parent.height
        visible: root.tabsOverflow
        iconSource: Qt.resolvedUrl("icons/more.svg")
        tooltip: UiText.text("显示所有已打开的编辑器")
        onClicked: overflowMenu.open()

        AppMenu {
            id: overflowMenu
            x: parent.width - width
            y: parent.height

            Repeater {
                model: root.viewState.openEditorTabs

                delegate: AppMenuItem {
                    required property string modelData

                    text: (root.documentSession.dirtyEditorKeys.indexOf(modelData) >= 0 ? "*" : "")
                          + root.titleForKey(modelData)
                    difficultyId: root.difficultyIdForKey(modelData)
                    checkable: true
                    checked: root.viewState.activeEditorKey === modelData
                    onTriggered: root.activateTab(modelData)
                }
            }
        }
    }

    Connections {
        target: root.viewState

        function onActiveEditorKeyChanged() {
            Qt.callLater(root.revealActiveTab)
        }

        function onOpenEditorTabsChanged() {
            Qt.callLater(root.revealActiveTab)
        }
    }
}
