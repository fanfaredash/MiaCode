import QtQuick
import QtQuick.Controls
import MiaCode.UI

ToolTip {
    id: root

    enter: FadeTransition {}
    exit: FadeTransition { appearing: false }

    delay: 550
    timeout: 3500
    padding: 6

    contentItem: Text {
        text: root.text
        color: Theme.colors.text.primary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }

    background: FloatingCard {}
}

