import QtQuick
import QtQuick.Controls as Controls
import MiaCode.UI

Item {
    id: root

    signal released()

    implicitWidth: 4
    implicitHeight: 4
    readonly property bool handlePressed: Controls.SplitHandle.pressed
    readonly property bool verticalDivider: parent !== null
                                            && parent.orientation === Qt.Horizontal

    // 悬停时切换为对应方向的调整光标，让分隔条可定位。
    // HoverHandler 只设置光标，不拦截指针事件，不干扰 SplitView 的拖拽。
    HoverHandler {
        cursorShape: root.verticalDivider ? Qt.SplitHCursor : Qt.SplitVCursor
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width > parent.height ? parent.width : 1
        height: parent.width > parent.height ? 1 : parent.height
        color: root.handlePressed || Controls.SplitHandle.hovered
               ? Theme.colors.accent.focus
               : Theme.colors.border.normal
    }

    property bool wasPressed: false

    onVisibleChanged: wasPressed = false

    onHandlePressedChanged: {
        if (root.wasPressed && !root.handlePressed)
            root.released()
        root.wasPressed = root.handlePressed
    }
}

