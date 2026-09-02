import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared popup menu — geometry mirrors v1 styleRoundedMenu.
Menu {
    id: root

    property var anchorItem: null
    property bool openRightAligned: false
    property bool hugContent: false

    padding: Theme.menuPadding
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    // Action-created rows and default Instantiator paths use this delegate.
    delegate: AppMenuItem {}

    function popupWidthForAnchor(anchor) {
        if (!root.hugContent)
            return Math.max(root.implicitWidth, anchor ? anchor.width : 0)
        let widest = 0
        for (let i = 0; i < root.count; i++) {
            const item = root.itemAt(i)
            if (item && item.textWidth !== undefined)
                widest = Math.max(widest, item.textWidth + item.chromeWidth)
        }
        return widest > 0
            ? Math.ceil(root.leftPadding + root.rightPadding + widest)
            : root.implicitWidth
    }

    function openAt(anchor) {
        if (!anchor || Overlay.overlay === null)
            return
        parent = Overlay.overlay
        root.anchorItem = anchor
        width = root.popupWidthForAnchor(anchor)
        open()
        Qt.callLater(root.reposition)
    }

    function reposition() {
        if (!root.anchorItem || Overlay.overlay === null)
            return
        const above = root.anchorItem.mapToItem(Overlay.overlay, 0, 0)
        width = root.popupWidthForAnchor(root.anchorItem)
        x = root.openRightAligned
            ? Math.max(0, above.x + root.anchorItem.width - width)
            : above.x
        y = Math.max(0, above.y - height)
    }

    onImplicitHeightChanged: if (visible)
        reposition()
    onCountChanged: if (visible)
        reposition()

    background: Rectangle {
        implicitWidth: root.hugContent ? 1 : 180
        color: Theme.colors.background.elevated
        border.width: Theme.controlBorderWidth
        border.color: Theme.colors.border.normal
        radius: Theme.controlRadius
    }
}
