pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var viewState
    required property var documentSession

    readonly property int minimumTabWidth: 100
    readonly property int preferredTabWidth: 160
    readonly property int tabCount: viewState.openEditorTabs.length
    readonly property bool tabsOverflow: tabCount * minimumTabWidth > width
    readonly property real tabWidth: tabCount === 0 ? preferredTabWidth
        : tabsOverflow ? minimumTabWidth
        : Math.min(preferredTabWidth, width / tabCount)

    function difficultyData(id) {
        const difficulties = root.documentSession.difficulties
        for (let index = 0; index < difficulties.length; ++index) {
            if (difficulties[index].id === id)
                return difficulties[index]
        }
        return null
    }

    function difficultyIdForKey(key) {
        return key.startsWith("difficulty:")
            ? Number(key.substring("difficulty:".length))
            : 0
    }

    function titleForKey(key) {
        if (key === viewState.metadataEditorKey)
            return qsTr("元数据")
        const difficulty = difficultyData(difficultyIdForKey(key))
        return difficulty ? difficulty.label : qsTr("难度")
    }

    function displayTitleForKey(key) {
        const dirty = root.documentSession.dirtyEditorKeys.indexOf(key) >= 0
        return (dirty ? "*" : "") + titleForKey(key)
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
            result += qsTr(" · 谱师：%1").arg(difficulty.designer)
        return result
    }

    function activateTab(key) {
        viewState.activateEditor(key)
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
    color: Theme.colors.background.surface

    Flickable {
        id: tabViewport

        anchors.left: parent.left
        anchors.right: root.tabsOverflow ? overflowButton.left : parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        contentWidth: tabRow.width
        contentHeight: height
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        ScrollBar.horizontal: ScrollBar {
            policy: root.tabsOverflow ? ScrollBar.AsNeeded : ScrollBar.AlwaysOff
            height: 3
        }

        Row {
            id: tabRow

            width: childrenRect.width
            height: parent.height

            Repeater {
                id: tabRepeater
                model: root.viewState.openEditorTabs

                delegate: AppTab {
                    required property string modelData

                    width: root.tabWidth
                    height: parent.height
                    preferredTabWidth: root.tabWidth
                    text: root.displayTitleForKey(modelData)
                    secondaryText: ""
                    iconSource: modelData === root.viewState.metadataEditorKey
                        ? Qt.resolvedUrl("icons/metadata.svg")
                        : Qt.resolvedUrl("icons/chart.svg")
                    tooltip: root.tooltipForKey(modelData)
                    active: root.viewState.activeEditorKey === modelData
                    closable: true
                    onClicked: root.activateTab(modelData)
                    onCloseRequested: root.viewState.closeEditor(modelData)
                }
            }
        }
    }

    IconButton {
        id: overflowButton

        anchors.right: parent.right
        anchors.top: parent.top
        width: 30
        height: parent.height
        visible: root.tabsOverflow
        iconSource: Qt.resolvedUrl("icons/more.svg")
        tooltip: qsTr("显示所有已打开的编辑器")
        onClicked: overflowMenu.open()

        AppMenu {
            id: overflowMenu
            x: parent.width - width
            y: parent.height

            Repeater {
                model: root.viewState.openEditorTabs

                delegate: AppMenuItem {
                    required property string modelData

                    text: root.displayTitleForKey(modelData)
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

