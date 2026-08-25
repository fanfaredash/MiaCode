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
    // Read-only projection of preview follow while paused or with 代码跟随 off.
    // It paints where the playhead is; it must never move the real caret.
    property bool followDecorationActive: false
    property int followDecorationStart: 0
    property int followDecorationEnd: 0
    property int followDecorationCursor: 0
    onMetadataModeChanged: {
        syncTextFromController()
        publishNavigationReadiness()
        root.followDecorationActive = false
    }
    onNavigationVisibleChanged: publishNavigationReadiness()

    function publishNavigationReadiness() {
        if (!root.documentSession)
            return
        root.documentSession.setQmlEditorNavigationReadiness(
            root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
            root.navigationVisible, root.metadataMode)
    }

    // The QML undo stack belongs to one difficulty's source. It is reset when
    // the editor actually changes documents — not on every controller-sourced
    // text sync, which is what silently emptied the user's history whenever the
    // backend pushed normalized text back.
    property int historyDifficultyId: -1
    property bool historyMetadataMode: false
    property bool historyIdentityValid: false

    function syncTextFromController() {
        const controllerText = root.metadataMode
            ? root.documentSession.metadataSourceText
            : root.documentSession.chartText
        const identityChanged = !root.historyIdentityValid
            || root.historyDifficultyId !== root.documentSession.currentDifficultyId
            || root.historyMetadataMode !== root.metadataMode
        if (sourceArea.text !== controllerText) {
            sourceArea.syncingFromController = true
            sourceArea.text = controllerText
            sourceArea.syncingFromController = false
            sourceArea.historyText = controllerText
            sourceArea.historyAnchor = sourceArea.selectionStart
            sourceArea.historyPosition = sourceArea.selectionEnd
            updateCursorPosition()
        }
        if (!identityChanged)
            return
        root.historyDifficultyId = root.documentSession.currentDifficultyId
        root.historyMetadataMode = root.metadataMode
        root.historyIdentityValid = true
        root.editorController.resetQmlHistory(controllerText, sourceArea.historyAnchor,
                                              sourceArea.historyPosition)
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
        // Defence in depth for non-playback callers: the C++ playback bridge
        // suppresses duplicate targets, but direct navigation must not turn an
        // unchanged selection into another TextArea update and center-scroll.
        if (sourceArea.selectionStart === start && sourceArea.selectionEnd === end) {
            if (focusEditor)
                sourceArea.forceActiveFocus()
            if (centerView)
                centerCursorInView()
            return
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

    function applyFollowDecoration(active, difficultyId, revision, startLine, startColumn,
                                   endLine, endColumn, cursorLine, cursorColumn, ensureVisible) {
        if (!active || root.metadataMode
                || difficultyId !== root.documentSession.currentDifficultyId
                || revision !== root.documentSession.documentRevision) {
            root.followDecorationActive = false
            return false
        }
        root.followDecorationStart = root.documentSession.chartPosition(startLine, startColumn)
        // Timeline spans use an inclusive end column; a text offset is exclusive.
        root.followDecorationEnd = Math.max(
            root.followDecorationStart,
            root.documentSession.chartPosition(endLine, endColumn + 1))
        root.followDecorationCursor = root.documentSession.chartPosition(cursorLine, cursorColumn)
        root.followDecorationActive = true
        if (ensureVisible)
            root.ensureFollowDecorationVisible()
        return true
    }

    // Scrolls the decoration into view without touching the caret or selection,
    // which is what separates paused follow from the playing caret-move path.
    function ensureFollowDecorationVisible() {
        Qt.callLater(() => {
            if (!root.followDecorationActive)
                return
            const flickable = editorScroll.contentItem
            const rect = sourceArea.positionToRectangle(root.followDecorationCursor)
            const top = sourceArea.y + rect.y
            const bottom = top + rect.height
            if (top >= flickable.contentY && bottom <= flickable.contentY + flickable.height)
                return
            flickable.contentY = Math.max(0, Math.min(
                Math.max(0, flickable.contentHeight - flickable.height),
                top + rect.height / 2 - flickable.height / 2))
        })
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
        return root.documentSession.seekPreviewToEditorLocation(
            root.documentSession.currentDifficultyId, root.documentSession.documentRevision,
            root.viewState.editorCursorLine, root.viewState.editorCursorColumn)
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

    AppMenu {
        id: editorContextMenu
        objectName: "editorContextMenu"
        parent: Overlay.overlay

        AppMenuItem {
            text: qsTr("剪切")
            enabled: sourceArea.selectedText.length > 0
            onTriggered: sourceArea.cut()
        }
        AppMenuItem {
            text: qsTr("复制")
            enabled: sourceArea.selectedText.length > 0
            onTriggered: sourceArea.copy()
        }
        AppMenuItem {
            text: qsTr("粘贴")
            onTriggered: root.applyPastePayload(root.editorController.clipboardText())
        }
        AppMenuSeparator {}
        AppMenuItem {
            text: qsTr("全选")
            onTriggered: root.selectAll()
        }
        AppMenuItem {
            text: qsTr("查找与替换")
            onTriggered: root.openFindReplace()
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
            objectName: "sourceArea"
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

            // Preview follow decoration: the playhead's token span. Drawn under
            // the text like the current-line highlight so it never competes
            // with the real selection.
            Rectangle {
                readonly property rect startRect: sourceArea.positionToRectangle(root.followDecorationStart)
                readonly property rect endRect: sourceArea.positionToRectangle(root.followDecorationEnd)
                visible: root.followDecorationActive && root.followDecorationEnd > root.followDecorationStart
                x: startRect.x
                y: startRect.y
                // A span that wraps or crosses lines falls back to the rest of
                // the first line rather than painting a misleading rectangle.
                width: endRect.y > startRect.y
                    ? Math.max(0, sourceArea.width - sourceArea.rightPadding - startRect.x)
                    : Math.max(0, endRect.x - startRect.x)
                height: Math.max(startRect.height, root.codeLineHeight)
                color: Theme.colors.state.textSelection
                opacity: 0.45
                z: -1
            }

            // Preview follow caret — the v1 blue line. Distinct from the real
            // caret so a paused seek is visible without stealing the cursor.
            Rectangle {
                readonly property rect caretRect: sourceArea.positionToRectangle(root.followDecorationCursor)
                visible: root.followDecorationActive
                x: caretRect.x
                y: caretRect.y
                width: 2
                height: Math.max(caretRect.height, root.codeLineHeight)
                color: Theme.colors.accent.primary
            }
            onTextChanged: {
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
                // The popup lives in the window overlay, so it cannot observe
                // the editor scrolling underneath it on its own.
                editorScrollY: editorScroll.contentItem.contentY
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

            // PointHandler only ever takes a passive grab, so TextArea still
            // receives the press and places the caret; a TapHandler would have
            // to take the exclusive grab TextArea already owns and never fires
            // inside one. A Ctrl+drag is a selection, not a jump.
            PointHandler {
                acceptedButtons: Qt.LeftButton
                acceptedModifiers: Qt.ControlModifier
                target: null
                onActiveChanged: {
                    // Deferred like v1's release dispatch: TextArea finishes
                    // placing the caret for this click first, so the seek uses
                    // the final location rather than the previous one.
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
                onClicked: mouse => root.openContextMenuAt(mouse.x, mouse.y)
            }
        }
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
            // reuse the outgoing document's active difficulty id.
            root.historyIdentityValid = false
            root.syncTextFromController()
            root.documentSession.logEditorDocumentState(
                "document_replaced", root.documentSession.currentDifficultyId,
                root.documentSession.documentRevision, sourceArea.text.length, root.metadataMode)
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
        function onQmlEditorFollowDecorationChanged(active, difficultyId, revision, startLine,
                                                    startColumn, endLine, endColumn, cursorLine,
                                                    cursorColumn, ensureVisible) {
            root.applyFollowDecoration(active, difficultyId, revision, startLine, startColumn,
                                       endLine, endColumn, cursorLine, cursorColumn, ensureVisible)
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
