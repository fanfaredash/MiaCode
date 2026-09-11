import QtQuick
import QtQuick.Controls
import MiaCode.UI

AppMenu {
    id: root

    required property var preferences
    openRightAligned: true
    hugContent: true
    signal fullscreenRequested()

    AppMenuItem {
        text: qsTrId("preview.fullscreen.window_title")
        onTriggered: root.fullscreenRequested()
    }

    AppMenuItem {
        text: qsTrId("preview.canvas.free_aspect")
        checkable: true
        checked: root.preferences && root.preferences.previewCanvasFreeAspect
        onTriggered: {
            if (!root.preferences)
                return
            root.preferences.previewCanvasFreeAspect = !root.preferences.previewCanvasFreeAspect
        }
    }
}
