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
        text: UiText.text("全屏预览")
        onTriggered: root.fullscreenRequested()
    }

    AppMenuItem {
        text: UiText.text("自由比例")
        checkable: true
        checked: root.preferences && root.preferences.previewCanvasFreeAspect
        onTriggered: {
            if (!root.preferences)
                return
            root.preferences.previewCanvasFreeAspect = !root.preferences.previewCanvasFreeAspect
        }
    }
}
