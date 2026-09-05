import QtQuick
import MiaCode.UI

Item {
    id: root

    required property var viewState
    required property var documentSession
    required property var preferences
    required property var commands
    required property var pages
    property bool compact: false
    readonly property bool primarySidebarVisible: compact || viewState.sidebarVisible
    readonly property real activityBarWidth: activityBar.width

    signal settingsRequested()

    clip: true

    // Activity Bar 只负责功能域导航。再次选择当前功能时，桌面端切换
    // Primary Sidebar；紧凑布局直接关闭当前覆盖层。
    function activateView(viewId) {
        const pageAlreadyActive = viewId === "export"
            ? root.pages.activePageId === "export"
            : !root.pages.overlayActive
        if (viewState.activeSidebarView === viewId && pageAlreadyActive) {
            if (compact) {
                viewState.compactPanel = ""
                return
            }
            viewState.sidebarVisible = !viewState.sidebarVisible
            root.preferences.sidebarVisible = viewState.sidebarVisible
            return
        }

        if (viewId === "chart" && root.pages.overlayActive)
            root.pages.leaveOverlayPage()

        viewState.activeSidebarView = viewId
        if (!viewState.sidebarVisible) {
            viewState.sidebarVisible = true
            root.preferences.sidebarVisible = true
        }

        if (viewId === "export")
            root.pages.openVideoExportPage()
    }

    ActivityBar {
        id: activityBar
        color: root.compact ? "transparent" : Theme.surfaceColor(Theme.colors.background.activityBar)
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        activeView: root.viewState.activeSidebarView
        normalizationEnabled: root.pages.activePageId !== "export"
        onViewRequested: viewId => root.activateView(viewId)
        onToolRequested: function(toolId) {
            if (toolId === "latency")
                root.pages.openLatencyPage()
            else if (toolId === "media")
                root.pages.openMediaProcessingTools()
            else if (toolId === "normalize")
                root.pages.openNormalizeWholeChart()
        }
        onSettingsRequested: root.settingsRequested()
    }

    Item {
        id: pageHost
        anchors.left: activityBar.right
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        visible: root.primarySidebarVisible

        ChartFieldSidebar {
            color: root.compact ? "transparent" : Theme.surfaceColor(Theme.colors.background.panel)
            anchors.fill: parent
            visible: root.viewState.activeSidebarView === "chart"
            viewState: root.viewState
            documentSession: root.documentSession
            commands: root.commands
            pages: root.pages
        }

        ExportSidebarPage {
            color: root.compact ? "transparent" : Theme.surfaceColor(Theme.colors.background.panel)
            anchors.fill: parent
            visible: root.viewState.activeSidebarView === "export"
            pages: root.pages
        }
    }
}
