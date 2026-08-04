import QtQuick
import MiaCode.UI

// Shared hover / selection fill for v2 shell chrome (not form controls).
// tones:
//   nav   — sidebar / list / menu rows (selected → gray lift); default chromeInset*
//   bar   — MenuBarItem; default vertical inset 3
//   hover — hover(+pressed) only; default chromeInset*
//   icon  — icon button; default no inset (margins 0)
Item {
    id: root

    property bool selected: false
    property bool hovered: false
    property bool pressed: false
    property string tone: "nav"

    // Set margins >= 0 to force uniform inset (overrides per-side defaults).
    property real margins: -1

    readonly property bool useRowInset: tone === "nav" || tone === "hover"
    readonly property real defaultX: useRowInset ? Theme.chromeInsetX : 0
    readonly property real defaultY: {
        if (root.tone === "bar")
            return 3
        if (root.useRowInset)
            return Theme.chromeInsetY
        return 0
    }

    property real topMargin: margins >= 0 ? margins : defaultY
    property real bottomMargin: margins >= 0 ? margins : defaultY
    property real leftMargin: margins >= 0 ? margins : defaultX
    property real rightMargin: margins >= 0 ? margins : defaultX

    readonly property color fillColor: {
        if (root.tone === "bar")
            return root.selected ? Theme.colors.state.lineHighlight : "transparent"
        if (root.tone === "hover" || root.tone === "icon") {
            if (root.pressed)
                return Theme.colors.state.pressed
            if (root.hovered)
                return Theme.colors.state.hover
            return "transparent"
        }
        // nav — gray only (no accent tint)
        if (root.pressed)
            return Theme.colors.state.pressed
        if (root.selected)
            return Theme.colors.state.selected
        if (root.hovered)
            return Theme.colors.state.hover
        return "transparent"
    }

    Rectangle {
        anchors.fill: parent
        anchors.topMargin: root.topMargin
        anchors.bottomMargin: root.bottomMargin
        anchors.leftMargin: root.leftMargin
        anchors.rightMargin: root.rightMargin
        radius: Theme.itemRadius
        color: root.fillColor
    }
}
