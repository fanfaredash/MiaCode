import QtQuick
import MiaCode.UI

// Sidebar / list navigation row. Text: selected → active (1.0); idle →
// secondary (~0.75 of active). Chrome and its content inset come from ChromeRow.
ChromeRow {
    id: root

    property int textLeftPadding: 20
    stateColors: Theme.colors.listState

    implicitHeight: 30
    height: implicitHeight
    leftPadding: root.textLeftPadding

    contentItem: Text {
        text: root.text
        color: !root.enabled ? Theme.colors.text.disabled
               : (root.chromeSelected || root.hovered || root.visualFocus) ? Theme.colors.text.active
               : Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
