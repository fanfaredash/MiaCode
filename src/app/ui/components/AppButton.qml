import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// Shared push button — geometry mirrors v1 dialogPushButtonStyleSheet.
Button {
    id: root

    // Primary actions use an accent fill. High-risk actions use the danger fill.
    property bool emphasized: false
    property bool destructive: false
    property bool selected: false

    readonly property bool filled: root.emphasized || root.destructive

    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    font.weight: root.filled ? Font.DemiBold : Font.Normal
    leftPadding: 12
    rightPadding: 12
    topPadding: 0
    bottomPadding: 0
    implicitHeight: Theme.controlMinHeight
    implicitWidth: Math.max(92, contentItem.implicitWidth + leftPadding + rightPadding)
    Layout.preferredHeight: implicitHeight
    Layout.maximumHeight: implicitHeight
    hoverEnabled: true

    contentItem: Text {
        text: root.text
        font: root.font
        color: {
            if (!root.enabled)
                return Theme.colors.text.disabled
            if (root.destructive)
                return Theme.colors.text.onDanger
            if (root.emphasized)
                return Theme.colors.text.onAccent
            return Theme.colors.text.primary
        }
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: HoverChrome {
        baseColor: !root.enabled ? Theme.colors.background.elevated
                   : root.destructive ? Theme.colors.danger.primary
                   : root.emphasized ? Theme.colors.accent.primary
                   : Theme.colors.background.elevated
        stateColors: root.destructive ? Theme.colors.dangerState
                   : root.emphasized ? Theme.colors.accentState
                   : Theme.colors.buttonState
        selected: root.selected || root.checked
        hovered: root.hovered
        pressed: root.down
        focused: root.visualFocus
    }
}
