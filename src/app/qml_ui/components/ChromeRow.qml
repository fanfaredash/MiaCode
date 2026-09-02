import QtQuick
import QtQuick.Controls
import MiaCode.UI

// A row whose background is the shared HoverChrome, with the content inset
// that HoverChrome itself cannot supply.
//
// HoverChrome is a background: it insets itself from the control's edges but
// never the control's content, and nothing forced a caller to make up the
// difference — so `background: HoverChrome` on a bare delegate rendered its
// text flush against (or under) the highlight. Owning the padding here is the
// whole point of this type: prefer it over writing `background: HoverChrome`
// by hand. The one standing exception is AppMenuItem, which must remain a
// MenuItem for Menu to lay it out.
//
// The second HoverChrome trap still applies and cannot be solved by a type:
// never put an interactive child inside `contentItem`, because the highlight
// spans the whole row and will run underneath it. Put the child beside this
// row instead (see the shortcut editor's 恢复默认 button).
ItemDelegate {
    id: root

    // See HoverChrome: "nav" | "bar" | "hover" | "icon".
    property string tone: "nav"
    property bool selected: false
    // Chrome margins; -1 keeps the per-tone defaults.
    property real chromeMargins: -1

    readonly property bool chromeSelected: root.selected || root.highlighted

    // Font of the default label. This is deliberately NOT Control.font: a base
    // type's grouped `font.family` binding and a derived type's whole-font
    // assignment both write the same value and fight over it, so a row that
    // wants the code font (the completion popup) would keep losing its family
    // back to the UI one.
    property font labelFont: Qt.font({
        family: Theme.uiFont,
        pixelSize: Theme.uiFontSize
    })

    leftPadding: Theme.rowPaddingX
    rightPadding: Theme.rowPaddingX
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true

    contentItem: Text {
        text: root.text
        color: !root.enabled ? Theme.colors.text.disabled
               : (root.chromeSelected || root.hovered) ? Theme.colors.text.active
               : Theme.colors.text.secondary
        font: root.labelFont
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: HoverChrome {
        selected: root.chromeSelected
        hovered: root.hovered
        pressed: root.down
        tone: root.tone
        margins: root.chromeMargins
    }
}
