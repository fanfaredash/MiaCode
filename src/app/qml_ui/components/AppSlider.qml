import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared slider — geometry mirrors v1 formSliderStyleSheet.
Slider {
    id: root

    hoverEnabled: true

    background: Rectangle {
        x: root.leftPadding
        y: root.topPadding + root.availableHeight / 2 - height / 2
        implicitWidth: 200
        implicitHeight: 6
        width: root.availableWidth
        height: 6
        radius: 3
        color: Theme.colors.border.control

        Rectangle {
            width: root.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: Theme.colors.accent.primary
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
        color: Theme.colors.background.elevated
        border.width: Theme.controlBorderWidth
        border.color: (root.pressed || root.hovered)
                      ? Theme.colors.accent.primary
                      : Theme.colors.border.control
    }
}
