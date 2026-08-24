import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var viewState
    required property var documentSession
    required property var editorController
    property bool metadataMode: false
    // EditorPane must provide effective visibility (including its own host),
    // so an editor retained behind an overlay cannot acknowledge navigation.
    property bool navigationVisible: false
    property bool imeComposing: false
    // Backend-originated selections must not loop back into the legacy
    // cursor→timeline bridge.  QML remains the visual owner of the caret.
    property bool suppressBackendCaretPublish: false
    property var bookmarks: []
    property int pendingBookmarkLine: -1
    onMetadataModeChanged: {
        syncTextFromController()
        publishNavigationReadiness()
    }
    onNavigationVisibleChanged: publishNavigationReadiness()

    function publishNavigationReadiness() {
        if (!root.documentSession)
            return
        root.documentSession.setQmlEditorNavigationReadiness(
            root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
            root.navigationVisible, root.metadataMode)
    }

    function syncTextFromController() {
        const controllerText = root.metadataMode
            ? root.documentSession.metadataSourceText
            : root.documentSession.chartText
        if (sourceArea.text === controllerText)
            return
        sourceArea.syncingFromController = true
        sourceArea.text = controllerText
        sourceArea.syncingFromController = false
        sourceArea.historyText = controllerText
        sourceArea.historyAnchor = sourceArea.selectionStart
        sourceArea.historyPosition = sourceArea.selectionEnd
        root.editorController.resetQmlHistory(controllerText, sourceArea.historyAnchor,
                                              sourceArea.historyPosition)
        updateCursorPosition()
    }
    readonly property real codeLineHeight: sourceArea.cursorRectangle.height > 0
                                               ? sourceArea.cursorRectangle.height
                                               : 20
    // 每个逻辑行顶部在文档坐标系中的 y。自动换行后行高不再固定，
    // 当前行号与行号 gutter 都按真实行顶坐标定位。
    property var lineTops: []
    readonly property int activeLine: {
        const tops = root.lineTops
        const y = sourceArea.cursorRectangle.y - sourceArea.topPadding
        let low = 0
        let high = tops.length - 1
        let result = 0
        while (low <= high) {
            const mid = (low + high) >> 1
            if (tops[mid] <= y) {
                result = mid
                low = mid + 1
            } else {
                high = mid - 1
            }
        }
        // lineTops 的下标从 0 开始，行号 gutter 的公开行号从 1 开始。
        return result + 1
    }

    color: Theme.colors.background.editor
    clip: true

    readonly property bool canUndo: editorController.canUndo
    readonly property bool canRedo: editorController.canRedo

    function undo() {
        applyEditorTransaction(editorController.undoQmlTransaction(), true)
    }

    function redo() {
        applyEditorTransaction(editorController.redoQmlTransaction(), true)
    }

    function selectAll() {
        sourceArea.selectAll()
    }

    function openFindReplace() {
        findReplaceBar.show()
    }

    function selectCurrentLine() {
        const start = sourceArea.text.lastIndexOf("\n", Math.max(0, sourceArea.cursorPosition - 1)) + 1
        const endAt = sourceArea.text.indexOf("\n", sourceArea.cursorPosition)
        sourceArea.select(start, endAt < 0 ? sourceArea.text.length : endAt)
    }

    function jumpToLine(line) {
        const position = root.documentSession.chartPosition(Math.max(1, line), 1)
        sourceArea.forceActiveFocus()
        sourceArea.cursorPosition = position
        centerCursorInView()
    }

    function centerCursorInView() {
        Qt.callLater(() => {
            const flickable = editorScroll.contentItem
            const target = sourceArea.y + sourceArea.cursorRectangle.y
                + sourceArea.cursorRectangle.height / 2 - flickable.height / 2
            flickable.contentY = Math.max(0, Math.min(
                Math.max(0, flickable.contentHeight - flickable.height), target))
        })
    }

    function selectBackendNavigation(line, column, endLine, endColumn, selectToken, focusEditor, centerView) {
        const position = root.documentSession.chartPosition(Math.max(1, line), Math.max(1, column))
        let start = position
        let end = position
        const hasExactSelection = selectToken && (endLine > line || endColumn > column)
        if (hasExactSelection) {
            // Timeline spans use an inclusive end column, whereas TextArea
            // selection uses an exclusive offset.
            end = Math.max(position, root.documentSession.chartPosition(
                Math.max(1, endLine), Math.max(1, endColumn + 1)))
        } else if (selectToken) {
            const delimiters = " /,`\n\r\t"
            while (start > 0 && delimiters.indexOf(sourceArea.text.charAt(start - 1)) < 0)
                --start
            while (end < sourceArea.text.length && delimiters.indexOf(sourceArea.text.charAt(end)) < 0)
                ++end
            if (end <= start)
                end = Math.min(sourceArea.text.length, start + 1)
        }
        root.suppressBackendCaretPublish = true
        sourceArea.select(start, end)
        if (focusEditor)
            sourceArea.forceActiveFocus()
        root.suppressBackendCaretPublish = false
        if (centerView)
            centerCursorInView()
    }

    function createBookmarkAtLine(line) {
        return applyEditorTransaction(editorController.createBookmarkForQml(
            sourceArea.text, line, qsTr("书签")))
    }
    function createBookmarkAtCurrentLine() {
        return createBookmarkAtLine(activeLine)
    }
    function deleteBookmarkAtCurrentLine() {
        return applyEditorTransaction(editorController.deleteBookmarkForQml(sourceArea.text, activeLine))
    }
    function deleteBookmarkAtLine(line) {
        return applyEditorTransaction(editorController.deleteBookmarkForQml(sourceArea.text, line))
    }
    function renameBookmarkAtLine(line, title) {
        return applyEditorTransaction(editorController.renameBookmarkForQml(sourceArea.text, line, title))
    }
    function promptRenameBookmark(line) {
        const bookmark = root.bookmarks.find(item => item.line === line)
        if (!bookmark)
            return
        root.pendingBookmarkLine = line
        bookmarkTitleField.text = bookmark.title
        bookmarkTitleDialog.open()
    }

    function collectBookmarks() {
        return root.editorController.bookmarksForQml(sourceArea.text)
    }

    function revealSyntaxIssue(difficultyId, revision, line, column, endColumn, completion, cancellation) {
        if (root.metadataMode) {
            if (cancellation)
                cancellation()
            return
        }
        if (difficultyId > 0 && difficultyId !== root.documentSession.currentDifficultyId)
            root.documentSession.selectDifficulty(difficultyId)
        Qt.callLater(() => {
            // Diagnostics are 1-based and are valid only for the revision
            // that produced them; select after a requested difficulty switch.
            if (root.documentSession.validationPending
                    || revision !== root.documentSession.validationRevision
                    || root.documentSession.validationRevision
                       !== root.documentSession.documentRevision) {
                if (cancellation)
                    cancellation()
                return
            }
            const start = root.documentSession.chartPosition(line, column)
            const end = root.documentSession.chartPosition(line, Math.max(column, endColumn + 1))
            root.suppressBackendCaretPublish = true
            sourceArea.forceActiveFocus()
            sourceArea.select(start, Math.max(start + 1, end))
            root.suppressBackendCaretPublish = false
            root.centerCursorInView()
            if (completion)
                completion()
        })
    }

    function updateCursorPosition() {
        const text = sourceArea.text
        const pos = Math.max(0, Math.min(text.length, sourceArea.cursorPosition))
        const before = text.substring(0, pos)
        const lines = before.split("\n")
        root.viewState.editorCursorLine = lines.length
        root.viewState.editorCursorColumn = lines[lines.length - 1].length + 1
    }

    function applyEditorTransaction(transaction, centerCursor) {
        if (!transaction.consumed)
            return false
        if (transaction.hasEdit) {
            const before = sourceArea.text
            const beforeAnchor = sourceArea.selectionStart
            const beforePosition = sourceArea.selectionEnd
            sourceArea.syncingFromController = true
            // TextEdit's mutation API keeps its native undo stack. A complete
            // replacement remains one logical controller transaction rather
            // than resetting the document by assigning `text`.
            sourceArea.remove(transaction.replacementStart, transaction.replacementEnd)
            sourceArea.insert(transaction.replacementStart, transaction.replacementText)
            sourceArea.select(transaction.anchor, transaction.position)
            sourceArea.syncingFromController = false
            if (transaction.undoGroup)
                root.editorController.recordQmlTransaction(before, sourceArea.text,
                                                           beforeAnchor, beforePosition,
                                                           transaction.anchor, transaction.position)
            sourceArea.historyText = sourceArea.text
            sourceArea.historyAnchor = sourceArea.selectionStart
            sourceArea.historyPosition = sourceArea.selectionEnd
            if (root.metadataMode)
                root.documentSession.metadataSourceText = sourceArea.text
            else
                root.documentSession.chartText = sourceArea.text
        }
        if (centerCursor)
            root.centerCursorInView()
        return true
    }

    function applyImeCommittedText(committedText) {
        return applyEditorTransaction(editorController.processImeCommitForQml(
            sourceArea.text, sourceArea.selectionStart, sourceArea.selectionEnd, committedText))
    }

    function applyPastePayload(pastedText) {
        return applyEditorTransaction(editorController.processPasteForQml(
            sourceArea.text, sourceArea.selectionStart, sourceArea.selectionEnd, pastedText))
    }

    LineNumberGutter {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        lineCount: sourceArea.lineCount
        activeLine: root.activeLine
        contentY: editorScroll.contentItem.contentY
        lineTops: root.lineTops
        rowHeight: root.codeLineHeight
        bookmarkedLines: root.bookmarks
        onJumpRequested: root.jumpToLine(line)
        onCreateRequested: line => root.createBookmarkAtLine(line)
        onDeleteRequested: line => root.deleteBookmarkAtLine(line)
        onRenameRequested: line => root.promptRenameBookmark(line)
    }

    Dialog {
        id: bookmarkTitleDialog
        parent: Overlay.overlay
        modal: true
        title: qsTr("重命名书签")
        standardButtons: Dialog.Ok | Dialog.Cancel
        onAccepted: root.renameBookmarkAtLine(root.pendingBookmarkLine, bookmarkTitleField.text)
        AppTextField {
            id: bookmarkTitleField
            width: 260
            Accessible.name: qsTr("书签名称")
        }
    }

    Menu {
        id: editorContextMenu
        parent: Overlay.overlay

        MenuItem {
            text: qsTr("剪切")
            enabled: sourceArea.selectedText.length > 0
            onTriggered: sourceArea.cut()
        }
        MenuItem {
            text: qsTr("复制")
            enabled: sourceArea.selectedText.length > 0
            onTriggered: sourceArea.copy()
        }
        MenuItem {
            text: qsTr("粘贴")
            onTriggered: root.applyPastePayload(root.editorController.clipboardText())
        }
        MenuSeparator {}
        MenuItem {
            text: qsTr("全选")
            onTriggered: root.selectAll()
        }
        MenuItem {
            text: qsTr("查找与替换")
            onTriggered: root.openFindReplace()
        }
    }

    FindReplaceBar {
        id: findReplaceBar
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        z: 4
        // The bar must operate the concrete TextArea, not its surrounding
        // layout item; proxy methods below retain the one transaction owner.
        editor: sourceArea
        controller: root.editorController
    }

    ScrollView {
        id: editorScroll
        anchors.left: parent.left
        anchors.leftMargin: 46
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true
        // TextEdit 与 QSyntaxHighlighter 组合在布局尺寸变化后存在不重绘的
        // 已知问题（QTBUG-58092 一类），窗口缩放后内容要等交互才刷新。
        // 视口尺寸变化时重新应用高亮，强制文本布局重绘。
        function refreshHighlight() {
            Qt.callLater(() => sourceArea.rehighlight())
        }
        function refreshLineTops() {
            Qt.callLater(() => root.lineTops = highlighter.lineTopPositions())
        }
        onWidthChanged: {
            refreshHighlight()
            refreshLineTops()
        }
        onHeightChanged: refreshHighlight()
        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
        }
        ScrollBar.vertical: ScrollBar {
            id: verticalScrollBar
            policy: ScrollBar.AsNeeded
        }

        TextArea {
            id: sourceArea
            property bool syncingFromController: false
            property bool readyForUserEdits: false
            property string historyText: ""
            property int historyAnchor: 0
            property int historyPosition: 0

            // 自动换行后内容宽度受视口宽度约束，若再用 contentWidth 参与
            // 宽度绑定会形成 width -> contentWidth -> width 的循环。
            width: editorScroll.availableWidth
            height: Math.max(editorScroll.availableHeight, contentHeight + topPadding + bottomPadding)
            padding: 12
            color: Theme.colors.text.editor
            selectedTextColor: Theme.colors.text.editor
            selectionColor: Theme.colors.state.textSelection
            // 自动换行：超宽行在视口内折行显示，避免依赖水平滚动查看长句。
            wrapMode: TextEdit.Wrap
            persistentSelection: true
            selectByMouse: true
            font: Theme.codeFont
            background: Rectangle {
                color: Theme.colors.background.editor
            }

            Rectangle {
                x: sourceArea.leftPadding
                y: Math.max(0, sourceArea.cursorRectangle.y)
                width: sourceArea.width - sourceArea.leftPadding - sourceArea.rightPadding
                height: root.codeLineHeight
                color: Theme.colors.state.lineHighlight
                z: -1
            }
            onTextChanged: {
                if (readyForUserEdits && !syncingFromController) {
                    root.editorController.recordQmlTransaction(historyText, text,
                                                               historyAnchor, historyPosition,
                                                               selectionStart, selectionEnd)
                    if (root.metadataMode)
                        root.documentSession.metadataSourceText = text
                    else
                        root.documentSession.chartText = text
                }
                root.updateCursorPosition()
                editorScroll.refreshLineTops()
                root.bookmarks = root.collectBookmarks()
                historyText = text
                historyAnchor = selectionStart
                historyPosition = selectionEnd
            }
            onCursorPositionChanged: {
                root.updateCursorPosition()
                if (!root.suppressBackendCaretPublish
                        && root.editorController.publishCaretForQml(
                        root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
                        selectionStart, selectionEnd, root.imeComposing)) {
                    root.documentSession.publishEditorCaret(
                        root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
                        root.viewState.editorCursorLine, root.viewState.editorCursorColumn)
                }
                root.documentSession.setQmlEditorInteraction(
                    root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
                    selectionStart, selectionEnd, activeFocus, root.imeComposing)
            }
            onActiveFocusChanged: root.documentSession.setQmlEditorInteraction(
                root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
                selectionStart, selectionEnd, activeFocus, root.imeComposing)
            function applyEditorTransaction(transaction) {
                return root.applyEditorTransaction(transaction)
            }
            function jumpToLine(line) {
                root.jumpToLine(line)
            }
            function centerCursorInView() {
                root.centerCursorInView()
            }
            // TextArea otherwise consumes completion keys before the QML
            // controller can apply its transaction.
            Keys.priority: Keys.BeforeItem
            Keys.onPressed: function(event) {
                if (event.key === Qt.Key_Control) {
                    root.documentSession.setQmlTouchPadAuthoringCtrlHold(true)
                    return
                }
                if (event.matches(StandardKey.Find)) {
                    root.openFindReplace()
                    event.accepted = true
                    return
                }
                if ((event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)
                        && event.key === Qt.Key_B) {
                    root.createBookmarkAtCurrentLine()
                    event.accepted = true
                    return
                }
                if ((event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)
                        && event.key === Qt.Key_Delete) {
                    root.deleteBookmarkAtCurrentLine()
                    event.accepted = true
                    return
                }
                if (event.matches(StandardKey.SelectAll)) {
                    root.selectAll()
                    event.accepted = true
                    return
                }
                if (event.matches(StandardKey.Undo)) {
                    root.undo()
                    event.accepted = true
                    return
                }
                if (event.matches(StandardKey.Redo)) {
                    root.redo()
                    event.accepted = true
                    return
                }
                if (event.matches(StandardKey.Paste)) {
                    if (root.applyPastePayload(root.editorController.clipboardText()))
                        event.accepted = true
                    return
                }
                const transaction = root.editorController.processKeyForQml(
                    sourceArea.text, sourceArea.selectionStart, sourceArea.selectionEnd,
                    event.text, event.key, event.modifiers)
                if (root.applyEditorTransaction(transaction))
                    event.accepted = true
            }
            Keys.onReleased: function(event) {
                if (event.key === Qt.Key_Control)
                    root.documentSession.setQmlTouchPadAuthoringCtrlHold(false)
            }
            Component.onCompleted: {
                root.syncTextFromController()
                readyForUserEdits = true
                historyText = text
                historyAnchor = selectionStart
                historyPosition = selectionEnd
                root.editorController.resetQmlHistory(text, historyAnchor, historyPosition)
                root.editorController.setDocumentContextForQml(
                    root.documentSession.currentDifficultyId, root.documentSession.documentRevision)
                root.updateCursorPosition()
                editorScroll.refreshLineTops()
            }

            SimaiSyntaxHighlighter {
                id: highlighter
                textDocument: sourceArea.textDocument
                keywordColor: Theme.colors.syntax.keyword
                commentColor: Theme.colors.syntax.comment
                durationColor: Theme.colors.syntax.duration
                modifierColor: Theme.colors.syntax.modifier
                errorColor: Theme.colors.syntax.error
                warningColor: Theme.colors.syntax.warning
                diagnostics: root.metadataMode || root.documentSession.validationPending
                    || root.documentSession.validationRevision !== root.documentSession.documentRevision
                    ? [] : root.documentSession.syntaxIssues
            }
            // 视口尺寸变化时由 editorScroll.refreshHighlight() 触发重高亮。
            function rehighlight() {
                highlighter.rehighlight()
            }

            cursorDelegate: Rectangle {
                width: 1
                color: Theme.colors.text.editor
            }

            CompletionPopup {
                editor: sourceArea
                controller: root.editorController
            }

            QmlEditorInputBridge {
                target: sourceArea
                onImeComposingChanged: root.imeComposing = composing
                onImeCommitted: function(text) {
                    root.applyImeCommittedText(text)
                }
            }

            function acceptCompletionFromPopup() {
                root.applyEditorTransaction(root.editorController.acceptCompletionForQml(
                    sourceArea.text, sourceArea.selectionStart, sourceArea.selectionEnd))
                sourceArea.forceActiveFocus()
            }

            TapHandler {
                acceptedButtons: Qt.RightButton
                gesturePolicy: TapHandler.ReleaseWithinBounds
                onTapped: function(eventPoint) {
                    sourceArea.forceActiveFocus()
                    const point = sourceArea.mapToItem(editorContextMenu.parent,
                                                       eventPoint.position.x, eventPoint.position.y)
                    editorContextMenu.popup(point.x, point.y)
                }
            }
        }
    }

    Connections {
        target: root.documentSession
        function onChartTextChanged() {
            if (!root.metadataMode)
                root.syncTextFromController()
        }
        function onMetadataSourceChanged() {
            if (root.metadataMode)
                root.syncTextFromController()
        }
        function onDocumentStateChanged() {
            root.editorController.setDocumentContextForQml(
                root.documentSession.currentDifficultyId, root.documentSession.documentRevision)
            root.documentSession.setQmlEditorInteraction(
                root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
                sourceArea.selectionStart, sourceArea.selectionEnd, sourceArea.activeFocus,
                root.imeComposing)
            root.publishNavigationReadiness()
        }
        function onQmlEditorNavigationRequested(difficultyId, revision, line, column, endLine,
                                                endColumn, selectToken, focusEditor, centerView) {
            if (!root.navigationVisible || root.metadataMode
                    || difficultyId !== root.documentSession.currentDifficultyId
                    || revision !== root.documentSession.documentRevision)
                return
            root.selectBackendNavigation(
                line, column, endLine, endColumn, selectToken, focusEditor, centerView)
        }
        function onQmlTouchPadAuthoringRequested(pad, useBacktickSeparator, difficultyId,
                                                  revision, anchor, position) {
            if (root.metadataMode || !sourceArea.activeFocus || root.imeComposing
                    || difficultyId !== root.documentSession.currentDifficultyId
                    || revision !== root.documentSession.documentRevision)
                return
            sourceArea.select(anchor, position)
            const tx = root.editorController.touchPadAuthoringForQml(
                sourceArea.text, anchor, position, pad, useBacktickSeparator)
            if (root.applyEditorTransaction(tx)) {
                root.documentSession.setTouchPadAuthoringPreviewAnchor(
                    difficultyId, root.documentSession.documentRevision,
                    sourceArea.text, tx.touchTokenStart)
            }
        }
    }

    Binding {
        target: editorScroll.contentItem
        property: "boundsBehavior"
        value: Flickable.StopAtBounds
    }

    Component.onCompleted: publishNavigationReadiness()
    Component.onDestruction: {
        if (root.documentSession)
            root.documentSession.setQmlEditorNavigationReadiness(-1, 0, false, true)
    }

    onImeComposingChanged: {
        root.documentSession.setQmlEditorInteraction(
            root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
            sourceArea.selectionStart, sourceArea.selectionEnd, sourceArea.activeFocus, root.imeComposing)
        if (root.imeComposing)
            root.documentSession.setQmlTouchPadAuthoringCtrlHold(false)
    }
}
