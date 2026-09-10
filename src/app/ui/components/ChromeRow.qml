import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared row content padding with state rendering supplied by HoverChrome.
ItemDelegate {
    id: root

    property bool selected: false
    property alias stateColors: chrome.stateColors

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
    implicitHeight: Math.max(Theme.controlMinHeight,
                             implicitContentHeight + 2 * (Theme.chromePadding + Theme.chromeInsetY))
    hoverEnabled: true

    contentItem: Text {
        text: root.text
        color: !root.enabled ? Theme.colors.text.disabled
               : (root.chromeSelected || root.hovered || root.visualFocus) ? Theme.colors.text.active
               : Theme.colors.text.secondary
        font: root.labelFont
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    background: HoverChrome {
        id: chrome
        contentHeight: root.implicitContentHeight
        selected: root.chromeSelected
        hovered: root.hovered
        pressed: root.down
        focused: root.visualFocus
    }
}
