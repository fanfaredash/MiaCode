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
            labels.push(qsTr("%1%").arg(Math.round(values[i] * 100)))
        return labels
    }

    Repeater {
        model: root.stateBridge ? root.stateBridge.zoomPresetValues : []

        delegate: AppMenuItem {
            required property var modelData

            text: qsTr("%1%").arg(Math.round(modelData * 100))
            checkable: true
            checked: root.stateBridge
                     && Math.abs(root.stateBridge.zoomScale - modelData) <= 1e-6
            onTriggered: root.stateBridge.applyZoomPreset(modelData)
        }
    }
}
