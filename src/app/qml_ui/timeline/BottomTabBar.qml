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

        CheckBox {
            id: followCode

            Layout.fillHeight: true
            Layout.rightMargin: 8
            visible: root.shellController.bottomTabsCurrentTabId === "timeline"
            text: root.width < 420 ? "" : root.shellController.timelineFollowCodeLabel
            checked: root.shellController.timelineStateBridge
                ? root.shellController.timelineStateBridge.followPreviewEnabled
                : false
            focusPolicy: Qt.TabFocus
            Accessible.name: root.shellController.timelineFollowCodeLabel
            Accessible.description: qsTr("跟随当前谱面代码位置")
            onClicked: root.shellController.timelineFollowPreviewToggled(checked)
        }

        IconButton {
            Layout.alignment: Qt.AlignVCenter
            Layout.rightMargin: 6
            visible: root.shellController.bottomTabsCurrentTabId === "timeline"
            iconSource: Qt.resolvedUrl("../resources/icons/more.svg")
            tooltip: qsTr("时间轴跟随设置")
            focusPolicy: Qt.TabFocus
            Accessible.name: qsTr("时间轴跟随设置")
            Accessible.description: qsTr("设置视图锁定和时间轴同步")
            onClicked: {
                const point = mapToGlobal(width, 0)
                root.shellController.openTimelineFollowSettingsMenu(
                    Math.round(point.x), Math.round(point.y))
        }
        }
    }
}
