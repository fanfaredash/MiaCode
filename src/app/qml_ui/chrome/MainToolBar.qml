import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    signal toggleSidebarRequested()
    signal toggleBottomRequested()
    signal openRequested()
    signal saveRequested()
    signal undoRequested()
    signal redoRequested()
    signal audioSettingsRequested()
    signal previewSettingsRequested()
    signal unavailableFeatureRequested(string featureName)

    property bool sidebarActive: false
    property bool bottomActive: false
    property bool canUndo: false
    property bool canRedo: false

    implicitHeight: 36
    color: Theme.colors.background.surface

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5

        IconButton {
            iconSource: Qt.resolvedUrl("icons/folder-open.svg")
            tooltip: UiText.text("打开")
            onClicked: root.openRequested()
        }
        IconButton {
            iconSource: Qt.resolvedUrl("icons/save.svg")
            tooltip: UiText.text("保存")
            onClicked: root.saveRequested()
        }
        IconButton {
            iconSource: Qt.resolvedUrl("icons/undo.svg")
            tooltip: UiText.text("撤销")
            enabled: root.canUndo
            onClicked: root.undoRequested()
        }
        IconButton {
            iconSource: Qt.resolvedUrl("icons/redo.svg")
            tooltip: UiText.text("重做")
            enabled: root.canRedo
            onClicked: root.redoRequested()
        }
        ToolSeparator {}
        IconButton {
            iconSource: Qt.resolvedUrl("icons/audio-settings.svg")
            tooltip: UiText.text("音频设置")
            onClicked: root.audioSettingsRequested()
        }
        IconButton {
            iconSource: Qt.resolvedUrl("icons/preview-settings.svg")
            tooltip: UiText.text("预览设置")
            onClicked: root.previewSettingsRequested()
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5

        IconButton {
            iconSource: Qt.resolvedUrl("icons/panel-left.svg")
            tooltip: UiText.text("切换侧栏 (Ctrl+B)")
            active: root.sidebarActive
            onClicked: root.toggleSidebarRequested()
        }
        IconButton {
            iconSource: Qt.resolvedUrl("icons/panel-bottom.svg")
            tooltip: UiText.text("切换底部面板")
            active: root.bottomActive
            onClicked: root.toggleBottomRequested()
        }
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.colors.border.normal
    }

    component ToolSeparator: Rectangle {
        width: 1
        height: 20
        color: Theme.colors.border.normal
    }
}
