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
    property var imageResources: null

    readonly property bool jobActive: !!root.progress && root.progress.active
    readonly property bool chartExportActive: !!root.progress
        && root.progress.active
        && root.progress.chartExport
    readonly property var chartExportLabelLines: {
        const label = root.progress ? String(root.progress.label || "") : ""
        const lines = label.split(/\r?\n/)
        return [lines.length > 0 ? lines[0] : "",
                lines.length > 1 ? lines.slice(1).join(" ") : ""]
    }
    readonly property real imageAspectRatio: 10 / 9
    readonly property real imageWidth: 340
    readonly property real imageHeight: root.imageWidth / root.imageAspectRatio
    readonly property bool hasImageResources: root.imageResources !== null
        && root.imageResources.resourceCount > 0
    readonly property int progressBodySpacing: 10

    AppDialog {
        objectName: "jobProgressCard"
        visible: root.jobActive
        preferredWidth: root.chartExportActive && root.hasImageResources ? 520 : 420
        preferredHeight: root.chartExportActive && root.hasImageResources
            ? 499 : Theme.dialogCompactHeight
        closePolicy: Popup.NoAutoClose
        title: root.progress ? root.progress.title : ""

        body: ColumnLayout {
            spacing: root.progressBodySpacing
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

            Item {
                objectName: "jobProgressImageControls"
                Layout.fillWidth: true
                Layout.preferredHeight: root.chartExportActive && root.hasImageResources
                    ? root.imageHeight : 0
                Layout.bottomMargin: root.chartExportActive && root.hasImageResources
                    ? root.progressBodySpacing * 2 : 0

                ImageSequence {
                    id: imageSequence
                    objectName: "jobProgressImageSequence"
                    anchors.fill: parent
                    imageWidth: root.imageWidth
                    resources: root.imageResources
                    active: root.chartExportActive
                    z: 0
                }

                IconButton {
                    iconSource: Qt.resolvedUrl("icons/chevron-left.svg")
                    iconWidth: 20
                    iconHeight: 20
                    width: 48
                    height: 56
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    z: 1
                    tooltip: qsTrId("qml.navigate_back")
                    Accessible.name: qsTrId("qml.navigate_back")
                    visible: root.chartExportActive
                        && root.imageResources !== null
                        && root.imageResources.resourceCount >= 2
                    enabled: imageSequence.canSwitch
                    onClicked: imageSequence.selectPrevious()
                }

                IconButton {
                    iconSource: Qt.resolvedUrl("icons/chevron-right.svg")
                    iconWidth: 20
                    iconHeight: 20
                    width: 48
                    height: 56
                    anchors.right: parent.right
                    anchors.verticalCenter: parent.verticalCenter
                    z: 1
                    tooltip: qsTrId("qml.navigate_forward")
                    Accessible.name: qsTrId("qml.navigate_forward")
                    visible: root.chartExportActive
                        && root.imageResources !== null
                        && root.imageResources.resourceCount >= 2
                    enabled: imageSequence.canSwitch
                    onClicked: imageSequence.selectNext()
                }
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
