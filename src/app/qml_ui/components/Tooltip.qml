import QtQuick
import QtQuick.Controls
import MiaCode.UI

ToolTip {
    id: root

    delay: 550
    timeout: 3500
    padding: 6

    contentItem: Text {
        text: root.text
        color: Theme.colors.text.primary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }

    background: Rectangle {
        color: Theme.colors.background.elevated
        border.color: Theme.colors.border.normal
        radius: Theme.itemRadius
    }
}

