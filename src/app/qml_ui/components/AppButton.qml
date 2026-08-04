import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared push button — geometry mirrors v1 dialogPushButtonStyleSheet.
Button {
    id: root

    // true = accent fill (primary action); false = outlined secondary.
    property bool emphasized: false

    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    font.weight: Font.Medium
    leftPadding: 12
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    implicitHeight: Theme.controlMinHeight
    implicitWidth: Math.max(92, contentItem.implicitWidth + leftPadding + rightPadding)
    hoverEnabled: true

    contentItem: Text {
        text: root.text
        font: root.font
        color: {
            if (!root.enabled)
                return Theme.colors.text.disabled
            if (root.emphasized)
                return Theme.colors.text.onAccent
            return Theme.colors.text.primary
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: Theme.controlRadius
        border.width: Theme.controlBorderWidth
        border.color: {
            if (!root.enabled)
                return Theme.colors.border.normal
            if (root.emphasized || root.down || root.hovered)
                return Theme.colors.accent.primary
            return Theme.colors.border.control
        }
        color: {
            if (!root.enabled)
                return Theme.colors.background.elevated
            if (root.emphasized) {
                if (root.down)
                    return Theme.colors.accent.badge
                if (root.hovered)
                    return Theme.colors.accent.badge
                return Theme.colors.accent.primary
            }
            if (root.down)
                return Theme.colors.state.pressed
            if (root.hovered)
                return Theme.colors.state.hover
            return Theme.colors.background.elevated
        }
    }
}
