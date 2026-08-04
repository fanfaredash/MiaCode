import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared popup menu — geometry mirrors v1 styleRoundedMenu.
Menu {
    id: root

    padding: Theme.menuPadding
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize

    // Action-created rows and default Instantiator paths use this delegate.
    delegate: AppMenuItem {}

    background: Rectangle {
        implicitWidth: 180
        color: Theme.colors.background.elevated
        border.width: Theme.controlBorderWidth
        border.color: Theme.colors.border.normal
        radius: Theme.controlRadius
    }
}
