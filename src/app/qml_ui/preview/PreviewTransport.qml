import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Rectangle {
    id: root

    required property var previewSession
    property var shellController
    signal fullscreenRequested()

    readonly property bool exportPageActive: !!(root.shellController && root.shellController.exportPageActive)

    implicitHeight: 63
    color: Theme.colors.background.surface

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

    // Shorten "pos / dur" only when the control row would actually collide —
    // independent of NoteStatistics column switching.
    readonly property int _fixedChromeWidth: {
        const fullscreenW = exportPageActive ? 0 : 28
        return 28 + 5 + 28 + 5 + 72 + 5 + fullscreenW
    }
    readonly property bool timeFitsFull: {
        const margins = 16
        const fullTimeW = fullTimeMetrics.width + 8
        return _fixedChromeWidth + fullTimeW + margins <= width
    }

    TextMetrics {
        id: fullTimeMetrics
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize
        text: root.formatTime(root.previewSession.positionSeconds)
              + " / " + root.formatTime(root.previewSession.durationSeconds)
    }

    AppSlider {
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
    }

    RowLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.bottomMargin: 5
        spacing: 5

        IconButton {
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            iconSource: Qt.resolvedUrl("icons/stop.svg")
            tooltip: qsTr("停止")
            onClicked: root.previewSession.stop()
        }
        IconButton {
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            iconSource: Qt.resolvedUrl(root.previewSession.playing ? "icons/pause.svg" : "icons/play.svg")
            tooltip: root.previewSession.playing ? qsTr("暂停") : qsTr("播放")
            onClicked: root.previewSession.playing = !root.previewSession.playing
        }

        Text {
            Layout.fillWidth: true
            Layout.minimumWidth: 40
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            text: {
                const pos = root.formatTime(root.previewSession.positionSeconds)
                if (root.timeFitsFull)
                    return pos + " / " + root.formatTime(root.previewSession.durationSeconds)
                return pos
            }
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.secondaryFontSize
        }

        AppComboBox {
            id: rateBox
            Layout.preferredWidth: 72
            Layout.preferredHeight: implicitHeight
            compact: true
            model: ["0.5x", "0.75x", "1x", "1.25x", "1.5x", "2x"]
            readonly property var rates: [0.5, 0.75, 1, 1.25, 1.5, 2]
            currentIndex: Math.max(0, rates.indexOf(root.previewSession.rate))
            onActivated: root.previewSession.rate = rates[currentIndex]
        }

        IconButton {
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            visible: !root.exportPageActive
            iconSource: Qt.resolvedUrl("icons/fullscreen.svg")
            tooltip: qsTr("全屏预览")
            onClicked: root.fullscreenRequested()
        }
    }
}
