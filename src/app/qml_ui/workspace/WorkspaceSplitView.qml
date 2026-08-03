pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
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
    property bool compact: false
    readonly property bool canUndo: editorPane.canUndo
    readonly property bool canRedo: editorPane.canRedo
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
        if (root.workbenchState.bottomPanelVisible)
            root.preferences.bottomPanelHeight = Math.round(bottomPanel.height)
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

            EditorPane {
                id: editorPane
                workbenchState: root.workbenchState
                documentSession: root.documentSession
                commands: root.commands
                SplitView.fillHeight: true
                SplitView.minimumHeight: 180
            }

            BottomPanel {
                id: bottomPanel
                visible: root.workbenchState.bottomPanelVisible
                workbenchState: root.workbenchState
                documentSession: root.documentSession
                preferences: root.preferences
                commands: root.commands
                shellController: root.shellController
                SplitView.preferredHeight: root.preferences.bottomPanelHeight
                SplitView.minimumHeight: 120
                SplitView.maximumHeight: 340
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
            width: Math.min(parent.width, parent.height) * 0.94
            height: width
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
