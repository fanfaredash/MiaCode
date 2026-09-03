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
        color: Theme.overlayColor(root.enabled
               ? Theme.colors.background.control
               : Theme.colors.background.controlDisabled)
        border.width: root.enabled && (root.activeFocus || root.hovered)
                      ? Theme.controlBorderWidth : 0
        border.color: Theme.colors.accent.primary
    }
}
