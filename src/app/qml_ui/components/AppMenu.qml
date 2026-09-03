import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared popup menu — geometry mirrors v1 styleRoundedMenu.
Menu {
    id: root
    popupType: Popup.Item

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

    implicitWidth: {
        let widest = 0
        for (let i = 0; i < root.count; i++) {
            const item = root.itemAt(i)
            if (item)
                widest = Math.max(widest, item.implicitWidth)
        }
        return Math.max(root.hugContent ? 0 : 180,
                        Math.ceil(root.leftPadding + root.rightPadding + widest))
    }
    width: popupWidthForAnchor(root.anchorItem)

    function popupWidthForAnchor(anchor) {
        const preferred = Math.max(root.implicitWidth,
                                   !root.hugContent && anchor ? anchor.width : 0)
        return Math.min(preferred, Overlay.overlay ? Overlay.overlay.width : preferred)
    }

    function openAt(anchor) {
        if (!anchor || Overlay.overlay === null)
            return
        parent = anchor
        closePolicy = Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        root.anchorItem = anchor
        open()
        Qt.callLater(root.reposition)
    }

    function reposition() {
        if (!root.anchorItem || Overlay.overlay === null)
            return
        x = root.openRightAligned ? root.anchorItem.width - width : 0
        y = -height
    }

    onImplicitHeightChanged: if (visible)
        reposition()
    onWidthChanged: if (visible)
        reposition()
    onCountChanged: if (visible)
        reposition()

    background: FloatingCard {
        popup: root
    }
}
