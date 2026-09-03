import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Themed switch — geometry/colors aligned with v2 Theme (not stock Fusion).
Switch {
    id: root

    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    hoverEnabled: true

    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: Theme.overlayColor(root.checked ? Theme.colors.accent.primary : Theme.colors.border.control)
        border.width: Theme.controlBorderWidth
        border.color: root.checked ? Theme.colors.accent.primary
                     : (root.hovered ? Theme.colors.border.normal : Theme.colors.border.control)
        opacity: root.enabled ? 1 : 0.45

        Rectangle {
            x: root.checked ? parent.width - width - 2 : 2
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 8
            color: Theme.colors.text.active
            Behavior on x { NumberAnimation { duration: 100 } }
        }
    }

    contentItem: Text {
        text: root.text
        font: root.font
        color: !root.enabled ? Theme.colors.text.disabled
               : root.checked ? Theme.colors.text.active
               : Theme.colors.text.secondary
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
    }
}
