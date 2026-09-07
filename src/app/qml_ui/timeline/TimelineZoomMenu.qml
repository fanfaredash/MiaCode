import QtQuick
import QtQuick.Controls
import MiaCode.UI

AppMenu {
    id: root

    required property var stateBridge
    hugContent: true

    readonly property var zoomLabels: {
        const values = stateBridge ? stateBridge.zoomPresetValues : []
        const labels = []
        for (let i = 0; i < values.length; i++)
            labels.push(UiText.text("%1%").arg(Math.round(values[i] * 100)))
        return labels
    }

    Repeater {
        model: root.stateBridge ? root.stateBridge.zoomPresetValues : []

        delegate: AppMenuItem {
            id: zoomItem
            required property var modelData
            compact: true

            readonly property bool current: root.stateBridge
                     && Math.abs(root.stateBridge.zoomScale - modelData) <= 1e-6

            text: UiText.text("%1%").arg(Math.round(modelData * 100))
            checkable: true
            checked: zoomItem.current
            // Single-choice, same as PreviewRateMenu — see the note there for
            // why the binding has to be reinstated after AbstractButton.toggle().
            onTriggered: {
                root.stateBridge.applyZoomPreset(modelData)
                checked = Qt.binding(() => zoomItem.current)
            }
        }
    }
}
