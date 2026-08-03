pragma ComponentBehavior: Bound

import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    required property int lineCount
    required property int activeLine
    required property real contentY
    // 每个逻辑行顶部在文档坐标系中的 y，与 lineCount 等长。
    // 自动换行后行高不固定，行号按真实行顶绘制。
    required property var lineTops
    property real rowHeight: 23.1
    property real topPadding: 12

    width: 46
    color: Theme.colors.background.editor
    clip: true

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
                ctx.textBaseline = "middle"
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
                        root.topPadding + line * root.rowHeight - root.contentY + root.rowHeight / 2)
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
            ctx.textBaseline = "middle"

            for (let line = firstLine; line <= lastLine; ++line) {
                const lineTop = tops.length > 0 ? tops[line] : line * root.rowHeight
                ctx.fillStyle = line + 1 === root.activeLine
                    ? Theme.colors.text.editor
                    : Theme.colors.text.lineNumber
                ctx.fillText(
                    String(line + 1),
                    width - 6,
                    root.topPadding + lineTop - root.contentY + root.rowHeight / 2)
            }
        }
    }

    onLineCountChanged: gutterCanvas.refresh()
    onLineTopsChanged: gutterCanvas.refresh()
    onActiveLineChanged: gutterCanvas.refresh()
    onContentYChanged: gutterCanvas.refresh()
    onRowHeightChanged: gutterCanvas.refresh()
    onTopPaddingChanged: gutterCanvas.refresh()
}

