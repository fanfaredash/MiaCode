import QtQuick
import QtQuick.Controls as Controls
import MiaCode.UI

Item {
    id: root

    signal released()

    implicitWidth: Theme.splitDividerThickness
    implicitHeight: Theme.splitDividerThickness
    // Paint above both panes so the thickened stroke is visible on each side.
    z: 1
    readonly property bool handlePressed: Controls.SplitHandle.pressed
    readonly property bool handleActive: handlePressed || Controls.SplitHandle.hovered
    readonly property bool verticalDivider: parent !== null
                                            && parent.orientation === Qt.Horizontal
    readonly property real lineThickness: handleActive
                                          ? Theme.splitHandleActiveThickness
                                          : Theme.splitDividerThickness

    containmentMask: Item {
        x: root.verticalDivider ? (root.width - width) / 2 : 0
        y: root.verticalDivider ? 0 : (root.height - height) / 2
        width: root.verticalDivider ? Theme.splitHandleHitExtent : root.width
        height: root.verticalDivider ? root.height : Theme.splitHandleHitExtent
    }

    // HoverHandler 只设置光标，不拦截指针事件，不干扰 SplitView 的拖拽。
    HoverHandler {
        cursorShape: root.verticalDivider ? Qt.SplitHCursor : Qt.SplitVCursor
    }

    Rectangle {
        anchors.centerIn: parent
        width: root.verticalDivider ? root.lineThickness : parent.width
        height: root.verticalDivider ? parent.height : root.lineThickness
        color: root.handleActive
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
