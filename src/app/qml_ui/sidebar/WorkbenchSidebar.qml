import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    required property var workbenchState
    required property var documentSession
    required property var preferences
    required property var commands
    property bool compact: false
    readonly property bool primarySidebarVisible: compact || workbenchState.sidebarVisible

    signal settingsRequested()

    color: Theme.colors.background.workbench
    clip: true

    // Activity Bar 只负责功能域导航。再次选择当前功能时，桌面端切换
    // Primary Sidebar；紧凑布局直接关闭当前覆盖层。
    function activateView(viewId) {
        if (workbenchState.activeSidebarView === viewId) {
            if (compact) {
                workbenchState.compactPanel = ""
                return
            }
            workbenchState.sidebarVisible = !workbenchState.sidebarVisible
            root.preferences.sidebarVisible = workbenchState.sidebarVisible
            return
        }

        workbenchState.activeSidebarView = viewId
        if (!workbenchState.sidebarVisible) {
            workbenchState.sidebarVisible = true
            root.preferences.sidebarVisible = true
        }
    }

    ActivityBar {
        id: activityBar
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        activeView: root.workbenchState.activeSidebarView
        onViewRequested: viewId => root.activateView(viewId)
        onSettingsRequested: root.settingsRequested()
    }

    Rectangle {
        anchors.left: activityBar.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: Theme.colors.border.normal
    }

    Item {
        id: pageHost
        anchors.left: activityBar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        visible: root.primarySidebarVisible

        // 三个页面保持各自实例，功能切换后列表滚动位置和页面局部状态仍在。

        ChartFieldSidebar {
            anchors.fill: parent
            visible: root.workbenchState.activeSidebarView === "chart"
            workbenchState: root.workbenchState
            documentSession: root.documentSession
            commands: root.commands
        }

        SidebarPlaceholderPage {
            anchors.fill: parent
            visible: root.workbenchState.activeSidebarView === "export"
            title: qsTr("导出")
            description: qsTr("导出功能将在这里提供")
        }

        SidebarPlaceholderPage {
            anchors.fill: parent
            visible: root.workbenchState.activeSidebarView === "tools"
            title: qsTr("工具")
            description: qsTr("谱面工具将在这里提供")
        }
    }
}

