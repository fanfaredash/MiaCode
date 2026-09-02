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

    // 只为可见行建场景图文字，和正文同一套变换；整列贴图在窗口插值里会拉伸。
    readonly property int firstVisibleLine: {
        const tops = root.lineTops
        const viewTop = root.contentY - root.topPadding
        if (root.lineCount <= 0)
            return 0
        if (tops.length === 0) {
            const h = Math.max(root.rowHeight, 1)
            return Math.max(0, Math.floor(viewTop / h))
        }
        let first = 0
        let low = 0
        let high = tops.length - 1
        while (low <= high) {
            const mid = (low + high) >> 1
            if (tops[mid] < viewTop) {
                first = mid + 1
                low = mid + 1
            } else {
                high = mid - 1
            }
        }
        return first
    }
    readonly property int lastVisibleLine: {
        const tops = root.lineTops
        const viewBottom = root.contentY + height - root.topPadding
        const lastIndex = root.lineCount - 1
        if (lastIndex < 0)
            return -1
        if (tops.length === 0) {
            const h = Math.max(root.rowHeight, 1)
            return Math.min(lastIndex, Math.ceil(viewBottom / h))
        }
        let last = tops.length - 1
        let low = root.firstVisibleLine
        let high = tops.length - 1
        while (low <= high) {
            const mid = (low + high) >> 1
            if (tops[mid] <= viewBottom) {
                last = mid
                low = mid + 1
            } else {
                high = mid - 1
            }
        }
        return Math.min(last, lastIndex)
    }

    Repeater {
        model: root.lineCount <= 0
            ? 0
            : Math.max(0, root.lastVisibleLine - root.firstVisibleLine + 1)
        delegate: Item {
            required property int index
            readonly property int line: root.firstVisibleLine + index
            readonly property real lineTop: {
                const tops = root.lineTops
                return line < tops.length ? tops[line] : line * root.rowHeight
            }
            readonly property real lineHeight: {
                const tops = root.lineTops
                if (line + 1 < tops.length)
                    return tops[line + 1] - lineTop
                return root.rowHeight
            }
            x: 0
            y: root.topPadding + lineTop - root.contentY
            width: root.width
            height: lineHeight
            Text {
                anchors.right: parent.right
                anchors.rightMargin: 6
                anchors.top: parent.top
                text: String(line + 1)
                font: Theme.codeFont
                color: line + 1 === root.activeLine
                    ? Theme.colors.text.editor
                    : Theme.colors.text.lineNumber
            }
            Rectangle {
                visible: {
                    const marks = root.bookmarkedLines
                    const n = line + 1
                    return marks.some(item => item.line === n)
                }
                x: 3
                y: 5
                width: 3
                height: Math.max(0, parent.height - 10)
                color: Theme.colors.accent.primary
            }
        }
    }

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
