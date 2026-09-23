import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

Rectangle {
    id: root

    property string activeView: "chart"
    property bool documentAvailable: true
    property bool toolsAvailable: true
    property bool chartEditorAvailable: true
    property bool normalizationEnabled: true
    signal viewRequested(string viewId)
    signal toolRequested(string toolId)
    signal settingsRequested()

    implicitWidth: Theme.activityButtonSize
    color: Theme.surfaceColor(Theme.colors.background.activityBar)

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top

        ActivityButton {
            iconSource: Qt.resolvedUrl("icons/file.svg")
            tooltip: qsTrId("qml.chart")
            selected: root.activeView === "chart"
            onClicked: root.viewRequested("chart")
        }
        ActivityButton {
            iconSource: Qt.resolvedUrl("icons/export.svg")
            tooltip: qsTrId("sidebar.export")
            enabled: root.documentAvailable
            selected: root.activeView === "export"
            onClicked: root.viewRequested("export")
        }
        ActivityButton {
            id: toolsButton
            iconSource: Qt.resolvedUrl("icons/tools.svg")
            tooltip: qsTrId("qml.tools")
            enabled: root.toolsAvailable
            selected: toolsPopup.active
            onClicked: {
                if (toolsPopup.active)
                    toolsPopup.close()
                else
                    toolsPopup.popup(toolsButton, toolsButton.width, 0)
            }
        }
    }

    AppMenu {
        id: toolsPopup
        hugContent: true

        AppMenuAction {
            text: qsTrId("media_tools.audio_video_processing")
            enabled: root.toolsAvailable
            onTriggered: root.toolRequested("media")
        }
        AppMenuAction {
            text: qsTrId("qml.normalize_whole_chart")
            enabled: root.chartEditorAvailable && root.normalizationEnabled
            onTriggered: root.toolRequested("normalize")
        }
    }

    onToolsAvailableChanged: {
        if (!toolsAvailable)
            toolsPopup.close()
    }

    ActivityButton {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        iconSource: Qt.resolvedUrl("icons/settings.svg")
        tooltip: qsTrId("qml.view_settings")
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
            color: !button.enabled ? Theme.colors.text.disabled
                 : button.selected ? Theme.colors.activityIcon.active
                 : button.hovered ? Theme.colors.activityIcon.hover
                 : Theme.colors.activityIcon.idle
        }

        background: Item {
            HoverChrome {
                anchors.fill: parent
                contentWidth: Theme.activityIconSize
                contentHeight: Theme.activityIconSize
                stateColors: Theme.colors.activityState
                hovered: button.hovered
                pressed: button.down
                selected: button.selected
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
