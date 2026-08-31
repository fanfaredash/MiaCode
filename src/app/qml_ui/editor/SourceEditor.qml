import QtQuick
import QtQuick.Controls
import QtQuick.Window
import MiaCode.UI

Rectangle {
    id: root

    required property var viewState
    required property var documentSession
    required property var editorController
    required property var syncController
    property bool metadataMode: false
    // EditorPane must provide effective visibility (including its own host),
    // so an editor retained behind an overlay cannot acknowledge navigation.
    property bool navigationVisible: false
    property bool imeComposing: false
    property int programmaticSelectionDepth: 0
    property bool contextCaretPending: false
    property double pendingNavigationSequence: 0
    property bool pendingNavigationApplied: false
    property var bookmarks: []
    property int pendingBookmarkLine: -1
    // Read-only projection of preview follow while paused or with 代码跟随 off.
    // It paints where the playhead is; it must never move the real caret.
    property bool followDecorationActive: false
    property int followDecorationStart: 0
    property int followDecorationEnd: 0
    property int followDecorationCursor: 0
    onMetadataModeChanged: {
        syncTextFromController()
        publishNavigationReadiness()
        scheduleEditorContext(false)
        root.followDecorationActive = false
    }
    onNavigationVisibleChanged: {
        publishNavigationReadiness()
        scheduleEditorContext(false)
        applyFollowProjection()
    }

    function publishNavigationReadiness() {
        if (!root.syncController || !root.documentSession)
            return
        root.syncController.setEditorReadiness(
            root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
            root.navigationVisible, root.metadataMode)
    }

    function scheduleEditorContext(publishCaret) {
        root.contextCaretPending = root.contextCaretPending
            || (publishCaret && root.programmaticSelectionDepth === 0)
        editorContextTimer.restart()
    }

    Timer {
        id: editorContextTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (!root.syncController || !root.documentSession)
                return
            const publishCaret = root.contextCaretPending
                && root.editorController.publishCaretForQml(
                    root.documentSession.currentDifficultyId,
                    root.documentSession.documentRevision,
                    sourceArea.selectionStart, sourceArea.selectionEnd,
                    root.imeComposing)
            root.contextCaretPending = false
            root.syncController.setEditorContext(
                root.documentSession.currentDifficultyId,
                root.documentSession.documentRevision,
                sourceArea.selectionStart, sourceArea.selectionEnd,
                sourceArea.activeFocus, root.imeComposing,
                root.viewState.editorCursorLine,
                root.viewState.editorCursorColumn,
                publishCaret)
        }
    }

    Timer {
        id: navigationAckTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (root.syncController && root.pendingNavigationSequence > 0)
                root.syncController.acknowledgeNavigation(
                    root.pendingNavigationSequence, root.pendingNavigationApplied)
            root.pendingNavigationSequence = 0
            root.pendingNavigationApplied = false
        }
    }

    function beginProgrammaticSelection() {
        root.contextCaretPending = false
        editorContextTimer.stop()
        ++root.programmaticSelectionDepth
    }

    function endProgrammaticSelection() {
        root.programmaticSelectionDepth = Math.max(0, root.programmaticSelectionDepth - 1)
        root.scheduleEditorContext(false)
    }

    // Which view's undo history the editor is looking at. The controller keeps
    // one per view, so switching difficulty changes the name and disturbs
    // nothing — a history is only discarded when its tab closes or the whole
    // document is replaced.
    //
    // A function, not a bound property, and this is the whole point. The
    // session updates its state and then emits, in order: chartTextChanged
    // first, currentDifficultyChanged after. A binding on currentDifficultyId
    // is therefore still holding the OUTGOING difficulty at the moment the
    // incoming text arrives — so naming the scope from a binding named the
    // difficulty being left, and every edit typed afterwards was recorded into
    // its history. Ctrl+Z in one difficulty then replayed another's edits.
    // Reading the property directly gets the value that is already correct.
    function currentHistoryScopeId() {
        return root.metadataMode
            ? "metadata"
            : "difficulty:" + root.documentSession.currentDifficultyId
    }

    function syncTextFromController() {
        const controllerText = root.metadataMode
            ? root.documentSession.metadataSourceText
            : root.documentSession.chartText
        // Named before the text moves: the swap below must not be able to land
        // a recording in the outgoing view's history.
        root.editorController.setHistoryScope(root.currentHistoryScopeId())
        if (sourceArea.text !== controllerText) {
            root.editorController.closeCompletion()
            root.beginProgrammaticSelection()
            sourceArea.syncingFromController = true
            sourceArea.text = controllerText
            sourceArea.syncingFromController = false
            root.endProgrammaticSelection()
            sourceArea.historyText = controllerText
            sourceArea.historyAnchor = sourceArea.selectionStart
            sourceArea.historyPosition = sourceArea.selectionEnd
            updateCursorPosition()
        }
    }
    readonly property real codeLineHeight: sourceArea.cursorRectangle.height
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

    Timer {
        id: cursorCenterTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (!root.documentSession || root.metadataMode || !sourceArea)
                return
            const flickable = editorScroll
            if (!flickable)
                return
            const target = sourceArea.y + sourceArea.cursorRectangle.y
                + sourceArea.cursorRectangle.height / 2 - flickable.height / 2
            flickable.contentY = Math.max(0, Math.min(
                Math.max(0, flickable.contentHeight - flickable.height), target))
        }
    }

    function centerCursorInView() {
        cursorCenterTimer.restart()
    }

    function applyNavigation(sequence, difficultyId, revision, start, end, focusEditor, reveal) {
        const accepted = root.navigationVisible && !root.metadataMode
            && difficultyId === root.documentSession.currentDifficultyId
            && revision === root.documentSession.documentRevision
            && start >= 0 && end >= start && end <= sourceArea.text.length
        if (accepted) {
            root.beginProgrammaticSelection()
            sourceArea.select(start, end)
            if (focusEditor)
                sourceArea.forceActiveFocus()
            root.endProgrammaticSelection()
            if (reveal)
                centerCursorInView()
        }
        root.pendingNavigationSequence = sequence
        root.pendingNavigationApplied = accepted
        navigationAckTimer.restart()
    }

    function createBookmarkAtLine(line) {
        return applyEditorTransaction(editorController.createBookmarkForQml(
            sourceArea.text, line, UiText.text("书签")))
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

    function applyFollowProjection() {
        if (!root.syncController || !root.syncController.followActive || root.metadataMode
                || root.syncController.followDifficultyId
                   !== root.documentSession.currentDifficultyId
                || root.syncController.followRevision
                   !== root.documentSession.documentRevision) {
            root.followDecorationActive = false
            return false
        }
        root.followDecorationStart = Math.max(
            0, Math.min(sourceArea.text.length, root.syncController.followStart))
        root.followDecorationEnd = Math.max(
            root.followDecorationStart,
            Math.min(sourceArea.text.length, root.syncController.followEnd))
        root.followDecorationCursor = Math.max(
            0, Math.min(sourceArea.text.length, root.syncController.followCaret))
        root.followDecorationActive = true
        if (root.syncController.followReveal)
            root.ensureFollowDecorationVisible()
        return true
    }

    // Scrolls the decoration into view without touching the caret or selection,
    // which is what separates paused follow from the playing caret-move path.
    Timer {
        id: decorationCenterTimer
        interval: 0
        repeat: false
        onTriggered: {
            if (!root.followDecorationActive || !sourceArea)
                return
            const flickable = editorScroll
            if (!flickable)
                return
            const rect = sourceArea.positionToRectangle(root.followDecorationCursor)
            const top = sourceArea.y + rect.y
            const bottom = top + rect.height
            if (top >= flickable.contentY && bottom <= flickable.contentY + flickable.height)
                return
            flickable.contentY = Math.max(0, Math.min(
                Math.max(0, flickable.contentHeight - flickable.height),
                top + rect.height / 2 - flickable.height / 2))
        }
    }

    function ensureFollowDecorationVisible() {
        decorationCenterTimer.restart()
    }

    function collectBookmarks() {
        return root.editorController.bookmarksForQml(sourceArea.text)
    }

    // v1 resolves a Ctrl/Command click through an event filter on the hidden
    // widget viewport, so in v2 the click only ever moved the timeline cursor
    // and the preview stayed where it was. TextArea has already placed the
    // caret by the time the click completes, so the caret is the location —
    // no separate hit-test, and it matches what the user sees.
    function seekPreviewToCaret() {
        if (root.metadataMode || sourceArea.selectedText.length > 0)
            return false
        return root.syncController.seekPreviewToEditorLocation(
            root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
            root.viewState.editorCursorLine, root.viewState.editorCursorColumn)
    }

    function updateCursorPosition() {
        const text = sourceArea.text
        const pos = Math.max(0, Math.min(text.length, sourceArea.cursorPosition))
        const before = text.substring(0, pos)
        const lines = before.split("\n")
        root.viewState.editorCursorLine = lines.length
        root.viewState.editorCursorColumn = lines[lines.length - 1].length + 1
    }

    // Human-readable summary of what normalize will act on.
    function selectionDescription() {
        if (sourceArea.selectionStart === sourceArea.selectionEnd)
            return UiText.text("将规范化整份谱面正文。")
        const startLine = sourceArea.text.substring(0, sourceArea.selectionStart).split("\n").length
        const endLine = sourceArea.text.substring(0, sourceArea.selectionEnd).split("\n").length
        return UiText.text("将规范化选中的第 %1 - %2 行。").arg(startLine).arg(endLine)
    }

    function applyNormalization(options) {
        const transaction = root.documentSession.normalizeChartSelection(
            sourceArea.text, sourceArea.selectionStart, sourceArea.selectionEnd, options)
        if (!transaction.consumed || !transaction.hasEdit)
            return false
        return root.applyEditorTransaction(transaction, false)
    }

    // 谱面变换 uses the same transaction path as normalization, so a mirror or a
    // subdivision step lands on the undo stack as one step and the selection
    // survives it. Returns false when there is nothing selected to act on.
    function applyChartTransform(opId) {
        if (root.metadataMode)
            return false
        const transaction = root.documentSession.transformChartSelection(
            sourceArea.text, sourceArea.selectionStart, sourceArea.selectionEnd, opId)
        if (!transaction.consumed || !transaction.hasEdit)
            return false
        return root.applyEditorTransaction(transaction, false)
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
        id: lineNumberGutter
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        lineCount: sourceArea.lineCount
        activeLine: root.activeLine
        contentY: editorScroll.contentY
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
        title: UiText.text("重命名书签")
        footer: DialogFooter {
            acceptText: UiText.text("确定")
            cancelText: UiText.text("取消")
            onAccepted: bookmarkTitleDialog.accept()
            onRejected: bookmarkTitleDialog.reject()
        }
        onAccepted: root.renameBookmarkAtLine(root.pendingBookmarkLine, bookmarkTitleField.text)
        AppTextField {
            id: bookmarkTitleField
            width: 260
            Accessible.name: UiText.text("书签名称")
        }
    }

    AppMenu {
        id: editorContextMenu
        objectName: "editorContextMenu"
        parent: Overlay.overlay

        readonly property var transformRows: root.documentSession.chartTransformMenu()
        // sourceArea keeps its selection while the popup holds focus
        // (persistentSelection), so this can bind live like 剪切 / 复制 above.
        readonly property bool hasSelection:
            sourceArea.selectedText.length > 0 && !root.metadataMode

        AppMenuItem {
            text: UiText.text("剪切")
            enabled: sourceArea.selectedText.length > 0
            onTriggered: sourceArea.cut()
        }
        AppMenuItem {
            text: UiText.text("复制")
            enabled: sourceArea.selectedText.length > 0
            onTriggered: sourceArea.copy()
        }
        AppMenuItem {
            text: UiText.text("粘贴")
            onTriggered: root.applyPastePayload(root.editorController.clipboardText())
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: UiText.text("全选")
            onTriggered: root.selectAll()
        }
        AppMenuItem {
            text: UiText.text("查找与替换")
            onTriggered: root.openFindReplace()
        }
        AppMenuSeparator {}

        // Same rows, labels and grouping as the menubar's 调整 menu — both read
        // documentSession.chartTransformMenu(). Every one of them edits the
        // selection, so they are disabled without one.
        Repeater {
            model: editorContextMenu.transformRows.filter(row => row.section === 0)
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.label
                enabled: editorContextMenu.hasSelection
                onTriggered: root.applyChartTransform(modelData.id)
            }
        }
        AppMenuSeparator {}
        Repeater {
            model: editorContextMenu.transformRows.filter(row => row.section === 1)
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.label
                enabled: editorContextMenu.hasSelection
                onTriggered: root.applyChartTransform(modelData.id)
            }
        }
        AppMenuSeparator {}
        Repeater {
            model: editorContextMenu.transformRows.filter(row => row.section === 2)
            delegate: AppMenuItem {
                required property var modelData
                text: modelData.label
                enabled: editorContextMenu.hasSelection
                onTriggered: root.applyChartTransform(modelData.id)
            }
        }
        AppMenu {
            title: root.documentSession.chartTransformMoreLabel()
            enabled: editorContextMenu.hasSelection
            Repeater {
                model: editorContextMenu.transformRows.filter(row => row.section === 3)
                delegate: AppMenuItem {
                    required property var modelData
                    text: modelData.label
                    onTriggered: root.applyChartTransform(modelData.id)
                }
            }
        }
    }

    // Both context-menu routes end here: a right-click passes the hit point,
    // and the keyboard route (Menu key / Shift+F10) passes the caret.
    function openContextMenuAt(x, y) {
        sourceArea.forceActiveFocus()
        const point = sourceArea.mapToItem(editorContextMenu.parent, x, y)
        editorContextMenu.popup(point.x, point.y)
    }

    function openContextMenuAtCaret() {
        const caret = sourceArea.cursorRectangle
        openContextMenuAt(caret.x, caret.y + caret.height)
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

    Flickable {
        id: editorScroll
        anchors.left: parent.left
        anchors.leftMargin: lineNumberGutter.width
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        clip: true
        flickableDirection: Flickable.VerticalFlick
        ScrollBar.vertical: ScrollBar {}
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

        TextArea.flickable: TextArea {
            id: sourceArea
            objectName: "sourceArea"
            property bool syncingFromController: false
            property bool readyForUserEdits: false
            property string historyText: ""
            property int historyAnchor: 0
            property int historyPosition: 0

            wrapMode: TextArea.Wrap
            padding: 12
            color: Theme.colors.text.editor
            selectedTextColor: Theme.colors.text.editor
            selectionColor: Theme.colors.state.textSelection
            inputMethodHints: root.editorController.halfWidthInputEnabled
                ? Qt.ImhLatinOnly : Qt.ImhNone
            persistentSelection: true
            selectByMouse: true
            font: Theme.codeFont

            background: Item {
                Rectangle {
                    anchors.fill: parent
                    color: Theme.colors.background.editor
                }
                // Preview follow decoration: the playhead's token span. Drawn in
                // the background so it sits under the glyphs and never competes
                // with the real selection.
                Rectangle {
                    readonly property rect startRect: sourceArea.positionToRectangle(root.followDecorationStart)
                    readonly property rect endRect: sourceArea.positionToRectangle(root.followDecorationEnd)
                    visible: root.followDecorationActive && root.followDecorationEnd > root.followDecorationStart
                    x: startRect.x
                    y: startRect.y - editorScroll.contentY
                    // A span that wraps or crosses lines falls back to the rest of
                    // the first line rather than painting a misleading rectangle.
                    width: endRect.y > startRect.y
                        ? Math.max(0, sourceArea.width - sourceArea.rightPadding - startRect.x)
                        : Math.max(0, endRect.x - startRect.x)
                    height: Math.max(startRect.height, root.codeLineHeight)
                    color: Theme.colors.state.textSelection
                    opacity: 0.45
                }
            }

            Rectangle {
                x: sourceArea.leftPadding
                y: Math.max(0, sourceArea.cursorRectangle.y)
                width: sourceArea.width - sourceArea.leftPadding - sourceArea.rightPadding
                height: root.codeLineHeight
                color: Theme.colors.state.lineHighlight
                z: -1
            }

            // Preview follow caret. Distinct from the real caret so a paused
            // seek is visible without stealing the cursor.
            Rectangle {
                readonly property rect caretRect: sourceArea.positionToRectangle(root.followDecorationCursor)
                visible: root.followDecorationActive
                    && (root.syncController.followPlaybackActive || !sourceArea.activeFocus)
                x: caretRect.x
                y: caretRect.y
                width: 2
                height: Math.max(caretRect.height, root.codeLineHeight)
                color: Theme.colors.accent.primary
            }
            onTextChanged: {
                editorInputBridge.applyBlockSpacing()
                // Rehighlighting is a formatting pass over the same characters,
                // but TextEdit still reports it as textChanged. Writing that
                // back would push an identical document through the backend on
                // every highlight, so only a real change is published.
                if (readyForUserEdits && !syncingFromController && text !== historyText) {
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
                root.scheduleEditorContext(root.programmaticSelectionDepth === 0)
                if (!syncingFromController)
                    root.editorController.updateCompletionForQml(text, cursorPosition)
            }
            onActiveFocusChanged: {
                root.scheduleEditorContext(false)
                root.Window.window.sourceEditorFocused = activeFocus
                if (!activeFocus && !completionPopup.pointerInside)
                    root.editorController.closeCompletion()
            }
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
                    root.syncController.setTouchPadControlHold(true)
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
                if (event.key === Qt.Key_Menu
                        || (event.key === Qt.Key_F10 && (event.modifiers & Qt.ShiftModifier))) {
                    root.openContextMenuAtCaret()
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
                if (root.applyEditorTransaction(transaction)) {
                    event.accepted = true
                    return
                }
                // The policy refused this key, but TextArea would still insert
                // its literal character (Qt guards Ctrl, not Meta/Super), so a
                // macOS 物理 Control+Z types "z" into the chart. Swallow it.
                if (transaction.suppressFallbackInsert)
                    event.accepted = true
            }
            Keys.onReleased: function(event) {
                if (event.key === Qt.Key_Control)
                    root.syncController.setTouchPadControlHold(false)
            }
            Component.onCompleted: {
                root.syncTextFromController()
                readyForUserEdits = true
                historyText = text
                historyAnchor = selectionStart
                historyPosition = selectionEnd
                root.editorController.setDocumentContextForQml(
                    root.documentSession.currentDifficultyId, root.documentSession.documentRevision)
                root.updateCursorPosition()
                editorScroll.refreshLineTops()
            }

            EditorTextStyle {
                textDocument: sourceArea.textDocument
                blockSpacing: Theme.codeBlockSpacing
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
                id: editorCaret
                width: 1
                color: Theme.colors.text.editor
                visible: sourceArea.activeFocus && !sourceArea.readOnly
                    && sourceArea.selectionStart === sourceArea.selectionEnd
                    && !root.syncController.followPlaybackActive

                Connections {
                    target: sourceArea
                    function onCursorPositionChanged() {
                        editorCaret.opacity = 1
                        caretBlinkTimer.restart()
                    }
                }

                Timer {
                    id: caretBlinkTimer
                    running: editorCaret.visible && interval > 0
                    repeat: true
                    interval: Application.styleHints.cursorFlashTime / 2
                    onTriggered: editorCaret.opacity = editorCaret.opacity > 0 ? 0 : 1
                    onRunningChanged: editorCaret.opacity = 1
                }
            }

            CompletionPopup {
                id: completionPopup
                editor: sourceArea
                controller: root.editorController
                // The popup lives in the window overlay, so it cannot observe
                // the editor scrolling underneath it on its own.
                editorScrollY: editorScroll.contentY
            }

            QmlEditorInputBridge {
                id: editorInputBridge
                target: sourceArea
                imeInputDisabled: root.editorController.imeInputDisabled
                textDocument: sourceArea.textDocument
                blockSpacing: Theme.codeBlockSpacing
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

            // PointHandler only ever takes a passive grab, so TextArea still
            // receives the press and places the caret; a TapHandler would have
            // to take the exclusive grab TextArea already owns and never fires
            // inside one. A Ctrl+drag is a selection, not a jump.
            PointHandler {
                acceptedButtons: Qt.LeftButton
                target: null
                onActiveChanged: {
                    if (active && !root.metadataMode)
                        root.syncController.beginPointerInteraction(
                            root.documentSession.currentDifficultyId,
                            root.documentSession.documentRevision)
                }
            }

            PointHandler {
                acceptedButtons: Qt.LeftButton
                acceptedModifiers: Qt.ControlModifier
                target: null
                onActiveChanged: {
                    // TextArea finishes placing the caret before the release
                    // dispatch, so the seek uses the final location.
                    if (!active)
                        Qt.callLater(() => root.seekPreviewToCaret())
                }
            }

            // TextArea takes the mouse grab on press, so a TapHandler's
            // release-within-bounds gesture never completes inside one and the
            // context menu never opened. A right-button MouseArea does get the
            // click, and leaves left-button selection to TextArea untouched —
            // the same pattern the line-number gutter menu already uses.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.RightButton
                scrollGestureEnabled: false
                cursorShape: Qt.IBeamCursor
                onClicked: mouse => root.openContextMenuAt(mouse.x, mouse.y)
            }
        }
    }

    Connections {
        target: root.viewState
        function onEditorClosed(key) { root.editorController.dropHistoryScope(key) }
    }

    Connections {
        target: root.documentSession
        function onChartTextChanged() {
            if (!root.metadataMode)
                root.syncTextFromController()
            root.documentSession.logEditorDocumentState(
                "chart_text_changed", root.documentSession.currentDifficultyId,
                root.documentSession.documentRevision, sourceArea.text.length, root.metadataMode)
        }
        function onMetadataSourceChanged() {
            if (root.metadataMode)
                root.syncTextFromController()
        }
        function onDocumentReplaced() {
            // A different chart is a different history, even when it happens to
            // reuse the outgoing document's difficulty ids.
            root.editorController.clearAllHistory()
            root.syncTextFromController()
            root.documentSession.logEditorDocumentState(
                "document_replaced", root.documentSession.currentDifficultyId,
                root.documentSession.documentRevision, sourceArea.text.length, root.metadataMode)
        }
        function onDocumentStateChanged() {
            // Every commit re-asserts the scope. syncTextFromController already
            // names it on the paths that move text; this covers the ones that
            // change which difficulty is active without changing any text.
            root.editorController.setHistoryScope(root.currentHistoryScopeId())
            root.editorController.setDocumentContextForQml(
                root.documentSession.currentDifficultyId, root.documentSession.documentRevision)
            root.publishNavigationReadiness()
            root.scheduleEditorContext(false)
            root.applyFollowProjection()
        }
    }

    Connections {
        target: root.syncController
        function onNavigationRequested(sequence, difficultyId, revision, start, end,
                                       focusEditor, reveal) {
            root.applyNavigation(sequence, difficultyId, revision, start, end,
                                 focusEditor, reveal)
        }
        function onFollowChanged() {
            root.applyFollowProjection()
        }
        function onTouchPadAuthoringRequested(pad, useBacktickSeparator, difficultyId,
                                               revision, anchor, position) {
            if (root.metadataMode || !sourceArea.activeFocus || root.imeComposing
                    || difficultyId !== root.documentSession.currentDifficultyId
                    || revision !== root.documentSession.documentRevision)
                return
            root.beginProgrammaticSelection()
            sourceArea.select(anchor, position)
            root.endProgrammaticSelection()
            const tx = root.editorController.touchPadAuthoringForQml(
                sourceArea.text, anchor, position, pad, useBacktickSeparator)
            if (root.applyEditorTransaction(tx)) {
                root.syncController.setTouchPadPreviewAnchor(
                    difficultyId, root.documentSession.documentRevision,
                    sourceArea.text, tx.touchTokenStart)
            }
        }
    }

    Component.onCompleted: {
        publishNavigationReadiness()
        scheduleEditorContext(false)
        applyFollowProjection()
    }
    Component.onDestruction: {
        if (root.syncController)
            root.syncController.setEditorReadiness(-1, 0, false, true)
    }

    onImeComposingChanged: {
        root.scheduleEditorContext(false)
        if (root.imeComposing)
            root.syncController.setTouchPadControlHold(false)
    }
}
