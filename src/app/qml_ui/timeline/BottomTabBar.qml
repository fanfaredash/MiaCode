import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    required property var viewState
    required property var documentSession

    implicitHeight: 28
    color: Theme.colors.background.surface

    Row {
        anchors.fill: parent
        z: 1

        AppTab {
            height: parent.height
            panelTab: true
            text: qsTr("时间轴")
            active: root.viewState.activeBottomTab === 0
            onClicked: root.viewState.activeBottomTab = 0
        }
        AppTab {
            height: parent.height
            panelTab: true
            text: qsTr("语法")
            count: root.documentSession.syntaxIssueCount
            active: root.viewState.activeBottomTab === 1
            onClicked: root.viewState.activeBottomTab = 1
        }
    }
}

