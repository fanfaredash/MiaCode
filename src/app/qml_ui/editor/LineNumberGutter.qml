pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property int lineCount
    required property int activeLine
    required property real contentY
    // 每个逻辑行顶部在文档坐标系中的 y，与 lineCount 等长。
    // 自动换行后行高不固定，行号按真实行顶绘制。
    required property var lineTops
    property var bookmarkedLines: []
    property real rowHeight: 23.1
    property real topPadding: 12
    signal jumpRequested(int line)
    signal createRequested(int line)
    signal deleteRequested(int line)
    signal renameRequested(int line)

    Accessible.role: Accessible.List
    Accessible.name: bookmarked(activeLine)
        ? UiText.text("书签与行号：第 %1 行有书签").arg(activeLine)
        : UiText.text("书签与行号：第 %1 行无书签").arg(activeLine)
    Accessible.description: UiText.text("Enter 跳转；Ctrl+Shift+B 创建；Delete 删除；F2 重命名；右键打开书签菜单")
    activeFocusOnTab: true
    focus: false

    width: 36
    color: Theme.surfaceColor("codeEditor", Theme.colors.background.editor)
    clip: true

    FontMetrics {
        id: codeFontMetrics
        font: Theme.codeFont
    }

    // 行号用 Canvas 只绘制可见行，避免大谱面下为每一行常驻一个 Text
    // 组件导致滚动和光标移动时全量重新布局。
    Canvas {
        id: gutterCanvas
        anchors.fill: parent

        function refresh() {
            requestPaint()
        }

        onPaint: {
            const ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            const tops = root.lineTops
            if (root.rowHeight <= 0 || root.lineCount <= 0) {
                return
            }

            // lineTops 尚未发布（如首帧）时按固定行高兜底，避免行号区空白。
            if (tops.length === 0) {
                const font = Theme.codeFont
                ctx.font = font.pixelSize + "px \"" + font.family + "\""
                ctx.textAlign = "right"
                ctx.textBaseline = "alphabetic"
                const firstLine = Math.max(0, Math.floor((root.contentY - root.topPadding) / root.rowHeight))
                const lastLine = Math.min(
                    root.lineCount - 1,
                    Math.ceil((root.contentY + height - root.topPadding) / root.rowHeight))
                for (let line = firstLine; line <= lastLine; ++line) {
                    ctx.fillStyle = line + 1 === root.activeLine
                        ? Theme.colors.text.editor
                        : Theme.colors.text.lineNumber
                    ctx.fillText(
                        String(line + 1),
                        width - 6,
                        root.topPadding + line * root.rowHeight - root.contentY
                            + codeFontMetrics.ascent)
                    if (root.bookmarkedLines.some(item => item.line === line + 1)) {
                        ctx.fillStyle = Theme.colors.accent.primary
                        ctx.fillRect(3, root.topPadding + line * root.rowHeight - root.contentY + 5,
                                     3, root.rowHeight - 10)
                    }
                }
                return
            }

            // lineTops 单调递增，二分找到首尾可见行，避免大谱面下全量遍历。
            const viewTop = root.contentY - root.topPadding
            const viewBottom = root.contentY + height - root.topPadding
            let firstLine = 0
            let low = 0
            let high = tops.length - 1
            while (low <= high) {
                const mid = (low + high) >> 1
                if (tops[mid] < viewTop) {
                    firstLine = mid + 1
                    low = mid + 1
                } else {
                    high = mid - 1
                }
            }
            let lastLine = tops.length - 1
            low = firstLine
            high = tops.length - 1
            while (low <= high) {
                const mid = (low + high) >> 1
                if (tops[mid] <= viewBottom) {
                    lastLine = mid
                    low = mid + 1
                } else {
                    high = mid - 1
                }
            }
            const font = Theme.codeFont
            ctx.font = font.pixelSize + "px \"" + font.family + "\""
            ctx.textAlign = "right"
            ctx.textBaseline = "alphabetic"

            for (let line = firstLine; line <= lastLine; ++line) {
                const lineTop = tops.length > 0 ? tops[line] : line * root.rowHeight
                ctx.fillStyle = line + 1 === root.activeLine
                    ? Theme.colors.text.editor
                    : Theme.colors.text.lineNumber
                ctx.fillText(
                    String(line + 1),
                    width - 6,
                    root.topPadding + lineTop - root.contentY + codeFontMetrics.ascent)
                if (root.bookmarkedLines.some(item => item.line === line + 1)) {
                    ctx.fillStyle = Theme.colors.accent.primary
                    ctx.fillRect(3, root.topPadding + lineTop - root.contentY + 5,
                                 3, root.rowHeight - 10)
                }
            }
        }
    }

    onLineCountChanged: gutterCanvas.refresh()
    onLineTopsChanged: gutterCanvas.refresh()
    onActiveLineChanged: gutterCanvas.refresh()
    onContentYChanged: gutterCanvas.refresh()
    onRowHeightChanged: gutterCanvas.refresh()
    onTopPaddingChanged: gutterCanvas.refresh()
    onBookmarkedLinesChanged: gutterCanvas.refresh()

    function lineAt(y) {
        const documentY = y + contentY - topPadding
        const tops = lineTops
        if (tops.length === 0)
            return Math.max(1, Math.min(lineCount, Math.floor(documentY / rowHeight) + 1))
        let line = 0
        for (let i = 0; i < tops.length; ++i) {
            if (tops[i] > documentY)
                break
            line = i
        }
        return Math.max(1, Math.min(lineCount, line + 1))
    }

    function bookmarked(line) {
        return bookmarkedLines.some(item => item.line === line)
    }

    Keys.onPressed: event => {
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
            jumpRequested(activeLine)
            event.accepted = true
        } else if (event.key === Qt.Key_Delete && bookmarked(activeLine)) {
            deleteRequested(activeLine)
            event.accepted = true
        } else if (event.key === Qt.Key_F2 && bookmarked(activeLine)) {
            renameRequested(activeLine)
            event.accepted = true
        } else if ((event.modifiers & Qt.ControlModifier) && (event.modifiers & Qt.ShiftModifier)
                   && event.key === Qt.Key_B) {
            createRequested(activeLine)
            event.accepted = true
        }
    }

    Menu {
        id: bookmarkMenu
        property int line: 1
        MenuItem {
            text: UiText.text("跳转到此行")
            onTriggered: root.jumpRequested(bookmarkMenu.line)
        }
        MenuItem {
            text: root.bookmarked(bookmarkMenu.line) ? UiText.text("删除书签") : UiText.text("创建书签")
            onTriggered: root.bookmarked(bookmarkMenu.line)
                         ? root.deleteRequested(bookmarkMenu.line)
                         : root.createRequested(bookmarkMenu.line)
        }
        MenuItem {
            text: UiText.text("重命名书签")
            enabled: root.bookmarked(bookmarkMenu.line)
            onTriggered: root.renameRequested(bookmarkMenu.line)
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onClicked: mouse => {
            const line = root.lineAt(mouse.y)
            if (mouse.button === Qt.RightButton) {
                bookmarkMenu.line = line
                bookmarkMenu.popup()
            } else {
                root.jumpRequested(line)
            }
        }
    }
}
