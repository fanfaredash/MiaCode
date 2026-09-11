import QtQuick
import QtQuick.Controls
import MiaCode.UI

AppMenu {
    id: root

    required property var previewSession
    openRightAligned: true
    hugContent: true

    // The same ladder the backend steps through
    // (runtime/Shared.cpp kPreviewPlaybackRateOptions). A rate the menu cannot
    // spell — 0.25, which Ctrl+O reaches — would leave the list with nothing
    // ticked, and a single-choice list always has exactly one answer.
    readonly property var rates: [0.25, 0.5, 0.75, 1, 1.25, 1.5, 2]
    readonly property var rateLabels: {
        const labels = []
        for (let i = 0; i < rates.length; i++)
            labels.push(qsTrId("qml.1x").arg(rates[i]))
        return labels
    }

    Repeater {
        model: root.rates

        delegate: AppMenuItem {
            id: rateItem
            required property var modelData

            // One rate is in force at a time, so `checked` reports the
            // session's state and is never owned by the row.
            readonly property bool current: root.previewSession
                     && Math.abs(root.previewSession.rate - modelData) <= 1e-6

            text: qsTrId("qml.1x").arg(modelData)
            checkable: true
            checked: rateItem.current
            // A click runs AbstractButton.toggle() first and triggered()
            // second, and toggle() writes `checked` from C++ — behind the
            // binding, which keeps its stale value until a dependency moves.
            // Picking the rate already in force is therefore a plain check-box
            // toggle: the row flips itself off, `rate` never changes, nothing
            // re-evaluates, and the ladder is left with no answer at all.
            // Reinstating the binding on the way out makes the row report the
            // session again, whichever route set it — this menu, the speed
            // shortcuts, or the Preview menu.
            onTriggered: {
                root.previewSession.rate = modelData
                checked = Qt.binding(() => rateItem.current)
            }
        }
    }
}
