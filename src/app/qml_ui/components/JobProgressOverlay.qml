import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Renders whatever job JobProgressService currently reports. It blocks input
// beneath itself while a job runs, but it is not a modal window: the job keeps
// running on the UI thread and cancellation only raises a flag the job reads at
// its own checkpoints.
Item {
    id: root

    // A miacode::v2::JobProgressService instance.
    property var progress: null

    readonly property bool jobActive: !!root.progress && root.progress.active

    visible: root.jobActive
    z: 100

    MouseArea {
        anchors.fill: parent
        enabled: root.jobActive
        hoverEnabled: true
        // Swallow clicks so the job cannot be re-triggered while it runs.
        onClicked: {}
    }

    Rectangle {
        anchors.fill: parent
        color: "#000000"
        opacity: 0.5
    }

    Rectangle {
        id: card
        objectName: "jobProgressCard"
        anchors.centerIn: parent
        width: Math.min(420, parent.width - 48)
        implicitHeight: layout.implicitHeight + 32
        height: implicitHeight
        radius: 8
        color: Theme.overlayColor(Theme.colors.background.elevated, Theme.popupOpacity)
        border.width: 1
        border.color: Theme.backgroundActive ? Theme.colors.border.floating : Theme.colors.border.normal

        Column {
            id: layout
            x: 16
            y: 16
            width: parent.width - 32
            spacing: 10

            Text {
                objectName: "jobProgressTitle"
                width: parent.width
                text: root.progress ? root.progress.title : ""
                color: Theme.colors.text.active
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                font.bold: true
                elide: Text.ElideRight
            }

            Text {
                objectName: "jobProgressLabel"
                width: parent.width
                text: root.progress ? root.progress.label : ""
                color: Theme.colors.text.secondary
                elide: Text.ElideMiddle
            }

            ProgressBar {
                objectName: "jobProgressBar"
                width: parent.width
                from: 0
                to: 100
                indeterminate: !!root.progress && root.progress.indeterminate
                value: root.progress ? root.progress.percent : 0
            }

            Row {
                width: parent.width
                layoutDirection: Qt.RightToLeft
                AppButton {
                    objectName: "jobProgressCancelButton"
                    visible: !!root.progress && root.progress.cancellable
                    enabled: !!root.progress && !root.progress.cancelRequested
                    text: root.progress && root.progress.cancelRequested
                          ? UiText.text("正在取消…")
                          : UiText.text("取消")
                    onClicked: if (root.progress) root.progress.requestCancel()
                }
            }
        }
    }
}
