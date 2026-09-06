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
    required property var preferences

    readonly property bool sourceVisible: viewState.difficultyEditorActive
    readonly property bool canUndo: sourceVisible && sourceEditor.canUndo
    readonly property bool canRedo: sourceVisible && sourceEditor.canRedo
    readonly property bool canCut: sourceVisible && sourceEditor.canCut
    readonly property bool canCopy: sourceVisible && sourceEditor.canCopy
    readonly property bool canPaste: sourceVisible && sourceEditor.canPaste
    readonly property string selectionBeatStatusText:
        sourceVisible ? sourceEditor.selectionBeatStatusText : ""
    readonly property string selectionBeatTooltipText:
        sourceVisible ? sourceEditor.selectionBeatTooltipText : ""
    // Metadata actions use one shared column width so the button geometry does
    // not change with translated label length or with the input-field width.
    readonly property int metadataActionButtonWidth: 168
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
        return sourceVisible
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
            if (line <= 0) {
                if (completion)
                    completion()
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

    component DifficultyHeaderField: Item {
        id: fieldGroup

        required property string labelText
        required property string value
        required property real editorWidth
        property bool stacked: false
        property bool commitOnEditingFinished: false
        signal committed(string value)

        implicitWidth: fieldLabel.implicitWidth + 8 + fieldEditor.editorWidth
        implicitHeight: stacked
            ? fieldLabel.implicitHeight + 8 + fieldEditor.implicitHeight
            : fieldEditor.implicitHeight

        Label {
            id: fieldLabel
            x: 0
            y: fieldGroup.stacked ? 0 : (fieldEditor.implicitHeight - height) / 2
            text: fieldGroup.labelText
            color: Theme.colors.text.primary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
        }

        AppTextField {
            id: fieldEditor
            readonly property real editorWidth: fieldGroup.editorWidth

            x: fieldGroup.stacked ? 0 : fieldLabel.width + 8
            y: fieldGroup.stacked ? fieldLabel.height + 8 : 0
            width: fieldGroup.stacked ? fieldGroup.width : editorWidth
            text: fieldGroup.value
            onTextEdited: if (!fieldGroup.commitOnEditingFinished) fieldGroup.committed(text)
            onEditingFinished: if (fieldGroup.commitOnEditingFinished) fieldGroup.committed(text)
        }
    }

    Item {
        id: difficultyHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        height: headerContent.height + 12
        visible: root.viewState.difficultyEditorActive

        Item {
            id: headerContent
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: Theme.panelPadding
            anchors.rightMargin: Theme.panelPadding
            readonly property real gap: 8
            readonly property real wideWidth:
                levelField.implicitWidth + designerField.implicitWidth
                + offsetField.implicitWidth + removeDifficultyButton.implicitWidth
                + 3 * gap
            readonly property real firstRowWidth:
                levelField.implicitWidth + offsetField.implicitWidth + gap
            readonly property real secondRowWidth:
                designerField.implicitWidth + removeDifficultyButton.implicitWidth + gap
            readonly property real twoRowWidth:
                firstRowWidth >= secondRowWidth ? firstRowWidth : secondRowWidth
            readonly property bool wide: width >= wideWidth
            readonly property bool twoRows: !wide && width >= twoRowWidth

            height: removeDifficultyButton.y + removeDifficultyButton.height

            DifficultyHeaderField {
                id: levelField
                x: 0
                y: 0
                stacked: !headerContent.wide && !headerContent.twoRows
                    && headerContent.width < implicitWidth
                width: stacked ? headerContent.width : implicitWidth
                labelText: UiText.text("等级")
                value: root.documentSession.currentDifficultyLevel
                editorWidth: 48
                onCommitted: value => root.documentSession.currentDifficultyLevel = value
            }

            DifficultyHeaderField {
                id: designerField
                x: headerContent.wide
                    ? offsetField.x + offsetField.width + headerContent.gap : 0
                y: headerContent.wide ? 0
                    : headerContent.twoRows
                        ? levelField.height + headerContent.gap
                        : offsetField.y + offsetField.height + headerContent.gap
                stacked: !headerContent.wide && !headerContent.twoRows
                    && headerContent.width < implicitWidth
                width: stacked ? headerContent.width : implicitWidth
                labelText: UiText.text("谱师")
                value: root.documentSession.currentDifficultyDesigner
                editorWidth: 100
                onCommitted: value => root.documentSession.currentDifficultyDesigner = value
            }

            DifficultyHeaderField {
                id: offsetField
                x: headerContent.wide || headerContent.twoRows
                    ? levelField.x + levelField.width + headerContent.gap : 0
                y: headerContent.wide || headerContent.twoRows
                    ? 0 : levelField.y + levelField.height + headerContent.gap
                stacked: !headerContent.wide && !headerContent.twoRows
                    && headerContent.width < implicitWidth
                width: stacked ? headerContent.width : implicitWidth
                labelText: UiText.text("延迟")
                value: root.documentSession.currentDifficultyOffset
                editorWidth: 64
                commitOnEditingFinished: true
                onCommitted: value => root.documentSession.currentDifficultyOffset = value
            }

            AppButton {
                id: removeDifficultyButton
                x: parent.width - width
                y: headerContent.wide ? 0
                    : headerContent.twoRows
                        ? designerField.y
                        : designerField.y + designerField.height + headerContent.gap
                text: UiText.text("删除难度")
                emphasized: true
                enabled: root.documentSession.currentDifficultyId > 0
                onClicked: removeDifficultyDialog.open()
            }
        }
    }

    SourceEditor {
        id: sourceEditor
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: difficultyHeader.bottom
        anchors.bottom: parent.bottom
        visible: root.sourceVisible
        navigationVisible: root.visible && root.viewState.difficultyEditorActive
        viewState: root.viewState
        documentSession: root.documentSession
        editorController: root.editorController
        syncController: root.editorSync
        preferences: root.preferences
    }

    Flickable {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: tabs.bottom
        anchors.bottom: parent.bottom
        visible: root.viewState.metadataEditorActive
        contentHeight: metadataColumn.implicitHeight + 32
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: AppScrollBar {}

        Column {
            id: metadataColumn
            x: 20
            y: 16
            width: parent.width - 40
            spacing: 12

            Label {
                text: UiText.text("dialog.unsaved_field_changes.field.metadata")
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize + 2
            }

            Label {
                visible: root.documentSession.metadataNeedsAttention
                width: metadataColumn.width
                text: root.documentSession.metadataAttentionText
                color: Theme.colors.syntax.warning
                font.family: Theme.uiFont
                font.pixelSize: Theme.secondaryFontSize
                wrapMode: Text.WordWrap
            }

            MetadataField {
                width: metadataColumn.width
                label: UiText.text("标题")
                value: root.documentSession.metadataTitle
                actionText: UiText.text("track_metadata.read_from_audio")
                onCommitted: value => root.documentSession.metadataTitle = value
                onActionRequested: root.documentSession.readTitleFromAudioFile()
            }
            MetadataField {
                width: metadataColumn.width
                label: UiText.text("曲师")
                value: root.documentSession.metadataArtist
                actionText: UiText.text("track_metadata.read_from_audio")
                onCommitted: value => root.documentSession.metadataArtist = value
                onActionRequested: root.documentSession.readArtistFromAudioFile()
            }
            MetadataField {
                width: metadataColumn.width
                label: UiText.text("谱师")
                value: root.documentSession.metadataDesigner
                actionText: UiText.text("谱师名义管理")
                onCommitted: value => root.documentSession.metadataDesigner = value
                onActionRequested: designerSlotsDialog.open()
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
            MetadataMediaField {
                width: metadataColumn.width
                label: UiText.text("metadata.field.cover")
                firstActionText: UiText.text("track_metadata.read_from_audio")
                secondActionText: UiText.text("track_metadata.import_file")
                onFirstActionRequested: root.documentSession.extractCoverFromAudioFile()
                onSecondActionRequested: root.documentSession.importChartBackgroundImage()
            }
            MetadataMediaField {
                width: metadataColumn.width
                label: UiText.text("metadata.field.background_video")
                firstActionText: UiText.text("track_metadata.import_pv")
                secondActionText: UiText.text("track_metadata.remove_pv")
                secondActionEnabled: root.documentSession.metadataHasVideo
                onFirstActionRequested: root.documentSession.importChartBackgroundVideo()
                onSecondActionRequested: root.documentSession.removeChartPv()
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

    Connections {
        target: root.documentSession
        function onEditingFinishedRequested() {
            root.forceActiveFocus()
        }
        function onMetadataChanged() {
            if (extraFieldsEdit.activeFocus)
                return
            extraFieldsEdit.userEdited = false
            extraFieldsEdit.text = root.documentSession.metadataExtraText
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
        property string actionText: ""
        readonly property int actionWidth: root.metadataActionButtonWidth
        signal committed(string value)
        signal actionRequested()
        spacing: 4

        Label {
            text: field.label
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.secondaryFontSize
        }
        Item {
            width: field.width
            implicitHeight: Math.max(Theme.controlMinHeight, actionButton.implicitHeight)
            height: implicitHeight

            AppTextField {
                id: metadataInput
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                anchors.right: actionButton.visible ? actionButton.left : parent.right
                anchors.rightMargin: actionButton.visible ? 8 : 0
                text: field.value
                onEditingFinished: field.committed(text)
            }
            AppButton {
                id: actionButton
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.bottom: parent.bottom
                width: field.actionWidth
                visible: field.actionText.length > 0
                text: field.actionText
                onClicked: field.actionRequested()
            }
        }
    }

    component MetadataMediaField: Column {
        id: mediaField
        required property string label
        required property string firstActionText
        required property string secondActionText
        readonly property int actionWidth: root.metadataActionButtonWidth
        property bool secondActionEnabled: true
        signal firstActionRequested()
        signal secondActionRequested()
        spacing: 4

        Label {
            text: mediaField.label
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.secondaryFontSize
        }
        Row {
            width: mediaField.width
            spacing: 8

            AppButton {
                width: mediaField.actionWidth
                text: mediaField.firstActionText
                onClicked: mediaField.firstActionRequested()
            }
            AppButton {
                width: mediaField.actionWidth
                enabled: mediaField.secondActionEnabled
                text: mediaField.secondActionText
                onClicked: mediaField.secondActionRequested()
            }
        }
    }
}
