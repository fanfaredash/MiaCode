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
    readonly property var imageResources: applicationContext.imageResources
    readonly property var mediaTools: applicationContext.mediaTools
    readonly property var preferencesModel: applicationContext.preferencesModel
    readonly property var latency: applicationContext.latency
    readonly property var shortcutModel: applicationContext.shortcuts
    readonly property var updates: applicationContext.update
    readonly property string documentTitle: {
        if (!documentSession.hasDocument)
            return ""
        const metadataTitle = documentSession.metadataTitle.trim()
        const baseTitle = metadataTitle.length > 0
            ? metadataTitle : documentSession.currentFileName
        return state.difficultyEditorActive
            ? baseTitle + " — " + documentSession.currentDifficultyLabel
            : baseTitle
    }
    readonly property bool editorActive: state.hasActiveEditor && !pages.overlayActive
    readonly property bool chartEditorActive: documentSession.hasDocument
        && documentSession.currentDifficultyId > 0 && !pages.overlayActive
    readonly property real minimumWidth: splitView.minimumWorkspaceWidth
    readonly property real minimumHeight: titleBar.height + platformMenuLoader.height
        + mainToolBar.height + statusBar.height + splitView.minimumHeight
    readonly property bool compact: width < minimumWidth + splitView.expandedSidebarWidth

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
        canCut: splitView.canCut
        canCopy: splitView.canCopy
        canPaste: splitView.canPaste
        onExitRequested: root.requestClose()
        onUndoRequested: root.undo()
        onRedoRequested: root.redo()
        onCutRequested: splitView.cut()
        onCopyRequested: splitView.copy()
        onPasteRequested: splitView.paste()
        onSelectAllRequested: root.selectAll()
        onFindRequested: splitView.showFindReplace()
        onSelectCurrentLineRequested: splitView.selectCurrentLine()
        onMetadataRequested: {
            if (root.documentSession.hasDocument)
                state.openMetadataEditor()
        }
        onLatencyCalibrationRequested: {
            if (!root.documentSession.hasDocument)
                return
            root.pages.rememberEditorReturnTarget(state.activeEditorKey)
            root.pages.openLatencyPage()
        }
        onMediaToolsRequested: {
            if (root.documentSession.hasDocument)
                root.pages.openMediaProcessingTools()
        }
        onUnavailableFeatureRequested: featureName => root.showUnavailableFeature(featureName)
        onOpenRequested: openFileDialog.open()
        onSaveRequested: root.saveDocument()
        onSaveWholeDocumentRequested: root.saveWholeDocument()
        onSaveAsRequested: {
            if (root.documentSession.hasDocument)
                saveFileDialog.open()
        }
        onChartTransformRequested: opId => {
            if (root.chartEditorActive)
                root.applyChartTransform(opId)
        }
        onNormalizeChartRequested: {
            if (root.chartEditorActive)
                root.pages.openNormalizeWholeChart()
        }
        onAboutRequested: aboutDialog.open()
        onPreferencesRequested: preferencesDialog.open()
        onNewDocumentRequested: root.commands.newDocument()
        onOpenRecentRequested: path => root.commands.openRecentDocument(path)
        onRestoreBackupRequested: path => root.commands.restoreBackupDocument(path)
        onCloseDocumentRequested: {
            if (root.documentSession.hasDocument)
                root.commands.closeDocument()
        }
        onAudioSettingsRequested: audioSettingsDialog.open()
        onPreviewSettingsRequested: previewSettingsDialog.open()
        onPreviewRateStepRequested: direction => {
            if (root.chartEditorActive)
                root.previewSession.adjustRate(direction)
        }
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

    // PageHost is the authority for overlay navigation. Keep the activity bar
    // projection in step with it even when a page is opened by a runtime signal
    // (for example, exporting a selection from the source editor).
    function syncSidebarViewToPage() {
        if (root.pages.activePageId === "export"
                || root.pages.activePageId === "cover")
            state.activeSidebarView = "export"
        else if (root.pages.activePageId === "latency")
            state.activeSidebarView = "tools"
        else if (root.pages.activePageId === "")
            state.activeSidebarView = "chart"
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
        if (!root.chartEditorActive)
            return false
        return splitView.applyChartTransform(opId)
    }

    function showUnavailableFeature(featureName) {
        unavailableFeatureDialog.featureName = featureName
        unavailableFeatureDialog.open()
    }

    function saveDocument() {
        if (!root.editorActive)
            return
        if (root.documentSession.currentFilePath.length === 0) {
            saveFileDialog.open()
            return
        }
        root.commands.saveDocument()
    }

    function saveWholeDocument() {
        if (!root.documentSession.hasDocument)
            return
        if (root.documentSession.currentFilePath.length === 0) {
            saveFileDialog.open()
            return
        }
        root.commands.saveWholeDocument()
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
            pet: root.applicationContext.pet
            saveEnabled: root.editorActive
            wholeDocumentSaveEnabled: root.documentSession.hasDocument
            documentAvailable: root.documentSession.hasDocument
            editorCommandsEnabled: root.editorActive
            chartCommandsEnabled: root.chartEditorActive
            toolCommandsEnabled: root.documentSession.hasDocument
            leadingInset: root.applicationContext.windowChrome
                ? root.applicationContext.windowChrome.titleBarLeadingInset
                : 0
            documentTitle: root.documentTitle
            normalizationEnabled: root.pages.activePageId !== "export"
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
                pet: root.applicationContext.pet
                commandsEnabled: true
                saveEnabled: root.editorActive
                wholeDocumentSaveEnabled: root.documentSession.hasDocument
                documentAvailable: root.documentSession.hasDocument
                editorCommandsEnabled: root.editorActive
                chartCommandsEnabled: root.chartEditorActive
                toolCommandsEnabled: root.documentSession.hasDocument
                normalizationEnabled: root.pages.activePageId !== "export"
            }
        }

        MainToolBar {
            id: mainToolBar
            width: parent.width
            height: implicitHeight
            hostWindow: root.hostWindow
            sidebarActive: root.compact
                           ? state.compactPanel === "sidebar"
                           : state.sidebarVisible
            bottomActive: state.difficultyEditorActive
                          && state.bottomPanelVisible && root.timelineSession.panelVisible
            bottomPanelEnabled: state.difficultyEditorActive
            saveEnabled: root.editorActive
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
                previewSettings: root.applicationContext.previewSettings
                commands: root.commands
                timelineSession: root.timelineSession
                preferencesModel: root.preferencesModel
                pages: root.pages
                editorController: root.editorController
                editorSync: root.editorSync
                latency: root.latency
                compact: root.compact
                onOpenRequested: openFileDialog.open()
                onSettingsRequested: preferencesDialog.open()
            }

            CompactPanelLayer {
                anchors.fill: parent
                viewState: state
                documentSession: root.documentSession
                preferences: root.preferences
                commands: root.commands
                pages: root.pages
                compact: root.compact
                onSettingsRequested: preferencesDialog.open()
            }
        }

        StatusBar {
            id: statusBar
            width: parent.width
            height: 23
            documentName: root.documentSession.hasDocument
                ? root.documentSession.currentFilePath : ""
            cursorLine: state.editorCursorLine
            cursorColumn: state.editorCursorColumn
            selectionBeatText: splitView.selectionBeatStatusText
            selectionBeatTooltip: splitView.selectionBeatTooltipText
            metadataActive: state.metadataEditorActive && !root.pages.overlayActive
            difficultyActive: state.difficultyEditorActive && !root.pages.overlayActive
            updateAvailable: root.updates ? root.updates.updateAvailable : false
            updateVersion: root.updates ? root.updates.availableVersion : ""
            onUpdateActivated: updatePrompt.present()
        }
    }

    Shortcut {
        sequences: [StandardKey.Close]
        enabled: root.editorActive
        onActivated: splitView.requestCloseActiveEditor()
    }

    FileDialog {
        id: openFileDialog
        title: qsTrId("qml.open_simai_file")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTrId("qml.simai_files_txt_simai"), qsTrId("qml.all_files_2")]
        onAccepted: root.requestOpenFile(selectedFile)
    }

    FileDialog {
        id: saveFileDialog
        title: qsTrId("qml.save_simai_file")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "txt"
        nameFilters: [qsTrId("qml.simai_files_txt_simai"), qsTrId("qml.all_files_2")]
        onAccepted: root.commands.saveDocumentAs(selectedFile)
    }

    ChoiceDialog {
        id: fileErrorDialog
        objectName: "shellFileErrorDialog"
        choices: [{ id: "ok", label: qsTrId("action.ok"), role: "accept" }]
        dismissChoiceId: "ok"
    }

    // These actions remain discoverable while their dedicated QML pages and
    // business APIs are pending. Keeping the explanation in the visible root
    // window gives keyboard and pointer users the same immediate feedback.
    ChoiceDialog {
        id: unavailableFeatureDialog
        objectName: "shellUnavailableFeatureDialog"
        property string featureName: ""
        title: qsTrId("qml.not_yet_available_in_qml")
        message: qsTrId("qml.1_has_not_yet_been_migrated_to_the_qml_interface").arg(featureName)
        details: qsTrId("qml.this_entry_will_remain_here_and_become_available_when_the_featur")
        choices: [{ id: "ok", label: qsTrId("action.ok"), role: "accept" }]
        dismissChoiceId: "ok"
    }

    // The only passive signal for an available update is the status bar badge;
    // this dialog opens only when the reader asks for it (badge click or the
    // About dialog's hint), never on its own.
    ChoiceDialog {
        id: updatePrompt
        objectName: "shellUpdateAvailableDialog"

        function present() {
            if (!root.updates || !root.updates.updateAvailable)
                return
            const detail = root.updates.availableDetail()
            title = qsTrId("dialog.update.title")
            message = qsTrId("dialog.update.message").arg(detail.version)
            let lines = []
            if (detail.releasedAt)
                lines.push(qsTrId("dialog.update.released").arg(detail.releasedAt))
            if (detail.sizeText)
                lines.push(qsTrId("dialog.update.size").arg(detail.sizeText))
            if (detail.notes)
                lines.push(detail.notes)
            details = lines.join("\n")
            // On launch the service can restore a remembered version before the
            // network check completes, so releasePageUrl can still be empty here.
            // Disable rather than hide the button so the dialog's shape stays
            // stable and nothing silently no-ops.
            choices = [
                { id: "download", label: qsTrId("dialog.update.download"), role: "accept",
                  enabled: !!detail.releasePageUrl },
                { id: "later", label: qsTrId("action.later"), role: "reject" },
                { id: "skip", label: qsTrId("dialog.update.skip"), role: "reject" }
            ]
            dismissChoiceId = "later"
            open()
        }

        onChosen: choiceId => {
            if (choiceId === "download")
                root.updates.openDownloadPage()
            else if (choiceId === "skip")
                root.updates.skipAvailableVersion()
        }
    }

    Connections {
        target: root.documentSession

        function onDocumentReplaced() {
            if (!root.documentSession.hasDocument)
                state.activeSidebarView = "chart"
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
            if (root.documentSession.currentDifficultyId <= 0)
                normalizeDialog.close()
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
            if (root.pages.overlayActive) {
                // Overlay exit may need an asynchronous unsaved-field answer.
                // Keep the requested difficulty until the page host confirms
                // the exit; selecting it here would bypass a cancellation.
                pendingDifficultyActivation = difficultyId
                if (!root.pages.leaveOverlayPage())
                    pendingDifficultyActivation = 0
                return
            }
            root.commands.selectDifficulty(difficultyId)
            root.pages.ensureDifficultyPageActive(difficultyId)
            state.activeSidebarView = "chart"
        }

        function onEditorPresentationCleared() {
            root.pages.clearEditorPresentation()
        }
    }

    property int pendingDifficultyActivation: 0

    Connections {
        target: root.pages

        function onActivePageIdChanged() {
            root.syncSidebarViewToPage()
        }

        function onOverlayPageLeft() {
            if (root.pendingDifficultyActivation <= 0)
                return
            const difficultyId = root.pendingDifficultyActivation
            root.pendingDifficultyActivation = 0
            root.commands.selectDifficulty(difficultyId)
            root.pages.ensureDifficultyPageActive(difficultyId)
            state.activeSidebarView = "chart"
        }

        function onNavigationRejected() {
            root.pendingDifficultyActivation = 0
            root.syncSidebarViewToPage()
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
        imageResources: root.imageResources
    }

    // Window-level tool overlays keep the current center page mounted.
    MediaToolsDialog {
        id: mediaToolsDialog
        objectName: "shellMediaToolsDialog"
        mediaTools: root.mediaTools
        documentAvailable: root.documentSession.hasDocument
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

    NormalizeOptionsDialog {
        id: normalizeDialog
        objectName: "shellNormalizeOptionsDialog"
        documentSession: root.documentSession

        onAccepted: {
            const options = {
                reduceTo384Grid: normalizeDialog.reduceTo384Grid,
                sectionMeasureCount: normalizeDialog.sectionMeasureCount,
                syntax: normalizeDialog.syntax
            }
            root.documentSession.setNormalizeOptions(options)
            splitView.applyNormalization(options)
        }
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
        updateService: root.updates
        onUpdateRequested: updatePrompt.present()
    }

    PreferencesDialog {
        id: preferencesDialog
        objectName: "shellPreferencesDialog"
        preferencesModel: root.preferencesModel
        shortcuts: root.shortcutModel
        preferences: root.preferences
        appBackground: root.appBackground
        updateService: root.updates
        onUpdateRequested: updatePrompt.present()
    }

    Connections {
        target: root.pages
        function onMediaToolsRequested() { mediaToolsDialog.open() }
        function onNormalizeWholeChartRequested() {
            if (root.pages.activePageId === "export" || !splitView.canNormalizeChart())
                return
            const stored = root.documentSession.normalizeOptions()
            normalizeDialog.reduceTo384Grid = stored.reduceTo384Grid
            normalizeDialog.sectionMeasureCount = stored.sectionMeasureCount
            normalizeDialog.syntax = stored.syntax
            normalizeDialog.selectionDescription = splitView.normalizationSelectionDescription()
            normalizeDialog.open()
        }
        function onPreferencesRequested() { preferencesDialog.open() }
    }
}
