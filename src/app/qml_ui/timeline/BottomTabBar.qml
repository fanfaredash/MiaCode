import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Rectangle {
    id: root

    required property var documentSession
    required property var analysisSession
    required property var shellController

    implicitHeight: 28
    color: Theme.colors.background.surface

    RowLayout {
        anchors.fill: parent
        z: 1

        AppTab {
            Layout.fillHeight: true
            panelTab: true
            visible: root.shellController.timelineTabVisible
            text: root.shellController.timelineTabLabel
            active: root.shellController.bottomTabsCurrentTabId === "timeline"
            onClicked: root.shellController.setBottomTabsCurrentTabId("timeline")
        }
        AppTab {
            Layout.fillHeight: true
            panelTab: true
            visible: root.shellController.validationTabVisible
            text: root.shellController.validationTabLabel
            count: root.analysisSession.validationRows.length
            active: root.shellController.bottomTabsCurrentTabId === "validation"
            onClicked: root.shellController.setBottomTabsCurrentTabId("validation")
        }
        AppTab {
            Layout.fillHeight: true
            panelTab: true
            text: qsTr("Muri")
            count: root.analysisSession.muriRows.length
            active: root.shellController.bottomTabsCurrentTabId === "muri"
            onClicked: root.shellController.setBottomTabsCurrentTabId("muri")
        }

        Item { Layout.fillWidth: true }

        AppCheckBox {
            id: followCode

            Layout.alignment: Qt.AlignVCenter
            Layout.rightMargin: 8
            visible: root.shellController.bottomTabsCurrentTabId === "timeline"
            text: root.shellController.timelineFollowCodeLabel
            checked: root.shellController.timelineStateBridge
                ? root.shellController.timelineStateBridge.followPreviewEnabled
                : false
            Accessible.description: qsTr("跟随当前谱面代码位置")
            onClicked: root.shellController.timelineFollowPreviewToggled(checked)
        }
    }
}
