import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var previewSession
    property var shellController
    signal fullscreenRequested()

    readonly property bool exportPageActive: !!(root.shellController && root.shellController.exportPageActive)

    implicitHeight: 63
    color: Theme.colors.background.workbench

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.colors.border.normal
    }

    function formatTime(totalSeconds) {
        const minutes = Math.floor(totalSeconds / 60)
        const seconds = Math.floor(totalSeconds % 60)
        return String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0")
    }

    Slider {
        id: progress
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 3
        height: 24
        from: 0
        to: root.previewSession.durationSeconds
        value: root.previewSession.positionSeconds
        onMoved: root.previewSession.positionSeconds = value

        background: Rectangle {
            x: progress.leftPadding
            y: progress.topPadding + progress.availableHeight / 2 - height / 2
            width: progress.availableWidth
            height: 3
            radius: 2
            color: Theme.colors.border.control

            Rectangle {
                width: progress.visualPosition * parent.width
                height: parent.height
                radius: 2
                color: Theme.colors.text.secondary
            }
        }

        handle: Rectangle {
            x: progress.leftPadding + progress.visualPosition * (progress.availableWidth - width)
            y: progress.topPadding + progress.availableHeight / 2 - height / 2
            width: 12
            height: 12
            radius: 6
            color: Theme.colors.text.secondary
        }
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 5
        spacing: 5

        IconButton {
            iconSource: Qt.resolvedUrl("icons/stop.svg")
            tooltip: qsTr("停止")
            onClicked: root.previewSession.stop()
        }
        IconButton {
            iconSource: Qt.resolvedUrl(root.previewSession.playing ? "icons/pause.svg" : "icons/play.svg")
            tooltip: root.previewSession.playing ? qsTr("暂停") : qsTr("播放")
            onClicked: root.previewSession.playing = !root.previewSession.playing
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: root.formatTime(root.previewSession.positionSeconds)
                  + " / " + root.formatTime(root.previewSession.durationSeconds)
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.secondaryFontSize
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 5
        spacing: 5

        ComboBox {
            id: rateBox
            width: 58
            height: 27
            model: ["0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x"]
            readonly property var rates: [0.5, 0.75, 1, 1.25, 1.5, 2]
            currentIndex: Math.max(0, rates.indexOf(root.previewSession.rate))
            onActivated: root.previewSession.rate = rates[currentIndex]
        }

        IconButton {
            visible: !root.exportPageActive
            iconSource: Qt.resolvedUrl("icons/fullscreen.svg")
            tooltip: qsTr("全屏预览")
            onClicked: root.fullscreenRequested()
        }
    }
}

