import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var hostWindow

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

    implicitHeight: 32
    color: Theme.surfaceColor(Theme.colors.background.activityBar)

    component ToolBarButton: IconButton {
        stateColors: Theme.colors.activityState
    }

    WindowGestureArea {
        anchors.fill: parent
        hostWindow: root.hostWindow
        z: 0
    }

    Row {
        id: leftActions
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5
        z: 1

        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/folder-open.svg")
            tooltip: UiText.text("打开")
            onClicked: root.openRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/save.svg")
            tooltip: UiText.text("保存")
            onClicked: root.saveRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/undo.svg")
            tooltip: UiText.text("撤销")
            enabled: root.canUndo
            onClicked: root.undoRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/redo.svg")
            tooltip: UiText.text("重做")
            enabled: root.canRedo
            onClicked: root.redoRequested()
        }
    }

    Row {
        id: rightActions
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5
        z: 1

        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/audio-settings.svg")
            label: UiText.text("音频设置")
            tooltip: UiText.text("音频设置")
            onClicked: root.audioSettingsRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/preview-settings.svg")
            label: UiText.text("预览设置")
            tooltip: UiText.text("预览设置")
            onClicked: root.previewSettingsRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/panel-left.svg")
            tooltip: UiText.text("切换侧栏")
            active: root.sidebarActive
            onClicked: root.toggleSidebarRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/panel-bottom.svg")
            tooltip: UiText.text("切换底部面板")
            active: root.bottomActive
            onClicked: root.toggleBottomRequested()
        }
    }

}
