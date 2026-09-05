import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Item {
    id: root

    required property var viewState
    required property var documentSession
    required property var commands
    required property var editorController
    required property var editorSync

    readonly property bool metadataSourceActive: viewState.metadataEditorActive
        && viewState.metadataEditorMode === 1
    readonly property bool sourceVisible: viewState.difficultyEditorActive
        || metadataSourceActive
    readonly property bool canUndo: sourceVisible && sourceEditor.canUndo
    readonly property bool canRedo: sourceVisible && sourceEditor.canRedo
    readonly property bool canCut: sourceVisible && sourceEditor.canCut
    readonly property bool canCopy: sourceVisible && sourceEditor.canCopy
    readonly property bool canPaste: sourceVisible && sourceEditor.canPaste
    property double pendingActivationSequence: 0
    property var pendingActivationCompletion: null
    property var pendingActivationCancellation: null

    function undo() {
        if (sourceVisible)
            sourceEditor.undo()
    }

    function redo() {
        if (sourceVisible)
            sourceEditor.redo()
    }

    function cut() {
        if (sourceVisible)
            sourceEditor.cut()
    }

    function copy() {
        if (sourceVisible)
            sourceEditor.copy()
    }

    function paste() {
        if (sourceVisible)
            sourceEditor.paste()
    }

    // Keyboard tab-close must use the same three-way guard as the tab's x
    // button. Calling ViewState.closeEditor directly would discard a dirty
    // metadata or difficulty view without asking.
    function requestCloseActiveEditor() {
        if (viewState.activeEditorKey.length > 0)
            tabs.requestCloseTab(viewState.activeEditorKey)
    }

    function selectAll() {
        if (sourceVisible)
            sourceEditor.selectAll()
    }

    function openFindReplace() {
        if (sourceVisible)
            sourceEditor.openFindReplace()
    }

    function selectCurrentLine() {
        if (sourceVisible)
            sourceEditor.selectCurrentLine()
    }

    function applyChartTransform(opId) {
        return sourceVisible && sourceEditor.applyChartTransform(opId)
    }

    function canNormalizeChart() {
        return sourceVisible && !metadataSourceActive
    }

    function normalizationSelectionDescription() {
        return canNormalizeChart() ? sourceEditor.selectionDescription() : ""
    }

    function applyNormalization(options) {
        return canNormalizeChart() && sourceEditor.applyNormalization(options)
    }

    function revealSyntaxIssue(difficultyId, revision, line, column, endColumn) {
        if (revision !== root.documentSession.validationRevision
                || revision !== root.documentSession.documentRevision
                || root.documentSession.validationPending)
            return
        requestIssueNavigation(difficultyId, revision, line, column, endColumn, null, null)
    }

    function revealAnalysisRow(difficultyId, revision, line, column, endColumn, second, analysisSession) {
        const cancel = () => analysisSession.cancelRowActivation(
            difficultyId, revision, line, column, endColumn, second)
        if (revision !== root.documentSession.validationRevision
                || revision !== root.documentSession.documentRevision
                || root.documentSession.validationPending) {
            cancel()
            return
        }
        requestIssueNavigation(
            difficultyId, revision, line, column, endColumn,
            () => analysisSession.completeRowActivation(
                difficultyId, revision, line, column, endColumn, second),
            cancel)
    }

    function requestIssueNavigation(difficultyId, revision, line, column, endColumn,
                                    completion, cancellation) {
        if (difficultyId !== root.documentSession.currentDifficultyId)
            root.documentSession.selectDifficulty(difficultyId)
        root.viewState.openDifficultyEditor(difficultyId)
        Qt.callLater(() => {
            if (difficultyId !== root.documentSession.currentDifficultyId
                    || revision !== root.documentSession.documentRevision) {
                if (cancellation)
                    cancellation()
                return
            }
            const start = root.documentSession.chartPosition(line, column)
            const end = Math.max(start + 1, root.documentSession.chartPosition(
                line, Math.max(column, endColumn + 1)))
            const sequence = root.editorSync.requestNavigation(
                difficultyId, revision, start, end, true, true)
            if (sequence <= 0) {
                if (cancellation)
                    cancellation()
                return
            }
            root.pendingActivationSequence = sequence
            root.pendingActivationCompletion = completion
            root.pendingActivationCancellation = cancellation
        })
    }

    Connections {
        target: root.editorSync
        function onNavigationFinished(sequence, applied) {
            if (sequence !== root.pendingActivationSequence)
                return
            const completion = root.pendingActivationCompletion
            const cancellation = root.pendingActivationCancellation
            root.pendingActivationSequence = 0
            root.pendingActivationCompletion = null
            root.pendingActivationCancellation = null
            if (applied) {
                if (completion)
                    completion()
            } else if (cancellation) {
                cancellation()
            }
        }
    }

    // Header/form background stops where the independently shaded source begins.
    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        y: tabs.height
        height: (root.sourceVisible ? sourceEditor.y : root.height) - y
        color: Theme.surfaceColor(Theme.colors.background.panel)
    }

    EditorTabBar {
        id: tabs
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        viewState: root.viewState
        documentSession: root.documentSession
        commands: root.commands
    }

    Item {
        id: difficultyHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        height: 42
        visible: root.viewState.difficultyEditorActive

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: Theme.panelPadding
            anchors.rightMargin: Theme.panelPadding
            spacing: 8

            Label {
                text: UiText.text("等级")
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
            }

            AppTextField {
                property bool userEdited: false

                Layout.preferredWidth: 48
                Layout.maximumWidth: Layout.preferredWidth
                text: root.documentSession.currentDifficultyLevel
                onTextEdited: userEdited = true
                onEditingFinished: {
                    if (userEdited)
                        root.documentSession.currentDifficultyLevel = text
                    userEdited = false
                }
            }

            Label {
                text: UiText.text("谱师")
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
            }

            AppTextField {
                id: difficultyDesignerField
                property bool userEdited: false

                Layout.preferredWidth: 100
                Layout.maximumWidth: Layout.preferredWidth
                text: root.documentSession.currentDifficultyDesigner
                // 文档打开、难度切换和焦点转移都可能结束编辑状态。只有收到
                // TextInput 的真实编辑信号后，才把显示值提交给文档模型。
                onTextEdited: userEdited = true
                onEditingFinished: {
                    if (userEdited)
                        root.documentSession.currentDifficultyDesigner = text
                    userEdited = false
                }
            }

            Label {
                text: UiText.text("延迟")
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
            }

            AppTextField {
                property bool userEdited: false

                Layout.preferredWidth: 64
                Layout.maximumWidth: Layout.preferredWidth
                text: root.documentSession.metadataFirst
                onTextEdited: userEdited = true
                onEditingFinished: {
                    if (userEdited)
                        root.documentSession.metadataFirst = text
                    userEdited = false
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.minimumWidth: 2 * Theme.panelPadding
            }

            AppButton {
                text: UiText.text("删除难度")
                enabled: root.documentSession.currentDifficultyId > 0
                onClicked: removeDifficultyDialog.open()
            }
        }
    }

    Item {
        id: metadataModeBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        height: 42
        visible: root.viewState.metadataEditorActive

        AppButton {
            anchors.left: parent.left
            anchors.leftMargin: Theme.panelPadding - Theme.chromeInsetX
            anchors.verticalCenter: parent.verticalCenter
            text: root.viewState.metadataEditorMode === 0
                ? UiText.text("字段源码")
                : UiText.text("表单")
            onClicked: root.viewState.metadataEditorMode
                = root.viewState.metadataEditorMode === 0 ? 1 : 0
        }

        // 谱师名义管理 replaces the old 统一谱师 switch: the seven &des_N slots and
        // the shared-name checkbox live in one dialog, so a toggle can never
        // rewrite names without showing what it is about to overwrite. It sits
        // on this bar rather than in the form so it is reachable from 字段源码
        // mode too.
        AppButton {
            anchors.right: parent.right
            anchors.rightMargin: Theme.panelPadding
            anchors.verticalCenter: parent.verticalCenter
            text: UiText.text("document.designer_management")
            onClicked: designerSlotsDialog.open()
        }
    }

    Label {
        id: metadataSourceError
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: metadataModeBar.bottom
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        height: visible ? implicitHeight + 8 : 0
        visible: root.metadataSourceActive && !root.documentSession.metadataSourceValid
        text: root.documentSession.metadataSourceError
        color: Theme.colors.syntax.error
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize
        verticalAlignment: Text.AlignVCenter
        wrapMode: Text.Wrap
    }

    // 难度正文与头字段源码共用这一套编辑控件。标签或模式切换只改变
    // SourceEditor 的业务数据源，光标、滚动和编辑命令始终由这一实例持有。
    SourceEditor {
        id: sourceEditor
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: root.metadataSourceActive
            ? metadataSourceError.bottom
            : difficultyHeader.bottom
        anchors.bottom: parent.bottom
        visible: root.sourceVisible
        navigationVisible: root.visible && root.viewState.difficultyEditorActive
        metadataMode: root.metadataSourceActive
        viewState: root.viewState
        documentSession: root.documentSession
        editorController: root.editorController
        syncController: root.editorSync
    }

    Flickable {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: metadataModeBar.bottom
        anchors.bottom: parent.bottom
        visible: root.viewState.metadataEditorActive
            && root.viewState.metadataEditorMode === 0
        contentHeight: metadataColumn.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: metadataColumn
            x: 20
            y: 16
            width: Math.max(240, parent.width - 40)
            spacing: 12

            Label {
                text: UiText.text("dialog.unsaved_field_changes.field.metadata")
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize + 2
            }

            MetadataField {
                width: metadataColumn.width
                label: UiText.text("标题")
                value: root.documentSession.metadataTitle
                onCommitted: value => root.documentSession.metadataTitle = value
            }
            MetadataField {
                width: metadataColumn.width
                label: UiText.text("曲师")
                value: root.documentSession.metadataArtist
                onCommitted: value => root.documentSession.metadataArtist = value
            }
            MetadataField {
                width: metadataColumn.width
                label: UiText.text("谱师")
                value: root.documentSession.metadataDesigner
                onCommitted: value => root.documentSession.metadataDesigner = value
            }
            MetadataField {
                width: metadataColumn.width
                label: UiText.text("metadata.field.first")
                value: root.documentSession.metadataFirst
                onCommitted: value => root.documentSession.metadataFirst = value
            }
            MetadataField {
                width: metadataColumn.width
                label: UiText.text("media_tools.beats")
                value: root.documentSession.metadataClockCount
                onCommitted: value => root.documentSession.metadataClockCount = value
            }

            Label {
                text: UiText.text("其他 &xx 字段")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.secondaryFontSize
            }
            AppTextArea {
                id: extraFieldsEdit
                property bool userEdited: false

                width: metadataColumn.width
                height: 150
                text: root.documentSession.metadataExtraText
                placeholderText: UiText.text("每行一个 &字段=值")
                onTextChanged: {
                    if (activeFocus && text !== root.documentSession.metadataExtraText)
                        extraFieldsEdit.userEdited = true
                }
                onActiveFocusChanged: {
                    if (!activeFocus && extraFieldsEdit.userEdited) {
                        root.documentSession.metadataExtraText = text
                        extraFieldsEdit.userEdited = false
                    }
                }
            }
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !root.viewState.hasActiveEditor
        text: UiText.text("从左侧打开元数据或难度")
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }

    DesignerSlotsDialog {
        id: designerSlotsDialog

        documentSession: root.documentSession
        commands: root.commands
    }

    ChoiceDialog {
        id: removeDifficultyDialog

        title: UiText.text("删除当前难度")
        message: UiText.text("当前难度及其正文将从文档中删除。")
        dismissChoiceId: "cancel"
        choices: [
            { id: "cancel", label: UiText.text("取消"), role: "reject" },
            { id: "remove", label: UiText.text("确定"), role: "accept" }
        ]
        onChosen: function(choiceId) {
            if (choiceId === "remove")
                root.commands.removeDifficulty(root.documentSession.currentDifficultyId)
        }
    }

    component MetadataField: Column {
        id: field
        required property string label
        required property string value
        signal committed(string value)
        spacing: 4

        Label {
            text: field.label
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.secondaryFontSize
        }
        AppTextField {
            property bool userEdited: false
            width: field.width
            text: field.value
            onTextEdited: userEdited = true
            onEditingFinished: {
                if (userEdited)
                    field.committed(text)
                userEdited = false
            }
        }
    }
    Connections {
        target: root.documentSession
        function onMetadataChanged() {
            if (extraFieldsEdit.activeFocus)
                return
            extraFieldsEdit.userEdited = false
            extraFieldsEdit.text = root.documentSession.metadataExtraText
        }
    }
}
