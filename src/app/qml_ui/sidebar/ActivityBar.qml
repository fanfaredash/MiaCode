import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

Rectangle {
    id: root

    property string activeView: "chart"
    signal viewRequested(string viewId)
    signal settingsRequested()

    implicitWidth: Theme.activityButtonSize
    color: Theme.surfaceColor("panel", Theme.colors.background.editor)

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top

        ActivityButton {
            iconSource: Qt.resolvedUrl("icons/chart.svg")
            tooltip: UiText.text("谱面")
            selected: root.activeView === "chart"
            onClicked: root.viewRequested("chart")
        }
        ActivityButton {
            iconSource: Qt.resolvedUrl("icons/export.svg")
            tooltip: UiText.text("导出")
            selected: root.activeView === "export"
            onClicked: root.viewRequested("export")
        }
        ActivityButton {
            iconSource: Qt.resolvedUrl("icons/tools.svg")
            tooltip: UiText.text("工具")
            selected: root.activeView === "tools"
            onClicked: root.viewRequested("tools")
        }
    }

    ActivityButton {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        iconSource: Qt.resolvedUrl("icons/settings.svg")
        tooltip: UiText.text("视图设置")
        onClicked: root.settingsRequested()
    }

    component ActivityButton: AbstractButton {
        id: button

        required property url iconSource
        required property string tooltip
        property bool selected: false

        width: Theme.activityButtonSize
        height: Theme.activityButtonSize
        hoverEnabled: true

        contentItem: ControlsImpl.IconImage {
            anchors.centerIn: parent
            width: Theme.activityIconSize
            height: Theme.activityIconSize
            source: button.iconSource
            sourceSize: Qt.size(Theme.activityIconSize, Theme.activityIconSize)
            color: button.selected || button.hovered
                   ? Theme.colors.text.active
                   : Theme.colors.text.secondary
        }

        background: Item {
            HoverChrome {
                anchors.fill: parent
                margins: 6
                hovered: button.hovered
                pressed: button.down
                tone: "icon"
            }

            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 2
                height: Theme.activityIconSize
                radius: 1
                visible: button.selected
                color: Theme.colors.accent.primary
            }
        }

        Tooltip {
            visible: button.hovered
            text: button.tooltip
        }
    }
}
