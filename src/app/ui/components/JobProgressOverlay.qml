import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// The shared dialog blocks interaction while the service runs asynchronously.
// Cancellation raises the flag consumed by the job at its checkpoints.
Item {
    id: root

    // A miacode::JobProgressService instance.
    property var progress: null
    property var comicResources: null

    readonly property bool jobActive: !!root.progress && root.progress.active
    readonly property bool chartExportActive: !!root.progress
        && root.progress.active
        && root.progress.taskTypeName === "chartExport"
    readonly property bool genericActive: !!root.progress
        && root.progress.active
        && root.progress.taskTypeName !== "chartExport"
    readonly property var chartExportLabelLines: {
        const label = root.progress ? String(root.progress.label || "") : ""
        const lines = label.split(/\r?\n/)
        return [lines.length > 0 ? lines[0] : "",
                lines.length > 1 ? lines.slice(1).join(" ") : ""]
    }
    readonly property real comicFrameAspectRatio: 10 / 9
    readonly property real comicPreferredHeight: root.chartExportActive
        ? 528 / root.comicFrameAspectRatio : 0

    AppDialog {
        objectName: "jobProgressCard"
        visible: root.jobActive
        preferredWidth: root.chartExportActive ? 560 : 420
        preferredHeight: root.chartExportActive
            ? 760 : Theme.dialogCompactHeight
        closePolicy: Popup.NoAutoClose
        title: root.progress ? root.progress.title : ""

        body: ColumnLayout {
            spacing: 10
            Text {
                objectName: "jobProgressLabel"
                Layout.fillWidth: true
                visible: !root.chartExportActive
                text: root.progress ? root.progress.label : ""
                color: Theme.colors.text.secondary
                wrapMode: Text.WordWrap
            }

            Text {
                objectName: "jobProgressChartExportLabelLine1"
                Layout.fillWidth: true
                Layout.preferredHeight: root.chartExportActive ? Theme.uiFontSize + 4 : 0
                visible: root.chartExportActive
                text: root.chartExportLabelLines[0]
                color: Theme.colors.text.secondary
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                objectName: "jobProgressChartExportLabelLine2"
                Layout.fillWidth: true
                Layout.preferredHeight: root.chartExportActive ? Theme.uiFontSize + 4 : 0
                visible: root.chartExportActive
                text: root.chartExportLabelLines[1]
                color: Theme.colors.text.secondary
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            ProgressBar {
                objectName: "jobProgressBar"
                Layout.fillWidth: true
                from: 0
                to: 100
                indeterminate: !!root.progress && root.progress.indeterminate
                value: root.progress ? root.progress.percent : 0
            }

            JobProgressComic {
                objectName: "jobProgressComic"
                Layout.fillWidth: true
                Layout.preferredHeight: root.chartExportActive
                    ? root.comicPreferredHeight : 0
                resources: root.comicResources
                active: root.jobActive
                chartExportActive: root.chartExportActive
            }

        }

        footer: DialogFooter {
            visible: !!root.progress && root.progress.cancellable
            choices: [{ id: "cancel", label: root.progress && root.progress.cancelRequested
                         ? qsTrId("qml.cancelling") : qsTrId("action.cancel"),
                        enabled: !!root.progress && !root.progress.cancelRequested }]
            onChosen: root.progress.requestCancel()
        }
    }
}
