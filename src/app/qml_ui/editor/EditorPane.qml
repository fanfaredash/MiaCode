import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Rectangle {
    id: root

    required property var viewState
    required property var documentSession
    required property var commands
    required property var editorController
    required property var editorSync
    required property var pages

    readonly property bool metadataSourceActive: viewState.metadataEditorActive
        && viewState.metadataEditorMode === 1
    readonly property bool sourceVisible: viewState.difficultyEditorActive
        || metadataSourceActive
    readonly property bool canUndo: sourceVisible && sourceEditor.canUndo
    readonly property bool canRedo: sourceVisible && sourceEditor.canRedo
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

    color: Theme.surfaceColor("codeEditor", Theme.colors.background.editor)

    EditorTabBar {
        id: tabs
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        viewState: root.viewState
        documentSession: root.documentSession
        commands: root.commands
    }

    Rectangle {
        id: difficultyHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        height: 42
        visible: root.viewState.difficultyEditorActive
        color: Theme.colors.background.surface

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 8
            spacing: 8

            Label {
                text: root.documentSession.currentDifficultyName
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
            }

            AppTextField {
                property bool userEdited: false

                Layout.preferredWidth: 90
                placeholderText: UiText.text("等级")
                text: root.documentSession.currentDifficultyLevel
                onTextEdited: userEdited = true
                onEditingFinished: {
                    if (userEdited)
                        root.documentSession.currentDifficultyLevel = text
                    userEdited = false
                }
            }

            AppTextField {
                id: difficultyDesignerField
                property bool userEdited: false

                Layout.fillWidth: true
                placeholderText: UiText.text("谱师")
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

            AppButton {
                text: UiText.text("删除难度")
                enabled: root.documentSession.currentDifficultyId > 0
                onClicked: removeDifficultyDialog.open()
            }
        }
    }

    Rectangle {
        id: metadataModeBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        height: 42
        visible: root.viewState.metadataEditorActive
        color: Theme.colors.background.surface

        AppButton {
            anchors.left: parent.left
            anchors.leftMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: root.viewState.metadataEditorMode === 0
                ? UiText.text("字段源码")
                : UiText.text("表单")
            onClicked: root.viewState.metadataEditorMode
                = root.viewState.metadataEditorMode === 0 ? 1 : 0
        }

        AppSwitch {
            id: unifiedDesignerSwitch
            anchors.right: parent.right
            anchors.rightMargin: 10
            anchors.verticalCenter: parent.verticalCenter
            text: UiText.text("统一谱师")

            onToggled: {
                if (checked === root.documentSession.unifiedDesignerEnabled)
                    return
                if (!checked) {
                    root.commands.disableUnifiedDesigner()
                    return
                }
                const candidates = root.documentSession.designerCandidates
                if (candidates.length > 1) {
                    canonicalDesignerDialog.candidates = candidates
                    designerChoice.currentIndex = 0
                    checked = root.documentSession.unifiedDesignerEnabled
                    canonicalDesignerDialog.open()
                    return
                }
                root.commands.enableUnifiedDesigner(
                    candidates.length === 1 ? candidates[0] : "")
            }

            Binding {
                target: unifiedDesignerSwitch
                property: "checked"
                value: root.documentSession.unifiedDesignerEnabled
            }
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
        ScrollBar.vertical: ScrollBar {}

        Column {
            id: metadataColumn
            x: 20
            y: 16
            width: Math.max(240, parent.width - 40)
            spacing: 12

            Label {
                text: UiText.text("元数据")
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
                label: UiText.text("艺术家")
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
                label: UiText.text("初始偏移")
                value: root.documentSession.metadataFirst
                onCommitted: value => root.documentSession.metadataFirst = value
            }
            MetadataField {
                width: metadataColumn.width
                label: UiText.text("视频路径")
                value: root.documentSession.metadataVideoPath
                onCommitted: value => root.documentSession.metadataVideoPath = value
            }

            Label {
                text: UiText.text("其他字段")
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

    Dialog {
        id: canonicalDesignerDialog
        property var candidates: []

        anchors.centerIn: parent
        modal: true
        title: UiText.text("选择统一谱师")
        footer: DialogFooter {
            acceptText: UiText.text("确定")
            cancelText: UiText.text("取消")
            onAccepted: canonicalDesignerDialog.accept()
            onRejected: canonicalDesignerDialog.reject()
        }
        onAccepted: root.commands.enableUnifiedDesigner(designerChoice.currentText)
        onRejected: unifiedDesignerSwitch.checked = root.documentSession.unifiedDesignerEnabled

        Column {
            spacing: 8

            Label {
                text: UiText.text("当前存在多个谱师名义，请选择要统一使用的值。")
                color: Theme.colors.text.primary
            }
            AppComboBox {
                id: designerChoice
                width: 280
                model: canonicalDesignerDialog.candidates
            }
        }
    }

    Dialog {
        id: removeDifficultyDialog
        anchors.centerIn: parent
        modal: true
        title: UiText.text("删除当前难度")
        footer: DialogFooter {
            acceptText: UiText.text("确定")
            cancelText: UiText.text("取消")
            onAccepted: removeDifficultyDialog.accept()
            onRejected: removeDifficultyDialog.reject()
        }
        onAccepted: root.commands.removeDifficulty(root.documentSession.currentDifficultyId)

        Label {
            text: UiText.text("当前难度及其正文将从文档中删除。")
            color: Theme.colors.text.primary
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
    // 整谱规范化: the sidebar entry asks, this pane collects options and applies
    // the transform as one editor transaction so undo covers it.
    Connections {
        target: root.documentSession
        function onMetadataChanged() {
            if (extraFieldsEdit.activeFocus)
                return
            extraFieldsEdit.userEdited = false
            extraFieldsEdit.text = root.documentSession.metadataExtraText
        }
    }

    Connections {
        target: root.pages
        function onNormalizeWholeChartRequested() {
            // Normalize acts on chart body. With no difficulty open (or the
            // metadata source showing) there is nothing to act on, so the entry
            // does nothing rather than transforming the wrong text.
            if (!root.sourceVisible || root.metadataSourceActive)
                return
            const stored = root.documentSession.normalizeOptions()
            normalizeDialog.reduceTo384Grid = stored.reduceTo384Grid
            normalizeDialog.sectionMeasureCount = stored.sectionMeasureCount
            normalizeDialog.syntax = stored.syntax
            normalizeDialog.selectionDescription = sourceEditor.selectionDescription()
            normalizeDialog.open()
        }
    }

    NormalizeOptionsDialog {
        id: normalizeDialog
        objectName: "normalizeOptionsDialog"
        documentSession: root.documentSession

        onAccepted: {
            const options = {
                reduceTo384Grid: normalizeDialog.reduceTo384Grid,
                sectionMeasureCount: normalizeDialog.sectionMeasureCount,
                syntax: normalizeDialog.syntax
            }
            root.documentSession.setNormalizeOptions(options)
            sourceEditor.applyNormalization(options)
        }
    }

}
