pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Window
import MiaCode.UI
import "qrc:/preview/runtime/qml" as Preview

Item {
    id: root

    required property Item backgroundSource
    required property point backgroundOffset
    required property var viewState
    required property var documentSession
    required property var analysisSession
    required property var preferences
    required property var previewSession
    required property var commands
    required property var timelineSession
    required property var preferencesModel
    required property var pages
    required property var editorController
    required property var editorSync
    required property var latency
    required property var coverSession
    property bool compact: false
    property real sidebarDragWidth: 0
    property bool sidebarResizing: false
    readonly property bool canUndo: editorPane.canUndo
    readonly property bool canRedo: editorPane.canRedo
    // User preference AND backend chart-bottom-tabs mode (export/metadata
    // call setChartBottomTabsMode(false); latency/difficulty turn it back on).
    readonly property bool bottomPanelEffectivelyVisible:
        root.viewState.bottomPanelVisible && root.timelineSession.panelVisible
    readonly property bool exportVideoActive:
        root.pages.activePageId === "export"
    readonly property bool coverExportActive:
        root.pages.activePageId === "cover"
    readonly property real previewEditorAvailableWidth:
        Math.max(1, workspaceSplit.width - (preview.visible ? Theme.splitDividerThickness : 0))
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

    function applyChartTransform(opId) {
        return editorPane.applyChartTransform(opId)
    }

    function validateChart() {
        root.commands.validateDocument()
        root.viewState.bottomPanelVisible = true
        root.timelineSession.setCurrentTabId("validation")
    }

    function showFullscreenPreview() {
        // Stop-gap for the export-page + fullscreen Intel iGPU D3D11 crash.
        if (root.coverExportActive)
            return
        if (root.exportVideoActive)
            return
        fullscreenPreview.visible = true
    }

    function persistSidebarWidth() {
        if (root.compact) return
        if (root.viewState.sidebarVisible)
            root.preferences.sidebarWidth = Math.round(root.sidebarDragWidth)
        root.preferences.sidebarVisible = root.viewState.sidebarVisible
    }

    function resizeSidebar(contentWidth) {
        root.sidebarDragWidth = Math.max(root.preferences.sidebarMinimumContentWidth,
            Math.min(root.preferences.sidebarMaximumContentWidth, contentWidth))
        root.viewState.sidebarVisible = contentWidth >= root.preferences.sidebarMinimumContentWidth / 2
    }

    function persistPreviewWidthRatio() {
        if (root.compact) return
        root.preferences.previewWidthRatio = preview.width / root.previewEditorAvailableWidth
    }

    function syncWorkspacePanelOrder() {
        const targetPreviewIndex = root.preferencesModel.previewOnLeft ? 0 : 1
        const currentPreviewIndex = workspaceSplit.itemAt(0) === preview ? 0 : 1
        if (currentPreviewIndex !== targetPreviewIndex)
            workspaceSplit.moveItem(currentPreviewIndex, targetPreviewIndex)
    }

    readonly property bool freeAspectActive:
        root.preferences && root.preferences.previewCanvasFreeAspect

    function fittedFullscreenWidth(hostWidth, hostHeight) {
        const aspect = Math.max(1.0, root.previewSession.canvasAspectRatio || 1.0)
        const safeWidth = Math.max(1, hostWidth)
        const safeHeight = Math.max(1, hostHeight)
        return Math.max(1, Math.min(safeWidth, safeHeight * aspect))
    }

    function fittedFullscreenHeight(hostWidth, hostHeight) {
        const aspect = Math.max(1.0, root.previewSession.canvasAspectRatio || 1.0)
        const frameWidth = fittedFullscreenWidth(hostWidth, hostHeight)
        return Math.max(1, Math.min(hostHeight, frameWidth / aspect))
    }

    Connections {
        target: root.preferencesModel
        function onInterfaceChanged() {
            root.syncWorkspacePanelOrder()
        }
    }

    // Leaving fullscreen on the export page is the same stop-gap as above; the
    // page identity comes from the QML router rather than from the backend.
    onExportVideoActiveChanged: {
        if (root.exportVideoActive && fullscreenPreview.visible)
            fullscreenPreview.visible = false
    }
    onCoverExportActiveChanged: {
        if (root.coverExportActive && fullscreenPreview.visible)
            fullscreenPreview.visible = false
    }

    Item {
        id: horizontalSplit
        anchors.fill: parent
        readonly property int orientation: Qt.Horizontal

        Sidebar {
            id: sidebar
            viewState: root.viewState
            documentSession: root.documentSession
            preferences: root.preferences
            commands: root.commands
            pages: root.pages
            compact: false
            visible: !root.compact
            width: visible ? sidebar.activityBarWidth + (root.viewState.sidebarVisible
                ? (root.sidebarResizing ? root.sidebarDragWidth : root.preferences.sidebarWidth) : 0) : 0
            height: parent.height
            onSettingsRequested: root.settingsRequested()
        }

        SplitView {
            id: workspaceSplit
            orientation: Qt.Horizontal
            x: sidebar.width + sidebarHandle.width
            width: parent.width - x
            height: parent.height
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
                        previewSession: root.previewSession
                    }

                    CoverExportPage {
                        anchors.fill: parent
                        visible: root.coverExportActive
                        pages: root.pages
                        coverSession: root.coverSession
                    }

                    LatencyPage {
                        id: latencyPage
                        anchors.fill: parent
                        visible: root.pages.activePageId === "latency"
                        latency: root.latency
                        pages: root.pages
                    }
                }

                BottomPanel {
                    id: bottomPanel
                    visible: root.bottomPanelEffectivelyVisible
                    documentSession: root.documentSession
                    analysisSession: root.analysisSession
                    preferences: root.preferences
                    commands: root.commands
                    timelineSession: root.timelineSession
                    previewSession: root.previewSession
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
                // Cover export owns the center workspace and must release both
                // the preview pane and its live surface while that page is open.
                visible: !root.coverExportActive
                surfaceActive: !root.coverExportActive && !fullscreenPreview.visible
                previewSession: root.previewSession
                preferences: root.preferences
                exportPageActive: root.exportVideoActive
                SplitView.preferredWidth: root.previewEditorAvailableWidth
                                          * root.preferences.previewWidthRatio
                SplitView.minimumWidth: root.previewEditorAvailableWidth
                                        * root.preferences.previewMinimumWidthRatio
                SplitView.maximumWidth: root.previewEditorAvailableWidth
                                        * root.preferences.previewMaximumWidthRatio
                onFullscreenRequested: root.showFullscreenPreview()
            }
        }

        CornerMask {
            x: root.compact ? 0 : sidebar.activityBarWidth
            backgroundSource: root.backgroundSource
            backgroundOffset: Qt.point(root.backgroundOffset.x + x, root.backgroundOffset.y)
        }

        SplitHandle {
            id: sidebarHandle
            x: sidebar.width
            width: visible && root.viewState.sidebarVisible ? Theme.splitDividerThickness : 0
            height: parent.height
            visible: !root.compact
            showDivider: root.viewState.sidebarVisible
            handlePressed: sidebarDrag.pressed
            handleHovered: sidebarDrag.containsMouse

            MouseArea {
                id: sidebarDrag
                anchors.centerIn: parent
                width: Theme.splitHandleHitExtent
                height: parent.height
                hoverEnabled: true
                cursorShape: Qt.SplitHCursor
                preventStealing: true
                property real startX: 0
                property real startWidth: 0

                onPressed: mouse => {
                    startX = mapToItem(horizontalSplit, mouse.x, mouse.y).x
                    startWidth = root.viewState.sidebarVisible ? root.preferences.sidebarWidth : 0
                    root.sidebarDragWidth = root.preferences.sidebarWidth
                    root.sidebarResizing = true
                }
                onPositionChanged: mouse => {
                    if (pressed)
                        root.resizeSidebar(startWidth + mapToItem(horizontalSplit, mouse.x, mouse.y).x - startX)
                }
                onReleased: {
                    root.persistSidebarWidth()
                    root.sidebarResizing = false
                }
                onCanceled: {
                    root.persistSidebarWidth()
                    root.sidebarResizing = false
                }
            }
        }
    }

    Rectangle {
        id: fullscreenPreview
        anchors.fill: parent
        visible: false
        z: 80
        color: Theme.colors.background.surface

        Loader {
            anchors.centerIn: parent
            width: root.freeAspectActive
                   ? parent.width
                   : root.fittedFullscreenWidth(parent.width, parent.height)
            height: root.freeAspectActive
                    ? parent.height
                    : root.fittedFullscreenHeight(parent.width, parent.height)
            active: fullscreenPreview.visible && width >= 64 && height >= 64

            sourceComponent: Preview.PreviewSurface {
                anchors.fill: parent
                runtime: root.previewSession.runtime
                mediaHost: root.previewSession.mediaHost
                logger: root.previewSession
                surfaceRole: "fullscreen"
            }
        }

        IconButton {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            iconSource: Qt.resolvedUrl("icons/fullscreen.svg")
            tooltip: UiText.text("退出全屏预览")
            onClicked: fullscreenPreview.visible = false
        }
    }
}
