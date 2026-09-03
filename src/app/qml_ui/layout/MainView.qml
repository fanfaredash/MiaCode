import QtQuick
import QtQuick.Dialogs
import MiaCode.UI

Item {
    id: root

    required property var hostWindow
    required property Item backgroundSource
    required property var applicationContext
    readonly property var documentSession: applicationContext.document
    readonly property var analysisSession: applicationContext.analysis
    readonly property var preferences: applicationContext.preferences
    readonly property var appBackground: applicationContext.appBackground
    readonly property var previewSession: applicationContext.preview
    readonly property var commands: applicationContext.commands
    readonly property var timelineSession: applicationContext.timeline
    readonly property var pages: applicationContext.pages
    readonly property var editorController: applicationContext.editor
    readonly property var editorSync: applicationContext.editorSync
    readonly property var platform: applicationContext.platform
    readonly property var uiRequests: applicationContext.uiRequests
    readonly property var jobProgress: applicationContext.jobProgress
    readonly property var mediaTools: applicationContext.mediaTools
    readonly property var preferencesModel: applicationContext.preferencesModel
    readonly property var latency: applicationContext.latency
    readonly property var coverExport: applicationContext.coverExport
    readonly property var shortcutModel: applicationContext.shortcuts
    readonly property string documentTitle: documentSession.documentTitle
    readonly property bool compact: width < 720

    ViewState { id: state }

    // 保存 means "save what I am working in", so the document model has to know
    // whether the view in front is one difficulty or the whole source.
    Binding {
        target: root.documentSession
        property: "wholeSourceEditorActive"
        value: state.metadataEditorActive
        restoreMode: Binding.RestoreNone
    }

    MainMenuCommands {
        id: menuCommands
        canUndo: splitView.canUndo
        canRedo: splitView.canRedo
        onToggleSidebarRequested: root.toggleSidebar()
        onToggleBottomPanelRequested: {
            state.bottomPanelVisible = !state.bottomPanelVisible
            root.preferences.bottomPanelVisible = state.bottomPanelVisible
        }
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
        onChartTransformRequested: opId => root.applyChartTransform(opId)
        onNormalizeChartRequested: root.pages.openNormalizeWholeChart()
        onAboutRequested: aboutDialog.open()
        onNewDocumentRequested: root.commands.newDocument()
        onOpenRecentRequested: path => root.commands.openRecentDocument(path)
        onRestoreBackupRequested: path => root.commands.restoreBackupDocument(path)
        onCloseDocumentRequested: root.commands.closeDocument()
        onAudioSettingsRequested: audioSettingsDialog.open()
        onPreviewSettingsRequested: previewSettingsDialog.open()
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

    // The unsaved-changes question is not asked here any more. It is asked by
    // commands.openDocument itself, along with every other action that would
    // discard the document — there used to be two three-way prompts written
    // twice, and only one of them covered anything but 打开.
    function requestOpenFile(fileUrl) {
        root.commands.openDocument(fileUrl)
    }

    function requestClose() {
        root.hostWindow.close()
    }

    Component.onCompleted: {
        state.sidebarVisible = root.preferences.sidebarVisible
        state.bottomPanelVisible = root.preferences.bottomPanelVisible
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
            documentSession: root.documentSession
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
                documentSession: root.documentSession
                commandsEnabled: true
            }
        }

        MainToolBar {
            id: mainToolBar
            width: parent.width
            height: implicitHeight
            sidebarActive: root.compact
                           ? state.compactPanel === "sidebar"
                           : state.sidebarVisible
            bottomActive: state.bottomPanelVisible && root.timelineSession.panelVisible
            canUndo: splitView.canUndo
            canRedo: splitView.canRedo
            onToggleSidebarRequested: root.toggleSidebar()
            onToggleBottomRequested: {
                state.bottomPanelVisible = !state.bottomPanelVisible
                root.preferences.bottomPanelVisible = state.bottomPanelVisible
            }
            onUndoRequested: root.undo()
            onRedoRequested: root.redo()
            onOpenRequested: openFileDialog.open()
            onSaveRequested: root.saveDocument()
            onAudioSettingsRequested: audioSettingsDialog.open()
            onPreviewSettingsRequested: previewSettingsDialog.open()
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
                backgroundSource: root.backgroundSource
                backgroundOffset: root.mapToItem(root.backgroundSource,
                                                 mainViewHost.x, mainViewHost.y)
                viewState: state
        documentSession: root.documentSession
        analysisSession: root.analysisSession
                preferences: root.preferences
                previewSession: root.previewSession
                commands: root.commands
                timelineSession: root.timelineSession
                preferencesModel: root.preferencesModel
                pages: root.pages
                editorController: root.editorController
                editorSync: root.editorSync
                latency: root.latency
                coverSession: root.coverExport
                compact: root.compact
                onSettingsRequested: root.commands.openPreferences()
            }

            CompactPanelLayer {
                anchors.fill: parent
                viewState: state
                documentSession: root.documentSession
                preferences: root.preferences
                commands: root.commands
                pages: root.pages
                compact: root.compact
                onSettingsRequested: root.commands.openPreferences()
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
        title: UiText.text("打开 simai 文件")
        fileMode: FileDialog.OpenFile
        nameFilters: [UiText.text("Simai 文件 (*.txt *.simai)"), UiText.text("所有文件 (*.*)")]
        onAccepted: root.requestOpenFile(selectedFile)
    }

    FileDialog {
        id: saveFileDialog
        title: UiText.text("保存 simai 文件")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [UiText.text("Simai 文件 (*.txt *.simai)"), UiText.text("所有文件 (*.*)")]
        onAccepted: root.commands.saveDocumentAs(selectedFile)
    }

    ChoiceDialog {
        id: fileErrorDialog
        objectName: "shellFileErrorDialog"
        choices: [{ id: "ok", label: UiText.text("确定"), role: "accept" }]
        dismissChoiceId: "ok"
    }

    // These actions remain discoverable while their dedicated QML pages and
    // business APIs are pending. Keeping the explanation in the visible root
    // window gives keyboard and pointer users the same immediate feedback.
    ChoiceDialog {
        id: unavailableFeatureDialog
        objectName: "shellUnavailableFeatureDialog"
        property string featureName: ""
        title: UiText.text("暂未更新支持")
        message: UiText.text("%1 尚未更新到 QML 界面。").arg(featureName)
        details: UiText.text("入口会保留，功能完成后将在此处提供。")
        choices: [{ id: "ok", label: UiText.text("确定"), role: "accept" }]
        dismissChoiceId: "ok"
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
            fileErrorDialog.message = message
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

    AudioSettingsDialog {
        id: audioSettingsDialog
        objectName: "shellAudioSettingsDialog"
        audioSettings: root.applicationContext.audioSettings
    }

    PreviewSettingsDialog {
        id: previewSettingsDialog
        objectName: "shellPreviewSettingsDialog"
        previewSettings: root.applicationContext.previewSettings
    }

    AboutDialog {
        id: aboutDialog
        objectName: "shellAboutDialog"
        preferences: root.preferences
    }

    PreferencesDialog {
        id: preferencesDialog
        objectName: "shellPreferencesDialog"
        preferencesModel: root.preferencesModel
        shortcuts: root.shortcutModel
        preferences: root.preferences
        appBackground: root.appBackground
    }

    Connections {
        target: root.pages
        function onMediaToolsRequested() { mediaToolsDialog.open() }
        function onPreferencesRequested() { preferencesDialog.open() }
    }
}
