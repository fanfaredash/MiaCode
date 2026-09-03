import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared popup menu — geometry mirrors v1 styleRoundedMenu.
Menu {
    id: root

    readonly property PopupLifecycle lifecycle: PopupLifecycle {
        popup: root
    }
    readonly property bool active: lifecycle.active
    enter: lifecycle.enterTransition
    exit: lifecycle.exitTransition

    property var anchorItem: null
    property bool openRightAligned: false
    property bool hugContent: false

    padding: Theme.menuPadding
    margins: 0
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
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
        parent = anchor
        closePolicy = Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        root.anchorItem = anchor
        width = root.popupWidthForAnchor(anchor)
        open()
        Qt.callLater(root.reposition)
    }

    function reposition() {
        if (!root.anchorItem || Overlay.overlay === null)
            return
        width = root.popupWidthForAnchor(root.anchorItem)
        x = root.openRightAligned ? root.anchorItem.width - width : 0
        y = -height
    }

    onImplicitHeightChanged: if (visible)
        reposition()
    onCountChanged: if (visible)
        reposition()

    background: FloatingCard {
        implicitWidth: root.hugContent ? 1 : 180
    }
}
