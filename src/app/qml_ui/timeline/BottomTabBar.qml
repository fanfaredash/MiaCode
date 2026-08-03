import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    required property var workbenchState
    required property var documentSession

    implicitHeight: 28
    color: Theme.colors.background.workbench

    Row {
        anchors.fill: parent
        z: 1

        WorkbenchTab {
            height: parent.height
            panelTab: true
            text: qsTr("时间轴")
            active: root.workbenchState.activeBottomTab === 0
            onClicked: root.workbenchState.activeBottomTab = 0
        }
        WorkbenchTab {
            height: parent.height
            panelTab: true
            text: qsTr("语法")
            count: root.documentSession.syntaxIssueCount
            active: root.workbenchState.activeBottomTab === 1
            onClicked: root.workbenchState.activeBottomTab = 1
        }
    }
}

