import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared push button — geometry mirrors v1 dialogPushButtonStyleSheet.
Button {
    id: root

    // Primary actions use an accent fill and a stronger font weight.
    property bool emphasized: false
    property bool selected: false

    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    font.weight: root.emphasized ? Font.DemiBold : Font.Normal
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

    background: HoverChrome {
        baseColor: root.enabled && root.emphasized
                   ? Theme.colors.accent.primary : Theme.colors.background.elevated
        stateColors: root.emphasized ? Theme.colors.accentState : Theme.colors.buttonState
        selected: root.selected || root.checked
        hovered: root.hovered
        pressed: root.down
        focused: root.visualFocus
    }
}
