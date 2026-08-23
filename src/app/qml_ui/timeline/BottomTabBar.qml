import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    required property var documentSession
    required property var shellController

    implicitHeight: 28
    color: Theme.colors.background.surface

    Row {
        anchors.fill: parent
        z: 1

        AppTab {
            height: parent.height
            panelTab: true
            visible: root.shellController.timelineTabVisible
            text: root.shellController.timelineTabLabel
            active: root.shellController.bottomTabsCurrentTabId === "timeline"
            onClicked: root.shellController.setBottomTabsCurrentTabId("timeline")
        }
        AppTab {
            height: parent.height
            panelTab: true
            visible: root.shellController.validationTabVisible
            text: root.shellController.validationTabLabel
            count: root.documentSession.syntaxIssueCount
            active: root.shellController.bottomTabsCurrentTabId === "validation"
            onClicked: root.shellController.setBottomTabsCurrentTabId("validation")
        }
    }
}

