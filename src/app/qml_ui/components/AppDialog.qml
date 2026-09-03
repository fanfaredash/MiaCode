import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Preserve Basic dialog geometry; the card owns the translucent fill.
Dialog {
    id: root

    background: Rectangle {
        color: Theme.overlayColor(root.palette.window, Theme.popupOpacity)
        border.color: Theme.backgroundActive ? Theme.colors.border.floating : root.palette.dark
    }

    header: Label {
        text: root.title
        visible: parent?.parent === Overlay.overlay && root.title
        elide: Text.ElideRight
        font.bold: true
        padding: 12
        background: Rectangle {
            x: 1
            y: 1
            width: parent.width - 2
            height: parent.height - 1
            color: Theme.backgroundActive ? "transparent" : root.palette.window
        }
    }
}
