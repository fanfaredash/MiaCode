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
        const negative = totalSeconds < -0.0001
        const safeSeconds = Math.floor(Math.abs(totalSeconds) + 0.0001)
        const minutes = Math.floor(safeSeconds / 60)
        const seconds = safeSeconds % 60
        return (negative ? "-" : "") + String(minutes).padStart(2, "0") + ":" + String(seconds).padStart(2, "0")
    }

    readonly property real lowerBoundSeconds: {
        const bound = root.previewSession && root.previewSession.lowerBoundSeconds !== undefined
                      ? root.previewSession.lowerBoundSeconds
                      : 0
        return Math.min(0, bound)
    }
    property bool scrubActive: false
    property real rateMenuClosedAt: 0
    property real activeScrubSecond: root.previewSession.positionSeconds
    readonly property real displayedSeconds: root.scrubActive
        ? root.activeScrubSecond
        : root.previewSession.positionSeconds

    // Shorten "pos / dur" only when the control row would actually collide —
    // independent of NoteStatistics column switching.
    readonly property int _fixedChromeWidth: {
        const fullscreenW = exportPageActive ? 0 : 28
        return 28 + 5 + 28 + 5 + rateButton.implicitWidth + 5 + fullscreenW
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
        text: root.formatTime(root.displayedSeconds)
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
        from: root.lowerBoundSeconds
        to: root.previewSession.durationSeconds
        live: true
        onPressedChanged: {
            if (pressed) {
                root.scrubActive = true
                root.activeScrubSecond = value
                root.previewSession.beginScrub()
                return
            }
            if (!root.scrubActive)
                return
            const releaseSecond = root.activeScrubSecond
            root.scrubActive = false
            root.previewSession.endScrub(releaseSecond)
        }
        onMoved: {
            root.activeScrubSecond = value
            root.previewSession.updateScrub(root.activeScrubSecond)
        }
    }

    Binding {
        target: progress
        property: "value"
        value: root.previewSession.positionSeconds
        when: !progress.pressed
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
                const pos = root.formatTime(root.displayedSeconds)
                if (root.timeFitsFull)
                    return pos + " / " + root.formatTime(root.previewSession.durationSeconds)
                return pos
            }
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.secondaryFontSize
        }

        AppDropDownButton {
            id: rateButton
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            text: qsTr("%1x").arg(root.previewSession.rate)
            sizeToLabels: rateMenu.rateLabels
            tooltip: qsTr("播放速度")
            expanded: rateMenu.visible
            Accessible.description: qsTr("打开播放速度预设")
            onClicked: {
                if (rateMenu.visible) {
                    rateMenu.close()
                    return
                }
                if (Date.now() - root.rateMenuClosedAt < 200)
                    return
                rateMenu.openAt(rateButton)
            }
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

    PreviewRateMenu {
        id: rateMenu
        previewSession: root.previewSession
        onClosed: root.rateMenuClosedAt = Date.now()
    }
}
