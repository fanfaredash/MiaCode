import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared form field — geometry mirrors v1 dialogMenuLineEditStyleSheet.
TextField {
    id: root

    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    color: Theme.colors.text.primary
    placeholderTextColor: Theme.colors.text.secondary
    selectedTextColor: Theme.colors.text.primary
    selectionColor: Theme.colors.state.textSelection
    leftPadding: 10
    rightPadding: 10
    topPadding: 4
    bottomPadding: 4
    implicitHeight: Theme.controlMinHeight
    hoverEnabled: true

    background: Rectangle {
        implicitHeight: Theme.controlMinHeight
        radius: Theme.controlRadius
        color: root.enabled
               ? Theme.surfaceColor("input", Theme.colors.background.surface)
               : Theme.colors.background.elevated
        border.width: Theme.controlBorderWidth
        border.color: !root.enabled ? Theme.colors.border.normal
                     : (root.activeFocus || root.hovered) ? Theme.colors.accent.primary
                     : Theme.colors.border.control
    }
}
