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
    property bool saveEnabled: true
    property bool bottomPanelEnabled: true
    property bool canUndo: false
    property bool canRedo: false
    property bool integratedInTitleBar: false
    property real titleBarLeadingInset: 0

    readonly property real leadingActionsRight: leftActions.x + leftActions.width
    readonly property real trailingActionsWidth: width - rightActions.x

    implicitHeight: 32
    color: root.integratedInTitleBar
           ? "transparent"
           : Theme.surfaceColor(Theme.colors.background.activityBar)

    component ToolBarButton: IconButton {
        stateColors: Theme.colors.activityState
    }

    WindowGestureArea {
        anchors.fill: parent
        hostWindow: root.hostWindow
        visible: !root.integratedInTitleBar
        z: 0
    }

    Row {
        id: leftActions
        anchors.left: parent.left
        anchors.leftMargin: root.integratedInTitleBar
                            ? (root.titleBarLeadingInset > 0
                               ? root.titleBarLeadingInset
                               : Theme.chromePadding)
                            : 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5
        z: 1

        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/folder-open.svg")
            tooltip: qsTrId("action.open")
            onClicked: root.openRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/save.svg")
            tooltip: qsTrId("action.save")
            enabled: root.saveEnabled
            onClicked: root.saveRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/undo.svg")
            tooltip: qsTrId("qml.undo")
            enabled: root.canUndo
            onClicked: root.undoRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/redo.svg")
            tooltip: qsTrId("action.redo")
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
            label: qsTrId("action.audio_settings")
            tooltip: qsTrId("action.audio_settings")
            onClicked: root.audioSettingsRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/preview-settings.svg")
            label: qsTrId("action.video_settings")
            tooltip: qsTrId("action.video_settings")
            onClicked: root.previewSettingsRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/panel-left.svg")
            tooltip: qsTrId("qml.toggle_sidebar")
            active: root.sidebarActive
            onClicked: root.toggleSidebarRequested()
        }
        ToolBarButton {
            iconSource: Qt.resolvedUrl("icons/panel-bottom.svg")
            tooltip: qsTrId("qml.toggle_bottom_panel")
            enabled: root.bottomPanelEnabled
            active: root.bottomActive
            onClicked: root.toggleBottomRequested()
        }
    }

}
