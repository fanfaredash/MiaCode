import QtQuick

// 鼠标选择的完整入口。TextArea 继续负责文字、键盘选择和光标绘制。
MouseArea {
    id: root
    required property var editor
    required property var viewport
    required property var inputBridge

    signal interactionStarted()
    signal contextMenuRequested(real x, real y)
    signal seekRequested()

    property bool selecting: false
    property point pressPoint: Qt.point(0, 0)
    property point viewportPoint: Qt.point(0, 0)
    property bool selectingWords: false
    property bool draggingSelection: false
    property int anchorStart: 0
    property int anchorEnd: 0

    acceptedButtons: Qt.LeftButton | Qt.RightButton
    preventStealing: true
    scrollGestureEnabled: false
    cursorShape: Qt.IBeamCursor

    function positionAt(x, y) {
        const hit = inputBridge.textHitPoint(x, y)
        return editor.positionAt(hit.x, hit.y)
    }

    function extendSelection() {
        const point = viewport.mapToItem(root, viewportPoint.x, viewportPoint.y)
        const position = positionAt(point.x, point.y)
        if (selectingWords) {
            const word = inputBridge.wordRange(position)
            if (position < anchorStart)
                editor.select(anchorEnd, word.start)
            else if (position > anchorEnd)
                editor.select(anchorStart, word.end)
            else
                editor.select(anchorStart, anchorEnd)
        } else {
            editor.select(anchorStart, position)
        }
    }

    onPressed: mouse => {
        selecting = mouse.button === Qt.LeftButton
        interactionStarted()
        editor.forceActiveFocus()
        Qt.inputMethod.commit()
        pressPoint = mapToItem(viewport, mouse.x, mouse.y)
        viewportPoint = pressPoint
        selectingWords = false
        draggingSelection = false
        const position = positionAt(mouse.x, mouse.y)
        if (mouse.button === Qt.RightButton) {
            if (position < editor.selectionStart || position >= editor.selectionEnd)
                editor.cursorPosition = position
            return
        }
        anchorStart = (mouse.modifiers & Qt.ShiftModifier)
            ? (editor.cursorPosition === editor.selectionStart ? editor.selectionEnd : editor.selectionStart)
            : position
        anchorEnd = anchorStart
        editor.select(anchorStart, position)
    }

    onDoubleClicked: mouse => {
        if (mouse.button !== Qt.LeftButton || mouse.modifiers !== Qt.NoModifier) {
            mouse.accepted = false
            return
        }
        selecting = true
        pressPoint = mapToItem(viewport, mouse.x, mouse.y)
        viewportPoint = pressPoint
        draggingSelection = false
        const word = inputBridge.wordRange(positionAt(mouse.x, mouse.y))
        anchorStart = word.start
        anchorEnd = word.end
        selectingWords = true
        editor.select(anchorStart, anchorEnd)
    }

    onPositionChanged: mouse => {
        if (!selecting)
            return
        viewportPoint = mapToItem(viewport, mouse.x, mouse.y)
        if (!draggingSelection) {
            const distance = Math.hypot(viewportPoint.x - pressPoint.x, viewportPoint.y - pressPoint.y)
            if (distance < Application.styleHints.startDragDistance)
                return
            draggingSelection = true
        }
        extendSelection()
    }

    onReleased: mouse => {
        if (mouse.button === Qt.LeftButton) {
            if (draggingSelection) {
                viewportPoint = mapToItem(viewport, mouse.x, mouse.y)
                extendSelection()
            }
            selecting = false
            if (!draggingSelection && (mouse.modifiers & Qt.ControlModifier))
                seekRequested()
        }
    }
    onCanceled: selecting = false
    onClicked: mouse => {
        if (mouse.button === Qt.RightButton)
            contextMenuRequested(mouse.x, mouse.y)
    }

    // 指针采用视口坐标保存，滚动后重新命中文字；选区始终跟随同一屏幕落点。
    Timer {
        interval: 16
        repeat: true
        running: root.selecting && root.draggingSelection
            && ((root.viewportPoint.y < 0 && root.viewport.contentY > 0)
                || (root.viewportPoint.y > root.viewport.height
                    && root.viewport.contentY < root.viewport.maximumViewportY))
        onTriggered: {
            const outside = root.viewportPoint.y < 0
                ? root.viewportPoint.y : root.viewportPoint.y - root.viewport.height
            const lineHeight = root.editor.cursorRectangle.height
            const velocity = Math.sign(outside) * Math.max(lineHeight * 4, Math.abs(outside) * 8)
            root.viewport.allowScroll = true
            root.viewport.contentY = root.viewport.clampViewportY(
                root.viewport.contentY + velocity * interval / 1000)
            root.viewport.allowScroll = false
            root.extendSelection()
        }
    }
}
