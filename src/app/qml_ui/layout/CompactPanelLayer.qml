import QtQuick
import MiaCode.UI

Item {
    id: root

    required property var viewState
    required property var documentSession
    required property var preferences
    required property var commands
    required property var pages
    property bool compact: false
    signal settingsRequested()

    visible: compact && viewState.compactPanel.length > 0
    z: 40

    Rectangle {
        anchors.fill: parent
        color: "#66000000"

        MouseArea {
            anchors.fill: parent
            onClicked: root.viewState.compactPanel = ""
        }
    }

    Sidebar {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: Math.min(320, parent.width - 36)
        visible: root.viewState.compactPanel === "sidebar"
        viewState: root.viewState
        documentSession: root.documentSession
        preferences: root.preferences
        commands: root.commands
        pages: root.pages
        compact: true
        onSettingsRequested: root.settingsRequested()
    }

}
