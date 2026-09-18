import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared slider — geometry mirrors v1 formSliderStyleSheet.
// Control 会把 background 拉到整颗滑条的尺寸，轨道必须画在内层，不能写在
// background 根上，否则轨道会变成整块色条。
Slider {
    id: root

    hoverEnabled: true
    implicitHeight: 24
    padding: 0

    background: Item {
        implicitWidth: 200
        implicitHeight: 24

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 6
            radius: 3
            color: Theme.overlayColor(Theme.colors.border.control)

            Rectangle {
                width: root.visualPosition * parent.width
                height: parent.height
                radius: 3
                color: Theme.colors.accent.primary
            }
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        implicitWidth: 14
        implicitHeight: 14
        width: 14
        height: 14
        radius: 7
        color: Theme.overlayColor(Theme.colors.background.elevated)
        border.width: Theme.controlBorderWidth
        border.color: (root.pressed || root.hovered)
                      ? Theme.colors.accent.primary
                      : Theme.colors.border.control
    }
}
