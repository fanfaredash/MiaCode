import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var workbenchState
    required property var documentSession
    property bool metadataMode: false
    onMetadataModeChanged: syncTextFromController()

    function syncTextFromController() {
        const controllerText = root.metadataMode
            ? root.documentSession.metadataSourceText
            : root.documentSession.chartText
        if (sourceArea.text === controllerText)
            return
        sourceArea.syncingFromController = true
        sourceArea.text = controllerText
        sourceArea.syncingFromController = false
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

    readonly property bool canUndo: sourceArea.canUndo
    readonly property bool canRedo: sourceArea.canRedo

    function undo() {
        sourceArea.undo()
    }

    function redo() {
        sourceArea.redo()
    }

    function selectAll() {
        sourceArea.selectAll()
    }

    function revealSyntaxIssue(line, column, endColumn) {
        if (root.metadataMode)
            return
        const start = root.documentSession.chartPosition(line, column)
        const end = root.documentSession.chartPosition(line, Math.max(column, endColumn + 1))
        sourceArea.forceActiveFocus()
        sourceArea.select(start, Math.max(start + 1, end))
    }

    function updateCursorPosition() {
        const text = sourceArea.text
        const pos = Math.max(0, Math.min(text.length, sourceArea.cursorPosition))
        const before = text.substring(0, pos)
        const lines = before.split("\n")
        root.workbenchState.editorCursorLine = lines.length
        root.workbenchState.editorCursorColumn = lines[lines.length - 1].length + 1
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
                    if (root.metadataMode)
                        root.documentSession.metadataSourceText = text
                    else
                        root.documentSession.chartText = text
                }
                root.updateCursorPosition()
                editorScroll.refreshLineTops()
            }
            onCursorPositionChanged: root.updateCursorPosition()
            Component.onCompleted: {
                root.syncTextFromController()
                readyForUserEdits = true
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
                diagnostics: root.metadataMode ? [] : root.documentSession.syntaxIssues
            }
            // 视口尺寸变化时由 editorScroll.refreshHighlight() 触发重高亮。
            function rehighlight() {
                highlighter.rehighlight()
            }

            cursorDelegate: Rectangle {
                width: 1
                color: Theme.colors.text.editor
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
    }

    Binding {
        target: editorScroll.contentItem
        property: "boundsBehavior"
        value: Flickable.StopAtBounds
    }
}

