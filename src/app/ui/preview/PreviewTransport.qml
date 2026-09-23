import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Item {
    id: root

    required property var previewSession
    required property var preferences
    required property var rangePreviewState
    property bool dataAvailable: true
    // True while the export page is up. The canvas menu hides there:
    // entering preview fullscreen on that page crashes the Intel iGPU D3D11
    // driver with hardware decode. Supplied by the caller, which knows the
    // active page.
    property bool exportPageActive: false
    property bool showCanvasMenuButton: !root.exportPageActive
    signal fullscreenRequested()


    readonly property real progressTopInset: 3
    readonly property real controlsBottomInset: 5
    readonly property real interactionGap: Theme.panelPadding
    implicitHeight: progressTopInset + progress.height + interactionGap
                    + transportRow.implicitHeight + controlsBottomInset

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
        if (!root.dataAvailable)
            return 0
        const bound = root.previewSession && root.previewSession.lowerBoundSeconds !== undefined
                      ? root.previewSession.lowerBoundSeconds
                      : 0
        return Math.min(0, bound)
    }
    readonly property real selectedRangeStartSeconds: root.rangePreviewState.available
        ? root.rangePreviewState.session.exportStartSeconds : 0
    readonly property real selectedRangeEndSeconds: root.rangePreviewState.available
        ? root.rangePreviewState.session.exportEndSeconds : 0
    readonly property real progressEndSeconds: root.dataAvailable
        ? (root.rangePreviewState.available
           ? root.rangePreviewState.session.contentDurationSeconds
           : root.previewSession.durationSeconds)
        : 0
    function boundedScrubSecond(second) {
        if (!root.rangePreviewState.active || !root.rangePreviewState.available)
            return second
        const start = root.selectedRangeStartSeconds <= 0.000001
            ? root.lowerBoundSeconds : root.selectedRangeStartSeconds
        return Math.max(start, Math.min(root.selectedRangeEndSeconds, second))
    }
    function moveRangeScrub(pointerX) {
        const travel = Math.max(1, progress.availableWidth - progress.handle.width)
        const fraction = Math.max(0, Math.min(1,
            (pointerX - progress.leftPadding - progress.handle.width / 2) / travel))
        const second = root.boundedScrubSecond(
            progress.from + fraction * (progress.to - progress.from))
        progress.value = second
        root.activeScrubSecond = second
        root.previewSession.updateScrub(second)
    }
    property bool scrubActive: false
    property real activeScrubSecond: root.previewSession.positionSeconds
    readonly property real displayedSeconds: root.dataAvailable
        ? (root.scrubActive ? root.activeScrubSecond : root.previewSession.positionSeconds)
        : 0

    // Shorten "pos / dur" only when the control row would actually collide —
    // independent of NoteStatistics column switching.
    readonly property int _visibleButtonCount: 3
        + (rangeModeButton.visible ? 1 : 0)
        + (canvasMenuButton.visible ? 1 : 0)
        + (fullscreenButton.visible ? 1 : 0)
    readonly property real _fixedChromeWidth: stopButton.implicitWidth + playButton.implicitWidth
        + (rangeModeButton.visible ? rangeModeButton.implicitWidth : 0)
        + rateButton.implicitWidth
        + (canvasMenuButton.visible ? canvasMenuButton.implicitWidth : 0)
        + (fullscreenButton.visible ? fullscreenButton.implicitWidth : 0)
        + transportRow.spacing * _visibleButtonCount
    readonly property real minimumWidth: _fixedChromeWidth + 16 + 40
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
              + " / " + root.formatTime(root.progressEndSeconds)
    }

    AppSlider {
        id: progress
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: root.progressTopInset
        height: 24
        from: root.lowerBoundSeconds
        to: root.progressEndSeconds
        rangeMarkersVisible: root.dataAvailable && root.rangePreviewState.available
        rangeStartValue: root.selectedRangeStartSeconds <= 0.000001
            ? progress.from : root.selectedRangeStartSeconds
        rangeEndValue: root.selectedRangeEndSeconds
        rangeHighlightVisible: rangeMarkersVisible && root.rangePreviewState.active
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
        // 范围重建时按当前进度定位，涵盖位置数值保持不变的页面切换。
        value: Math.max(progress.from, Math.min(progress.to, root.previewSession.positionSeconds))
        when: !progress.pressed && !root.scrubActive
    }

    MouseArea {
        anchors.fill: progress
        visible: progress.rangeHighlightVisible
        onPressed: function(mouse) {
            root.scrubActive = true
            root.previewSession.beginScrub()
            root.moveRangeScrub(mouse.x)
        }
        onPositionChanged: function(mouse) {
            if (pressed)
                root.moveRangeScrub(mouse.x)
        }
        onReleased: function(mouse) {
            root.moveRangeScrub(mouse.x)
            root.previewSession.endScrub(root.activeScrubSecond)
            root.scrubActive = false
        }
        onCanceled: {
            root.previewSession.endScrub(root.activeScrubSecond)
            root.scrubActive = false
        }
    }

    RowLayout {
        id: transportRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.bottomMargin: root.controlsBottomInset
        spacing: 5

        IconButton {
            id: stopButton
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            iconSource: Qt.resolvedUrl("icons/stop.svg")
            tooltip: qsTrId("dialog.video_export.preview.stop")
            onClicked: root.previewSession.stop()
        }
        IconButton {
            id: playButton
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            iconSource: Qt.resolvedUrl(root.previewSession.playing ? "icons/pause.svg" : "icons/play.svg")
            tooltip: root.previewSession.playing ? qsTrId("preview.pause") : qsTrId("preview.play")
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
                    return pos + " / " + root.formatTime(root.progressEndSeconds)
                return pos
            }
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.secondaryFontSize
        }

        IconButton {
            id: rangeModeButton
            objectName: "exportRangeModeButton"
            visible: root.rangePreviewState.available
            enabled: root.dataAvailable && root.rangePreviewState.available
                     && root.rangePreviewState.session.exportEndSeconds
                        > root.rangePreviewState.session.exportStartSeconds
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            iconSource: Qt.resolvedUrl("icons/circle-play.svg")
            active: root.rangePreviewState.active
            tooltip: qsTrId("preview.range_mode")
            onClicked: root.rangePreviewState.active = !root.rangePreviewState.active
        }

        AppDropDownButton {
            id: rateButton
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            text: qsTrId("qml.1x").arg(root.previewSession.rate)
            sizeToLabels: rateMenu.rateLabels
            tooltip: qsTrId("qml.playback_speed")
            expanded: rateMenu.active
            Accessible.description: qsTrId("qml.open_playback_speed_presets")
            onClicked: {
                if (rateMenu.active) {
                    rateMenu.close()
                    return
                }
                rateMenu.openAt(rateButton)
            }
        }

        IconButton {
            id: canvasMenuButton
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            visible: root.showCanvasMenuButton
            active: canvasMenu.active
            iconSource: Qt.resolvedUrl("icons/preview-settings.svg")
            tooltip: qsTrId("preview.canvas.menu_tooltip")
            Accessible.description: qsTrId("preview.canvas.menu_description")
            onClicked: {
                if (canvasMenu.active) {
                    canvasMenu.close()
                    return
                }
                canvasMenu.openAt(canvasMenuButton)
            }
        }

        IconButton {
            id: fullscreenButton
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            visible: root.showCanvasMenuButton
            iconSource: Qt.resolvedUrl("icons/fullscreen.svg")
            tooltip: qsTrId("preview.fullscreen.enter_tooltip")
            onClicked: root.fullscreenRequested()
        }
    }

    PreviewRateMenu {
        id: rateMenu
        previewSession: root.previewSession
    }

    PreviewCanvasMenu {
        id: canvasMenu
        preferences: root.preferences
        previewSession: root.previewSession
    }
}
