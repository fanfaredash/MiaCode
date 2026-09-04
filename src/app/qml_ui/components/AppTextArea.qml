import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Multi-line field matching AppTextField chrome (metadata extra fields).
TextArea {
    id: root

    font: Theme.codeFont
    color: Theme.colors.text.editor
    placeholderTextColor: Theme.colors.text.secondary
    selectedTextColor: Theme.colors.text.primary
    selectionColor: Theme.colors.state.textSelection
    leftPadding: 10
    rightPadding: 10
    topPadding: 8
    bottomPadding: 8
    wrapMode: TextEdit.NoWrap
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.controlRadius
        color: Theme.overlayColor(root.enabled
               ? Theme.colors.background.control
               : Theme.colors.background.controlDisabled)
        border.width: root.enabled && (root.activeFocus || root.hovered)
                      ? Theme.controlBorderWidth : 0
        border.color: Theme.colors.accent.primary
    }
}
