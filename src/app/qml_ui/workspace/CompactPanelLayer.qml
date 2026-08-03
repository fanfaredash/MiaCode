import QtQuick
import MiaCode.UI

Item {
    id: root

    required property var workbenchState
    required property var documentSession
    required property var preferences
    required property var previewSession
    required property var commands
    required property var shellController
    required property var pages
    property bool compact: false
    signal fullscreenRequested()
    signal settingsRequested()

    visible: compact && workbenchState.compactPanel.length > 0
    z: 40

    Rectangle {
        anchors.fill: parent
        color: "#66000000"

        MouseArea {
            anchors.fill: parent
            onClicked: root.workbenchState.compactPanel = ""
        }
    }

    WorkbenchSidebar {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(320, parent.width - 36)
        visible: root.workbenchState.compactPanel === "sidebar"
        workbenchState: root.workbenchState
        documentSession: root.documentSession
        preferences: root.preferences
        commands: root.commands
        pages: root.pages
        compact: true
        onSettingsRequested: root.settingsRequested()
    }

    PreviewPane {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(360, parent.width - 36)
        visible: root.workbenchState.compactPanel === "preview"
        previewSession: root.previewSession
        shellController: root.shellController
        onFullscreenRequested: root.fullscreenRequested()
    }
}
