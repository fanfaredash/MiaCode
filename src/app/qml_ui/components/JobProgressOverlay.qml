import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// The shared dialog blocks interaction while the service runs asynchronously.
// Cancellation raises the flag consumed by the job at its checkpoints.
Item {
    id: root

    // A miacode::v2::JobProgressService instance.
    property var progress: null

    readonly property bool jobActive: !!root.progress && root.progress.active

    AppDialog {
        objectName: "jobProgressCard"
        visible: root.jobActive
        preferredWidth: 420
        closePolicy: Popup.NoAutoClose
        title: root.progress ? root.progress.title : ""

        body: ColumnLayout {
            spacing: 10
            Text {
                objectName: "jobProgressLabel"
                Layout.fillWidth: true
                text: root.progress ? root.progress.label : ""
                color: Theme.colors.text.secondary
                wrapMode: Text.WordWrap
            }

            ProgressBar {
                objectName: "jobProgressBar"
                Layout.fillWidth: true
                from: 0
                to: 100
                indeterminate: !!root.progress && root.progress.indeterminate
                value: root.progress ? root.progress.percent : 0
            }

        }

        footer: DialogFooter {
            visible: !!root.progress && root.progress.cancellable
            choices: [{ id: "cancel", label: root.progress && root.progress.cancelRequested
                         ? UiText.text("正在取消…") : UiText.text("取消"),
                        enabled: !!root.progress && !root.progress.cancelRequested }]
            onChosen: root.progress.requestCancel()
        }
    }
}
