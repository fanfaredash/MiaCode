import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Rectangle {
    id: root

    required property var documentSession
    required property var analysisSession
    required property var timelineSession

    implicitHeight: 28
    color: Theme.colors.background.surface

    RowLayout {
        anchors.fill: parent
        z: 1

        AppTab {
            Layout.fillHeight: true
            panelTab: true
            visible: root.timelineSession.timelineTabVisible
            text: root.timelineSession.timelineTabLabel
            active: root.timelineSession.currentTabId === "timeline"
            onClicked: root.timelineSession.setCurrentTabId("timeline")
        }
        AppTab {
            Layout.fillHeight: true
            panelTab: true
            visible: root.timelineSession.validationTabVisible
            text: root.timelineSession.validationTabLabel
            count: root.analysisSession.validationRows.length
            active: root.timelineSession.currentTabId === "validation"
            onClicked: root.timelineSession.setCurrentTabId("validation")
        }
        AppTab {
            Layout.fillHeight: true
            panelTab: true
            text: UiText.text("Muri")
            count: root.analysisSession.muriRows.length
            active: root.timelineSession.currentTabId === "muri"
            onClicked: root.timelineSession.setCurrentTabId("muri")
        }

        Item { Layout.fillWidth: true }

        AppCheckBox {
            id: followCode

            Layout.alignment: Qt.AlignVCenter
            Layout.rightMargin: 8
            visible: root.timelineSession.currentTabId === "timeline"
            text: root.timelineSession.followCodeLabel
            checked: root.timelineSession.stateBridge
                ? root.timelineSession.stateBridge.followPreviewEnabled
                : false
            Accessible.description: UiText.text("跟随当前谱面代码位置")
            onClicked: root.timelineSession.followPreviewToggled(checked)
        }
    }
}
