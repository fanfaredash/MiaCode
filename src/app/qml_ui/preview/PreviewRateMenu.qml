import QtQuick
import QtQuick.Controls
import MiaCode.UI

AppMenu {
    id: root

    required property var previewSession
    openRightAligned: true
    hugContent: true

    readonly property var rates: [0.5, 0.75, 1, 1.25, 1.5, 2]
    readonly property var rateLabels: {
        const labels = []
        for (let i = 0; i < rates.length; i++)
            labels.push(UiText.text("%1x").arg(rates[i]))
        return labels
    }

    Repeater {
        model: root.rates

        delegate: AppMenuItem {
            required property var modelData

            text: UiText.text("%1x").arg(modelData)
            checkable: true
            checked: root.previewSession
                     && Math.abs(root.previewSession.rate - modelData) <= 1e-6
            onTriggered: root.previewSession.rate = modelData
        }
    }
}
