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
        color: Theme.overlayColor(Theme.colors.background.surface)
        border.width: Theme.controlBorderWidth
        border.color: !root.enabled ? Theme.colors.border.normal
                     : (root.activeFocus || root.hovered) ? Theme.colors.accent.primary
                     : Theme.colors.border.control
    }
}
