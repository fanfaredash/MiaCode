import QtQuick
import QtQuick.Dialogs
import MiaCode.UI

Item {
    id: root

    required property var hostWindow
    required property var applicationContext
    readonly property var documentSession: applicationContext.document
    readonly property var analysisSession: applicationContext.analysis
    readonly property var preferences: applicationContext.preferences
    readonly property var previewSession: applicationContext.preview
    readonly property var commands: applicationContext.commands
    readonly property var shellController: applicationContext.shell
    readonly property var pages: applicationContext.pages
    readonly property var editorController: applicationContext.editor
    readonly property var editorSync: applicationContext.editorSync
    readonly property var platform: applicationContext.platform
    readonly property var uiRequests: applicationContext.uiRequests
    readonly property var jobProgress: applicationContext.jobProgress
    readonly property var mediaTools: applicationContext.mediaTools
    readonly property var preferencesModel: applicationContext.preferencesModel
    readonly property var latency: applicationContext.latency
    readonly property var shortcutModel: applicationContext.shortcuts
    readonly property string documentTitle: documentSession.documentTitle
    readonly property bool compact: width < 720
    // 打开文件时的未保存决策。关闭应用走 v1 shell confirmClose 协议，
    // 不在这里再维护一套 closeApproved / pendingClose。
    property url pendingOpenFile
    property bool continueAfterSaveAs: false

    ViewState { id: state }

    MainMenuCommands {
        id: menuCommands
        canUndo: splitView.canUndo
        canRedo: splitView.canRedo
        onToggleSidebarRequested: root.toggleSidebar()
        onToggleBottomPanelRequested: {
            state.bottomPanelVisible = !state.bottomPanelVisible
            root.preferences.bottomPanelVisible = state.bottomPanelVisible
        }
        onTogglePreviewRequested: root.togglePreview()
        onExitRequested: root.requestClose()
        onUndoRequested: root.undo()
        onRedoRequested: root.redo()
        onSelectAllRequested: root.selectAll()
        onFindRequested: splitView.showFindReplace()
        onSelectCurrentLineRequested: splitView.selectCurrentLine()
        onValidateRequested: root.validateChart()
        onMetadataRequested: state.openMetadataEditor()
        onUnavailableFeatureRequested: featureName => root.showUnavailableFeature(featureName)
        onOpenRequested: openFileDialog.open()
        onSaveRequested: root.saveDocument()
        onSaveAsRequested: saveFileDialog.open()
    }

    function toggleSidebar() {
        if (root.compact) {
            if (!state.sidebarVisible) {
                state.sidebarVisible = true
                root.preferences.sidebarVisible = true
            }
            state.compactPanel = state.compactPanel === "sidebar" ? "" : "sidebar"
            return
        }
        state.sidebarVisible = !state.sidebarVisible
        root.preferences.sidebarVisible = state.sidebarVisible
    }

    function togglePreview() {
        if (root.compact) {
            if (!state.previewVisible) {
                state.previewVisible = true
                root.preferences.previewVisible = true
            }
            state.compactPanel = state.compactPanel === "preview" ? "" : "preview"
            return
        }
        state.previewVisible = !state.previewVisible
        root.preferences.previewVisible = state.previewVisible
    }

    function undo() {
        splitView.undo()
    }

    function redo() {
        splitView.redo()
    }

    function selectAll() {
        splitView.selectAll()
    }

    function applyChartTransform(opId) {
        return splitView.applyChartTransform(opId)
    }

    function validateChart() {
        splitView.validateChart()
        root.preferences.bottomPanelVisible = true
    }

    function showUnavailableFeature(featureName) {
        unavailableFeatureDialog.featureName = featureName
        unavailableFeatureDialog.open()
    }

    function saveDocument() {
        if (root.documentSession.currentFilePath.length === 0) {
            saveFileDialog.open()
            return
        }
        root.commands.saveDocument()
    }

    function requestOpenFile(fileUrl) {
        if (!root.documentSession.dirty) {
            root.commands.openDocument(fileUrl)
            return
        }
        root.pendingOpenFile = fileUrl
        unsavedChangesDialog.open()
    }

    function requestClose() {
        root.hostWindow.close()
    }

    function clearPendingAction() {
        root.pendingOpenFile = ""
        root.continueAfterSaveAs = false
    }

    function continuePendingAction() {
        const fileToOpen = root.pendingOpenFile
        root.clearPendingAction()
        if (fileToOpen.toString().length > 0)
            root.commands.openDocument(fileToOpen)
    }

    Component.onCompleted: {
        state.sidebarVisible = root.preferences.sidebarVisible
        state.bottomPanelVisible = root.preferences.bottomPanelVisible
        state.previewVisible = root.preferences.previewVisible
        state.resetEditorTabs(root.documentSession.currentDifficultyId)
    }

    onCompactChanged: state.compactPanel = ""

    Column {
        anchors.fill: parent
        spacing: 0

        WindowTitleBar {
            id: titleBar
            width: parent.width
            height: visible ? implicitHeight : 0
            visible: root.platform.customTitleBar
            hostWindow: root.hostWindow
            platform: root.platform
            menuCommands: menuCommands
            shortcuts: root.applicationContext.shortcuts
            leadingInset: root.applicationContext.windowChrome
                ? root.applicationContext.windowChrome.titleBarLeadingInset
                : 0
            documentTitle: root.documentSession.documentTitle
        }

        Loader {
            id: platformMenuLoader
            width: parent.width
            height: active ? 30 : 0
            active: !root.platform.customTitleBar
            sourceComponent: MainMenu {
                width: platformMenuLoader.width
                height: 30
                availableWidth: width
                commands: menuCommands
                shortcuts: root.applicationContext.shortcuts
                commandsEnabled: true
            }
        }

        MainToolBar {
            id: mainToolBar
            width: parent.width
            height: 36
            sidebarActive: root.compact
                           ? state.compactPanel === "sidebar"
                           : state.sidebarVisible
            bottomActive: state.bottomPanelVisible && root.shellController.bottomTabsVisible
            previewActive: root.compact
                           ? state.compactPanel === "preview"
                           : state.previewVisible
            canUndo: splitView.canUndo
            canRedo: splitView.canRedo
            onToggleSidebarRequested: root.toggleSidebar()
            onToggleBottomRequested: {
                state.bottomPanelVisible = !state.bottomPanelVisible
                root.preferences.bottomPanelVisible = state.bottomPanelVisible
            }
            onTogglePreviewRequested: root.togglePreview()
            onUndoRequested: root.undo()
            onRedoRequested: root.redo()
            onOpenRequested: openFileDialog.open()
            onSaveRequested: root.saveDocument()
            onUnavailableFeatureRequested: featureName => root.showUnavailableFeature(featureName)
        }

        Item {
            id: mainViewHost
            width: parent.width
            height: parent.height - titleBar.height - platformMenuLoader.height
                    - mainToolBar.height - statusBar.height

            MainSplitView {
                id: splitView
                anchors.fill: parent
                viewState: state
        documentSession: root.documentSession
        analysisSession: root.analysisSession
                preferences: root.preferences
                previewSession: root.previewSession
                commands: root.commands
                shellController: root.shellController
                pages: root.pages
                editorController: root.editorController
                editorSync: root.editorSync
                latency: root.latency
                compact: root.compact
                onSettingsRequested: root.commands.openPreferences()
            }

            CompactPanelLayer {
                anchors.fill: parent
                viewState: state
                documentSession: root.documentSession
                preferences: root.preferences
                previewSession: root.previewSession
                commands: root.commands
                shellController: root.shellController
                pages: root.pages
                compact: root.compact
                onSettingsRequested: root.commands.openPreferences()
                onFullscreenRequested: {
                    state.compactPanel = ""
                    splitView.showFullscreenPreview()
                }
            }
        }

        StatusBar {
            id: statusBar
            width: parent.width
            height: 23
            difficulty: state.difficultyEditorActive
                ? root.documentSession.currentDifficultyLabel : ""
            documentName: state.metadataEditorActive ? "metadata"
                : state.difficultyEditorActive ? root.documentSession.currentFilePath : ""
            cursorLine: state.editorCursorLine
            cursorColumn: state.editorCursorColumn
        }
    }

    Shortcut {
        sequence: StandardKey.Close
        onActivated: state.closeActiveEditor()
    }

    Shortcut {
        sequence: "Ctrl+F4"
        onActivated: state.closeActiveEditor()
    }

    FileDialog {
        id: openFileDialog
        title: qsTr("打开 simai 文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Simai 文件 (*.txt *.simai)"), qsTr("所有文件 (*.*)")]
        onAccepted: root.requestOpenFile(selectedFile)
    }

    FileDialog {
        id: saveFileDialog
        title: qsTr("保存 simai 文件")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTr("Simai 文件 (*.txt *.simai)"), qsTr("所有文件 (*.*)")]
        onAccepted: {
            const saved = root.commands.saveDocumentAs(selectedFile)
            if (saved && root.continueAfterSaveAs) {
                root.continuePendingAction()
            } else if (!saved && root.continueAfterSaveAs) {
                root.clearPendingAction()
            }
        }
        onRejected: {
            if (root.continueAfterSaveAs)
                root.clearPendingAction()
        }
    }

    MessageDialog {
        id: fileErrorDialog
        buttons: MessageDialog.Ok
    }

    // These actions remain discoverable while their dedicated QML pages and
    // business APIs are pending. Keeping the explanation in the visible root
    // window gives keyboard and pointer users the same immediate feedback.
    MessageDialog {
        id: unavailableFeatureDialog
        property string featureName: ""
        title: qsTr("暂未更新支持")
        text: qsTr("%1 尚未更新到 QML 界面。").arg(featureName)
        informativeText: qsTr("入口会保留，功能完成后将在此处提供。")
        buttons: MessageDialog.Ok
    }

    MessageDialog {
        id: unsavedChangesDialog
        title: qsTr("未保存的更改")
        text: qsTr("当前谱面有未保存的更改。")
        informativeText: qsTr("保存更改后继续，或丢弃本次编辑。")
        buttons: MessageDialog.Save | MessageDialog.Discard | MessageDialog.Cancel

        onButtonClicked: function(button, role) {
            if (button === MessageDialog.Save) {
                if (root.documentSession.currentFilePath.length === 0) {
                    root.continueAfterSaveAs = true
                    saveFileDialog.open()
                } else {
                    if (root.commands.saveDocument())
                        root.continuePendingAction()
                    else
                        root.clearPendingAction()
                }
                return
            }
            if (button === MessageDialog.Discard) {
                root.commands.discardDocumentChanges()
                root.continuePendingAction()
                return
            }
            root.clearPendingAction()
        }
    }

    Connections {
        target: root.documentSession

        function onDocumentReplaced() {
            state.resetEditorTabs(root.documentSession.currentDifficultyId)
            // The projection is queued, so this can run while the incoming
            // document's active difficulty is not set yet. Healing here means a
            // replacement never leaves the editor with no tab at all.
            state.syncDifficultyEditors(root.documentSession.difficulties,
                                        root.documentSession.currentDifficultyId)
        }

        function onDifficultiesChanged() {
            state.syncDifficultyEditors(root.documentSession.difficulties,
                                        root.documentSession.currentDifficultyId)
        }

        function onCurrentDifficultyChanged() {
            state.syncDifficultyEditors(root.documentSession.difficulties,
                                        root.documentSession.currentDifficultyId)
        }

        function onOperationFailed(title, message) {
            fileErrorDialog.title = title
            fileErrorDialog.text = message
            fileErrorDialog.open()
        }
    }

    Connections {
        target: state

        function onDifficultyEditorActivationRequested(difficultyId) {
            if (root.pages.overlayActive)
                root.pages.leaveOverlayPage()
            root.commands.selectDifficulty(difficultyId)
            state.activeSidebarView = "chart"
        }
    }

    // One host for the whole shell. Every Widgets-free flow — export, pack as
    // ZIP, the tool pages — routes its file picks and messages here, so a page
    // never owns dialog code and two pages can never open competing pickers.
    UiRequestHost {
        objectName: "shellUiRequestHost"
        requests: root.uiRequests
    }

    JobProgressOverlay {
        objectName: "shellJobProgress"
        anchors.fill: parent
        progress: root.jobProgress
    }

    // 音视频处理 lives at shell level: the tools menu, the latency page and the
    // tools sidebar all reach the same dialog.
    MediaToolsDialog {
        id: mediaToolsDialog
        objectName: "shellMediaToolsDialog"
        mediaTools: root.mediaTools
        onPrependRequested: function(isTrack) {
            const context = root.mediaTools.prependContext(isTrack)
            // An unavailable target has already explained itself as a notice.
            if (!context.available)
                return
            prependBlankDialog.loadContext(context)
            prependBlankDialog.open()
        }
    }

    PrependBlankDialog {
        id: prependBlankDialog
        objectName: "shellPrependBlankDialog"
        mediaTools: root.mediaTools
    }

    PreferencesDialog {
        id: preferencesDialog
        objectName: "shellPreferencesDialog"
        preferencesModel: root.preferencesModel
        shortcuts: root.shortcutModel
        preferences: root.preferences
    }

    Connections {
        target: root.pages
        function onMediaToolsRequested() { mediaToolsDialog.open() }
        function onPreferencesRequested() { preferencesDialog.open() }
    }
}
