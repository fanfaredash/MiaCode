import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Item {
    id: root

    required property var documentSession
    required property var analysisSession
    required property var timelineSession

    implicitHeight: Theme.compactControlHeight + 2

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.panelPadding - Theme.chromeInsetX
        anchors.rightMargin: Theme.panelPadding
        spacing: 0
        z: 1

        AppTab {
            Layout.alignment: Qt.AlignVCenter
            panelTab: true
            compact: true
            visible: root.timelineSession.timelineTabVisible
            text: root.timelineSession.timelineTabLabel
            active: root.timelineSession.currentTabId === "timeline"
            onClicked: root.timelineSession.setCurrentTabId("timeline")
        }
        AppTab {
            Layout.alignment: Qt.AlignVCenter
            panelTab: true
            compact: true
            visible: root.timelineSession.validationTabVisible
            text: root.timelineSession.validationTabLabel
            Layout.leftMargin: 4
            count: root.analysisSession.validationRows.length
            active: root.timelineSession.currentTabId === "validation"
            onClicked: root.timelineSession.setCurrentTabId("validation")
        }
        AppTab {
            Layout.alignment: Qt.AlignVCenter
            panelTab: true
            compact: true
            visible: root.timelineSession.muriTabVisible
            text: root.timelineSession.muriTabLabel
            Layout.leftMargin: 4
            count: root.analysisSession.muriRows.length
            active: root.timelineSession.currentTabId === "muri"
            onClicked: root.timelineSession.setCurrentTabId("muri")
        }

        Item { Layout.fillWidth: true }

        AppCheckBox {
            compact: true
            Layout.alignment: Qt.AlignVCenter
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
