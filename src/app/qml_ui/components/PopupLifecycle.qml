import QtQuick
import QtQuick.Controls

// Assign to a property on the popup so these connections stay out of contentData.
Connections {
    id: root

    required property Popup popup
    target: root.popup

    property bool closing: false
    readonly property bool active: root.popup.visible && !root.closing

    readonly property FadeTransition enterTransition: FadeTransition {}
    readonly property FadeTransition exitTransition: FadeTransition {
        appearing: false
        initialOpacity: root.popup.opacity
    }

    function onAboutToShow() {
        root.enterTransition.initialOpacity = root.closing ? root.popup.opacity : 0
        root.closing = false
    }
    function onAboutToHide() { root.closing = true }
    function onClosed() { root.closing = false }
}
