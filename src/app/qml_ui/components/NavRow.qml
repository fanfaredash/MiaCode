import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Sidebar / list navigation row with shared HoverChrome.
// Text: selected → active (1.0); idle → secondary (~0.75 of active).
AbstractButton {
    id: root

    property bool selected: false
    property int textLeftPadding: 20

    implicitHeight: 30
    height: implicitHeight
    hoverEnabled: true

    contentItem: Text {
        leftPadding: root.textLeftPadding
        text: root.text
        color: !root.enabled ? Theme.colors.text.disabled
               : root.selected ? Theme.colors.text.active
               : Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: HoverChrome {
        selected: root.selected
        hovered: root.hovered
        pressed: root.down
        tone: "nav"
    }
}
