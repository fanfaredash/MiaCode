pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import MiaCode.UI
import "qrc:/preview/runtime/qml" as Preview

Item {
    id: root

    required property var viewState
    required property var documentSession
    required property var analysisSession
    required property var preferences
    required property var previewSession
    required property var commands
    required property var shellController
    required property var pages
    required property var editorController
    required property var editorSync
    property bool compact: false
    readonly property bool canUndo: editorPane.canUndo
    readonly property bool canRedo: editorPane.canRedo
    // User preference AND backend chart-bottom-tabs mode (export/metadata
    // call setChartBottomTabsMode(false); latency/difficulty turn it back on).
    readonly property bool bottomPanelEffectivelyVisible:
        root.viewState.bottomPanelVisible && root.shellController.bottomTabsVisible
    readonly property bool exportVideoActive:
        root.pages.activePageId === "export"
    readonly property real previewEditorAvailableWidth:
        Math.max(1, workspaceSplit.width - (preview.visible ? 4 : 0))
    signal settingsRequested()

    function persistBottomPanelHeightRatio() {
        if (!root.bottomPanelEffectivelyVisible || centerSplit.height <= 0)
            return
        root.preferences.bottomPanelHeightRatio = bottomPanel.height / centerSplit.height
    }

    function undo() {
        editorPane.undo()
    }

    function redo() {
        editorPane.redo()
    }

    function selectAll() {
        editorPane.selectAll()
    }

    function showFindReplace() {
        editorPane.openFindReplace()
    }

    function selectCurrentLine() {
        editorPane.selectCurrentLine()
    }

    function validateChart() {
        root.commands.validateDocument()
        root.viewState.bottomPanelVisible = true
        root.shellController.setBottomTabsCurrentTabId("validation")
    }

    function showFullscreenPreview() {
        if (root.shellController.exportPageActive)
            return
        fullscreenPreview.visible = true
    }

    function persistSidebarWidth() {
        if (root.compact) return
        if (root.viewState.sidebarVisible)
            root.preferences.sidebarWidth = Math.round(sidebar.width - sidebar.activityBarWidth)
    }

    function persistPreviewWidthRatio() {
        if (root.compact) return
        if (root.viewState.previewVisible) {
            root.preferences.previewWidthRatio = preview.width / root.previewEditorAvailableWidth
        }
    }

    function syncWorkspacePanelOrder() {
        const targetPreviewIndex = root.shellController.workspacePanelsSwapped ? 0 : 1
        const currentPreviewIndex = workspaceSplit.itemAt(0) === preview ? 0 : 1
        if (currentPreviewIndex !== targetPreviewIndex)
            workspaceSplit.moveItem(currentPreviewIndex, targetPreviewIndex)
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
        function onWorkspacePanelsSwappedChanged() {
            root.syncWorkspacePanelOrder()
        }
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
            onReleased: root.persistSidebarWidth()
        }

        Sidebar {
            id: sidebar
            viewState: root.viewState
            documentSession: root.documentSession
            preferences: root.preferences
            commands: root.commands
            pages: root.pages
            compact: false
            visible: !root.compact
            SplitView.preferredWidth: root.viewState.sidebarVisible
                                      ? root.preferences.sidebarWidth + sidebar.activityBarWidth
                                      : sidebar.activityBarWidth
            SplitView.minimumWidth: root.viewState.sidebarVisible
                                    ? root.preferences.sidebarMinimumContentWidth + sidebar.activityBarWidth
                                    : sidebar.activityBarWidth
            SplitView.maximumWidth: root.viewState.sidebarVisible
                                    ? root.preferences.sidebarMaximumContentWidth + sidebar.activityBarWidth
                                    : sidebar.activityBarWidth
            onSettingsRequested: root.settingsRequested()
        }

        SplitView {
            id: workspaceSplit
            orientation: Qt.Horizontal
            SplitView.fillWidth: true
            Component.onCompleted: root.syncWorkspacePanelOrder()

            handle: SplitHandle {
                onReleased: root.persistPreviewWidthRatio()
            }

            SplitView {
                id: centerSplit
                orientation: Qt.Vertical
                SplitView.fillWidth: true
                SplitView.minimumWidth: root.previewEditorAvailableWidth
                                        * (1.0 - root.preferences.previewMaximumWidthRatio)

                handle: SplitHandle {
                    onReleased: root.persistBottomPanelHeightRatio()
                }

                Item {
                    id: editorHost
                    SplitView.fillHeight: true
                    SplitView.minimumHeight: 180

                    EditorPane {
                        id: editorPane
                        anchors.fill: parent
                        visible: !root.pages.overlayActive && !root.exportVideoActive
                        editorController: root.editorController
                        editorSync: root.editorSync
                        viewState: root.viewState
                        documentSession: root.documentSession
                        commands: root.commands
                        pages: root.pages
                    }

                    // v2 video export center: QML chrome + ExportVideoController panel surface.
                    ExportVideoPage {
                        anchors.fill: parent
                        visible: root.exportVideoActive
                        pages: root.pages
                    }

                    // LatencyDetectionPage (and any remaining full-page widget host).
                    WindowContainer {
                        id: nativePageHost
                        anchors.fill: parent
                        visible: root.pages.activePageId === "latency" && root.pages.pageWindow !== null
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
                    documentSession: root.documentSession
                    analysisSession: root.analysisSession
                    preferences: root.preferences
                    commands: root.commands
                    shellController: root.shellController
                    SplitView.preferredHeight: root.bottomPanelEffectivelyVisible
                                               ? centerSplit.height * root.preferences.bottomPanelHeightRatio
                                               : 0
                    SplitView.minimumHeight: root.bottomPanelEffectivelyVisible
                                             ? centerSplit.height * root.preferences.bottomPanelMinimumHeightRatio
                                             : 0
                    SplitView.maximumHeight: root.bottomPanelEffectivelyVisible
                                             ? centerSplit.height * root.preferences.bottomPanelMaximumHeightRatio
                                             : 0
                    onAnalysisRowActivated: (difficultyId, revision, line, column, endColumn, second) =>
                        editorPane.revealAnalysisRow(
                            difficultyId, revision, line, column, endColumn, second, root.analysisSession)
                }
            }

            PreviewPane {
                id: preview
                visible: !root.compact && root.viewState.previewVisible
                // Fullscreen owns the sole live scene root while its overlay is
                // active; leave this pane's transport chrome in place underneath.
                surfaceActive: !fullscreenPreview.visible
                previewSession: root.previewSession
                shellController: root.shellController
                SplitView.preferredWidth: root.previewEditorAvailableWidth
                                          * root.preferences.previewWidthRatio
                SplitView.minimumWidth: root.previewEditorAvailableWidth
                                        * root.preferences.previewMinimumWidthRatio
                SplitView.maximumWidth: root.previewEditorAvailableWidth
                                        * root.preferences.previewMaximumWidthRatio
                onFullscreenRequested: root.showFullscreenPreview()
            }
        }
    }

    Rectangle {
        id: fullscreenPreview
        anchors.fill: parent
        visible: false
        z: 80
        color: Theme.colors.background.editor

        Loader {
            anchors.centerIn: parent
            width: root.fittedFullscreenWidth(parent.width, parent.height)
            height: root.fittedFullscreenHeight(parent.width, parent.height)
            active: fullscreenPreview.visible && width >= 64 && height >= 64

            sourceComponent: Preview.PreviewSurface {
                anchors.fill: parent
                runtime: root.previewSession.runtime
                mediaHost: root.previewSession.mediaHost
                logger: root.shellController
                surfaceRole: "fullscreen"
            }
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
