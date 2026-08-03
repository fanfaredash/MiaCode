pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import MiaCode.UI
import "qrc:/quick_shell/qml" as Shell

Item {
    id: root

    required property var workbenchState
    required property var documentSession
    required property var preferences
    required property var previewSession
    required property var commands
    required property var shellController
    required property var pages
    property bool compact: false
    readonly property bool canUndo: editorPane.canUndo
    readonly property bool canRedo: editorPane.canRedo
    // User preference AND backend chart-bottom-tabs mode (export/metadata
    // call setChartBottomTabsMode(false); latency/difficulty turn it back on).
    readonly property bool bottomPanelEffectivelyVisible:
        root.workbenchState.bottomPanelVisible && root.shellController.bottomTabsVisible
    signal settingsRequested()

    function undo() {
        editorPane.undo()
    }

    function redo() {
        editorPane.redo()
    }

    function selectAll() {
        editorPane.selectAll()
    }

    function validateChart() {
        root.commands.validateDocument()
        root.workbenchState.bottomPanelVisible = true
        root.workbenchState.activeBottomTab = 1
    }

    function showFullscreenPreview() {
        if (root.shellController.exportPageActive)
            return
        fullscreenPreview.visible = true
    }

    function persistHorizontalLayout() {
        if (root.compact) return
        if (root.workbenchState.sidebarVisible)
            root.preferences.sidebarWidth = Math.round(sidebar.width - 48)
        if (root.workbenchState.previewVisible) {
            const available = Math.max(1, root.width
                - sidebar.width - 4
                - 4)
            root.preferences.previewWidthRatio = Math.min(0.5, preview.width / available)
        }
    }

    function persistVerticalLayout() {
        if (root.bottomPanelEffectivelyVisible)
            root.preferences.bottomPanelHeight = Math.round(bottomPanel.height)
    }

    function fittedFullscreenWidth(hostWidth, hostHeight) {
        const aspect = Math.max(1.0, root.shellController.previewCanvasAspectRatio || 1.0)
        const safeWidth = Math.max(1, hostWidth)
        const safeHeight = Math.max(1, hostHeight)
        return Math.max(1, Math.min(safeWidth, safeHeight * aspect))
    }

    function fittedFullscreenHeight(hostWidth, hostHeight) {
        const aspect = Math.max(1.0, root.shellController.previewCanvasAspectRatio || 1.0)
        const frameWidth = fittedFullscreenWidth(hostWidth, hostHeight)
        return Math.max(1, Math.min(hostHeight, frameWidth / aspect))
    }

    Connections {
        target: root.shellController
        function onShellStateChanged() {
            if (root.shellController.exportPageActive && fullscreenPreview.visible)
                fullscreenPreview.visible = false
        }
    }

    SplitView {
        id: horizontalSplit
        anchors.fill: parent
        orientation: Qt.Horizontal

        handle: SplitHandle {
            onReleased: root.persistHorizontalLayout()
        }

        WorkbenchSidebar {
            id: sidebar
            workbenchState: root.workbenchState
            documentSession: root.documentSession
            preferences: root.preferences
            commands: root.commands
            pages: root.pages
            compact: false
            visible: !root.compact
            SplitView.preferredWidth: root.workbenchState.sidebarVisible
                                      ? root.preferences.sidebarWidth + 48
                                      : 48
            SplitView.minimumWidth: root.workbenchState.sidebarVisible ? 168 : 48
            SplitView.maximumWidth: root.workbenchState.sidebarVisible ? 368 : 48
            onSettingsRequested: root.settingsRequested()
        }

        SplitView {
            id: centerSplit
            orientation: Qt.Vertical
            SplitView.fillWidth: true
            SplitView.minimumWidth: 280

            handle: SplitHandle {
                onReleased: root.persistVerticalLayout()
            }

            Item {
                id: editorHost
                SplitView.fillHeight: true
                SplitView.minimumHeight: 180

                EditorPane {
                    id: editorPane
                    anchors.fill: parent
                    visible: !root.pages.overlayActive
                    workbenchState: root.workbenchState
                    documentSession: root.documentSession
                    commands: root.commands
                }

                // v1 ExportLauncherPage / LatencyDetectionPage host. Sidebar stays
                // QML; the full widget page reuses MainWindow switchTo* lifecycle.
                WindowContainer {
                    id: nativePageHost
                    anchors.fill: parent
                    visible: root.pages.overlayActive && root.pages.pageWindow !== null
                    window: root.pages.pageWindow
                    function syncNativeSize() {
                        if (visible && width > 0 && height > 0)
                            root.pages.syncPageSize(width, height)
                    }
                    onVisibleChanged: syncNativeSize()
                    onWidthChanged: syncNativeSize()
                    onHeightChanged: syncNativeSize()
                    Component.onCompleted: syncNativeSize()
                }
            }

            BottomPanel {
                id: bottomPanel
                visible: root.bottomPanelEffectivelyVisible
                workbenchState: root.workbenchState
                documentSession: root.documentSession
                preferences: root.preferences
                commands: root.commands
                shellController: root.shellController
                SplitView.preferredHeight: root.bottomPanelEffectivelyVisible
                                           ? root.preferences.bottomPanelHeight
                                           : 0
                SplitView.minimumHeight: root.bottomPanelEffectivelyVisible ? 120 : 0
                SplitView.maximumHeight: root.bottomPanelEffectivelyVisible ? 340 : 0
                onSyntaxIssueActivated: (line, column, endColumn) =>
                    editorPane.revealSyntaxIssue(line, column, endColumn)
            }
        }

        PreviewPane {
            id: preview
            visible: !root.compact && root.workbenchState.previewVisible
            previewSession: root.previewSession
            shellController: root.shellController
            SplitView.preferredWidth: Math.max(
                220,
                (root.width - 48
                    - (root.workbenchState.sidebarVisible ? root.preferences.sidebarWidth + 4 : 0) - 4)
                * root.preferences.previewWidthRatio
            )
            SplitView.minimumWidth: 220
            SplitView.maximumWidth: Math.max(
                220,
                (root.width - sidebar.width - 4) * 0.5
            )
            onFullscreenRequested: root.showFullscreenPreview()
        }
    }

    Rectangle {
        id: fullscreenPreview
        anchors.fill: parent
        visible: false
        z: 80
        color: Theme.colors.background.editor

        Shell.QuickShellPreviewSurface {
            anchors.centerIn: parent
            width: root.fittedFullscreenWidth(parent.width * 0.94, parent.height * 0.94)
            height: root.fittedFullscreenHeight(parent.width * 0.94, parent.height * 0.94)
            runtime: root.previewSession.runtime
            mediaHost: root.previewSession.mediaHost
            logger: root.shellController
            surfaceRole: "fullscreen"
            dcompFallbackActive: true
        }

        IconButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            iconSource: Qt.resolvedUrl("icons/fullscreen.svg")
            tooltip: qsTr("退出全屏预览")
            onClicked: fullscreenPreview.visible = false
        }
    }
}
