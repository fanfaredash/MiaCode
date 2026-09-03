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

        AppSwitch {
            id: unifiedDesignerSwitch
            anchors.right: parent.right
            anchors.rightMargin: Theme.panelPadding
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
        ScrollBar.vertical: AppScrollBar {}

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

    AppDialog {
        id: canonicalDesignerDialog

        property var candidates: []

        title: UiText.text("选择统一谱师")
        footer: DialogFooter {
            acceptText: UiText.text("确定")
            cancelText: UiText.text("取消")
            onAccepted: canonicalDesignerDialog.accept()
            onRejected: canonicalDesignerDialog.reject()
        }
        onAccepted: root.commands.enableUnifiedDesigner(designerChoice.currentText)
        onRejected: unifiedDesignerSwitch.checked = root.documentSession.unifiedDesignerEnabled

        body: ColumnLayout {
            spacing: 8

            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                text: UiText.text("当前存在多个谱师名义，请选择要统一使用的值。")
                color: Theme.colors.text.primary
            }
            AppComboBox {
                id: designerChoice
                Layout.fillWidth: true
                model: canonicalDesignerDialog.candidates
            }
        }
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
