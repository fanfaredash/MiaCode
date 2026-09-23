import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import MiaCode.UI

Item {
    id: root

    required property var exportSession
    required property var previewSession

    property string draggingTarget: ""
    property real dragStartSeconds: 0
    property real dragEndSeconds: 0
    property real dragPressSecond: 0
    property real dragPreviewSecond: 0
    property real hoverSecond: 0
    signal rangeInteractionRequested(real startSecond, real endSecond)

    readonly property real totalSeconds: Math.max(0, Number(exportSession.contentDurationSeconds) || 0)
    readonly property real minimumRangeSeconds: Math.max(0,
                                                         Number(exportSession.minimumExportRangeSeconds) || 0)
    readonly property real startSeconds: Math.max(0, Math.min(totalSeconds,
                                                               Number(exportSession.exportStartSeconds) || 0))
    readonly property real endSeconds: Math.max(startSeconds + minimumRangeSeconds,
                                                 Math.min(totalSeconds,
                                                          Number(exportSession.exportEndSeconds) || 0))
    readonly property real playheadSeconds: Math.max(0, Math.min(totalSeconds,
                                                                  Number(previewSession.positionSeconds) || 0))
    // The lane can land on fractional logical coordinates on a fractional-DPR
    // display. Keep narrow playback geometry on the device-pixel grid so its
    // apparent width does not change as the center moves between samples.
    readonly property real renderDpr: Math.max(1, Number(Screen.devicePixelRatio) || 1)
    readonly property real timestampBandHeight: 0
    // What the overlay reads out. Hovering asks "what is under my pointer";
    // dragging asks "where is the thing I am moving", and those are not the
    // same second — grabbing the middle of the range and pushing it left put
    // the readout wherever the grab happened to land, which is a point with no
    // meaning to anyone.
    readonly property real displaySecond: draggingTarget.length > 0 ? dragPreviewSecond
                                                                    : hoverSecond
    readonly property real timestampWidth: Math.max(1,
                                                     Math.ceil(Math.max(
                                                         timestampMetrics.advanceWidth,
                                                         durationTimestampMetrics.advanceWidth)))

    implicitHeight: timestampBandHeight + lane.height + Theme.captionFontSize + 6
    Layout.fillWidth: true

    function formatSecond(second) {
        const milliseconds = Math.max(0, Math.round(second * 1000))
        const minutes = Math.floor(milliseconds / 60000)
        const seconds = Math.floor(milliseconds / 1000) % 60
        const millis = milliseconds % 1000
        return String(minutes).padStart(2, "0") + ":"
            + String(seconds).padStart(2, "0") + "."
            + String(millis).padStart(3, "0")
    }

    function beginDrag(target, second) {
        draggingTarget = target
        dragStartSeconds = startSeconds
        dragEndSeconds = endSeconds
        dragPressSecond = second
        // A body drag reads out its start: that is the edge the range is being
        // placed by, and it is the one the label can sit against while both
        // ends move together.
        dragPreviewSecond = target === "end" ? endSeconds : startSeconds
        previewSession.beginScrub()
    }

    function updateDrag(second) {
        const delta = second - dragPressSecond
        if (draggingTarget === "start") {
            const nextStart = Math.max(0, Math.min(dragEndSeconds - minimumRangeSeconds,
                                                    dragStartSeconds + delta))
            exportSession.setExportRangeSeconds(nextStart, dragEndSeconds)
            dragPreviewSecond = nextStart
        } else if (draggingTarget === "end") {
            const nextEnd = Math.max(dragStartSeconds + minimumRangeSeconds,
                                     Math.min(totalSeconds, dragEndSeconds + delta))
            exportSession.setExportRangeSeconds(dragStartSeconds, nextEnd)
            dragPreviewSecond = nextEnd
        } else if (draggingTarget === "range") {
            const boundedDelta = Math.max(-dragStartSeconds,
                                          Math.min(totalSeconds - dragEndSeconds, delta))
            const nextStart = dragStartSeconds + boundedDelta
            const nextEnd = dragEndSeconds + boundedDelta
            exportSession.setExportRangeSeconds(nextStart, nextEnd)
            dragPreviewSecond = nextStart
        } else {
            return
        }
        previewSession.updateScrub(dragPreviewSecond)
    }

    function endDrag() {
        if (draggingTarget.length === 0)
            return
        previewSession.endScrub(dragPreviewSecond)
        rangeInteractionRequested(exportSession.exportStartSeconds,
                                  exportSession.exportEndSeconds)
        draggingTarget = ""
    }

    TextMetrics {
        id: timestampMetrics

        font.family: Theme.uiFont
        font.pixelSize: Theme.captionFontSize
        // Eight is normally the widest proportional digit. The duration
        // metric below also covers longer-than-99-minute charts.
        text: "88:88.888"
    }

    TextMetrics {
        id: durationTimestampMetrics

        font.family: Theme.uiFont
        font.pixelSize: Theme.captionFontSize
        text: root.formatSecond(root.totalSeconds)
    }

    Text {
        id: timestamp

        objectName: "exportRangeTimestamp"
        width: root.timestampWidth
        horizontalAlignment: Text.AlignHCenter
        x: Math.max(0, Math.min(root.width - width,
                                lane.xForSecond(root.displaySecond) - width * 0.5))
        y: -height
        visible: mouseArea.containsMouse || root.draggingTarget.length > 0
        text: root.formatSecond(root.displaySecond)
        color: Theme.colors.text.primary
        font.family: Theme.uiFont
        font.pixelSize: Theme.captionFontSize
        z: 1

        Rectangle {
            anchors.fill: parent
            anchors.margins: -4
            radius: 3
            color: Theme.overlayColor(Theme.colors.background.elevated, Theme.popupOpacity)
            z: -1
        }
    }

    Item {
        id: lane

        objectName: "exportRangeLane"
        anchors.left: parent.left
        anchors.right: parent.right
        y: root.timestampBandHeight
        height: 16

        readonly property real sideInset: 10
        readonly property real trackY: 5
        readonly property real trackHeight: 6
        readonly property real handleWidth: 14
        readonly property real handleHeight: 14
        readonly property real handleHitRadius: handleWidth * 0.5 + 3
        readonly property real minimumVisualSelectionWidth: handleWidth * 3
        readonly property int playheadWidthDevicePixels: 2
        // Use the lane's scene origin when snapping. mapToItem() is an
        // invokable rather than a property, so read it at each geometry
        // evaluation; caching it in a binding can retain the pre-layout origin.
        function sceneOriginX() {
            const mapped = lane.mapToItem(null, 0, 0)
            return mapped && isFinite(mapped.x) ? mapped.x : 0
        }
        readonly property int playheadCenterDeviceX: {
            const originX = sceneOriginX()
            return Math.round((originX + xForSecond(root.playheadSeconds)) * root.renderDpr)
        }
        readonly property int playheadLeftDeviceX:
            playheadCenterDeviceX - Math.floor(playheadWidthDevicePixels * 0.5)
        readonly property int playheadRightDeviceX:
            playheadCenterDeviceX + Math.ceil(playheadWidthDevicePixels * 0.5)
        readonly property real playheadLeftX: {
            const originX = sceneOriginX()
            return playheadLeftDeviceX / root.renderDpr - originX
        }
        readonly property real playheadRightX: {
            return playheadLeftX + playheadWidth
        }
        readonly property real playheadWidth:
            playheadWidthDevicePixels / root.renderDpr
        readonly property real actualStartX: xForSecond(root.startSeconds)
        readonly property real actualEndX: xForSecond(root.endSeconds)
        readonly property real availableTrackWidth: Math.max(1, width - sideInset * 2)
        readonly property real visualSelectionWidth: Math.min(availableTrackWidth,
                                                               Math.max(minimumVisualSelectionWidth,
                                                                        actualEndX - actualStartX))
        readonly property real visualStartX: Math.max(sideInset,
                                                       Math.min(width - sideInset - visualSelectionWidth,
                                                                (actualStartX + actualEndX - visualSelectionWidth) * 0.5))
        readonly property real visualEndX: visualStartX + visualSelectionWidth
        readonly property bool rangeCanShift: root.totalSeconds > root.endSeconds - root.startSeconds

        function xForSecond(second) {
            const availableWidth = Math.max(1, width - sideInset * 2)
            if (root.totalSeconds <= 0)
                return sideInset
            return sideInset + Math.max(0, Math.min(1, second / root.totalSeconds)) * availableWidth
        }

        function secondForX(x) {
            const availableWidth = Math.max(1, width - sideInset * 2)
            return Math.max(0, Math.min(1, (x - sideInset) / availableWidth)) * root.totalSeconds
        }

        function targetAt(x, y) {
            if (Math.abs(x - visualStartX) <= handleHitRadius)
                return "start"
            if (Math.abs(x - visualEndX) <= handleHitRadius)
                return "end"
            const inSelectedBody = x > visualStartX + handleHitRadius
                                && x < visualEndX - handleHitRadius
                                && y >= trackY && y <= trackY + trackHeight
            return inSelectedBody && rangeCanShift ? "range" : ""
        }

        Rectangle {
            x: lane.sideInset
            y: lane.trackY
            width: lane.availableTrackWidth
            height: lane.trackHeight
            radius: height * 0.5
            color: Theme.colors.border.control
        }

        Rectangle {
            id: rangeBody

            objectName: "exportRangeSelectedBody"
            x: lane.visualStartX
            y: lane.trackY
            width: Math.max(1, lane.visualEndX - x)
            height: lane.trackHeight
            radius: height * 0.5
            color: Theme.colors.accent.focus
        }

        Rectangle {
            id: playhead

            objectName: "exportRangePlayhead"
            x: lane.playheadLeftX
            y: 1
            width: lane.playheadWidth
            height: lane.height - 2
            antialiasing: false
            color: Theme.colors.syntax.warning

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: -1
                width: 8
                height: 6
                rotation: 45
                antialiasing: false
                color: Theme.colors.syntax.warning
            }
        }

        Item {
            id: startHandle

            objectName: "exportRangeStartHandle"
            x: lane.visualStartX - width * 0.5
            y: (lane.height - height) * 0.5
            width: lane.handleWidth
            height: lane.handleHeight

            Rectangle {
                anchors.fill: parent
                radius: 3
                color: Theme.colors.accent.primary
                border.width: root.draggingTarget === "start" ? 2 : 0
                border.color: Theme.colors.accent.soft
            }

            Rectangle {
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: parent.height - 8
                radius: 1
                color: Theme.colors.accent.soft
            }
        }

        Item {
            id: endHandle

            objectName: "exportRangeEndHandle"
            x: lane.visualEndX - width * 0.5
            y: (lane.height - height) * 0.5
            width: lane.handleWidth
            height: lane.handleHeight

            Rectangle {
                anchors.fill: parent
                radius: 3
                color: Theme.colors.accent.primary
                border.width: root.draggingTarget === "end" ? 2 : 0
                border.color: Theme.colors.accent.soft
            }

            Rectangle {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 3
                height: parent.height - 8
                radius: 1
                color: Theme.colors.accent.soft
            }
        }

        MouseArea {
            id: mouseArea

            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: root.draggingTarget === "range" ? Qt.ClosedHandCursor
                        : root.draggingTarget.length > 0 ? Qt.SizeHorCursor
                        : lane.targetAt(mouseX, mouseY) === "range" ? Qt.OpenHandCursor
                        : lane.targetAt(mouseX, mouseY).length > 0 ? Qt.SizeHorCursor
                        : Qt.ArrowCursor

            onPressed: function(mouse) {
                root.hoverSecond = lane.secondForX(mouse.x)
                const target = lane.targetAt(mouse.x, mouse.y)
                if (target.length > 0)
                    root.beginDrag(target, root.hoverSecond)
            }
            onPositionChanged: function(mouse) {
                root.hoverSecond = lane.secondForX(mouse.x)
                if (pressed && root.draggingTarget.length > 0)
                    root.updateDrag(root.hoverSecond)
            }
            onReleased: root.endDrag()
            onCanceled: root.endDrag()
        }
    }

    Text {
        anchors.left: lane.left
        anchors.top: lane.bottom
        anchors.topMargin: 3
        text: root.formatSecond(0)
        color: Theme.colors.text.disabled
        font.family: Theme.uiFont
        font.pixelSize: Theme.captionFontSize
    }

    Text {
        anchors.right: lane.right
        anchors.top: lane.bottom
        anchors.topMargin: 3
        text: root.formatSecond(root.totalSeconds)
        color: Theme.colors.text.disabled
        font.family: Theme.uiFont
        font.pixelSize: Theme.captionFontSize
    }
}
