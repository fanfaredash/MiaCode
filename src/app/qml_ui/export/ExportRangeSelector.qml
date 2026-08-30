import QtQuick
import QtQuick.Layouts
import MiaCode.UI

Item {
    id: root

    required property var exportSession
    required property var previewSession

    property string draggingEndpoint: ""

    readonly property real totalSeconds: Math.max(0, Number(exportSession.contentDurationSeconds) || 0)
    readonly property real startSeconds: Math.max(0, Math.min(totalSeconds,
                                                               Number(exportSession.exportStartSeconds) || 0))
    readonly property real endSeconds: Math.max(startSeconds, Math.min(totalSeconds,
                                                                         Number(exportSession.exportEndSeconds) || 0))
    readonly property real playheadSeconds: Math.max(0, Math.min(totalSeconds,
                                                                  Number(previewSession.positionSeconds) || 0))
    readonly property bool labelsOverlap: Math.abs(lane.xForSecond(startSeconds)
                                                    - lane.xForSecond(endSeconds))
                                        < startLabel.width + endLabel.width + 8
    readonly property real labelRowsHeight: labelsOverlap
                                             ? startLabel.implicitHeight * 2 + 6
                                             : startLabel.implicitHeight

    implicitHeight: labelRowsHeight + lane.height + 22
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

    function beginEndpointDrag(endpoint) {
        draggingEndpoint = endpoint
        previewSession.beginScrub()
    }

    function updateEndpointDrag(second) {
        if (draggingEndpoint === "start") {
            const value = Math.min(second, endSeconds)
            exportSession.exportStartSeconds = value
            previewSession.updateScrub(value)
        } else if (draggingEndpoint === "end") {
            const value = Math.max(second, startSeconds)
            exportSession.exportEndSeconds = value
            previewSession.updateScrub(value)
        }
    }

    function endEndpointDrag() {
        if (draggingEndpoint.length === 0)
            return
        const value = draggingEndpoint === "start" ? startSeconds : endSeconds
        previewSession.endScrub(value)
        draggingEndpoint = ""
    }

    Text {
        id: startLabel

        objectName: "exportRangeStartLabel"
        x: Math.max(0, Math.min(root.width - width, lane.xForSecond(root.startSeconds) - width * 0.5))
        y: 0
        text: qsTr("开始 %1").arg(root.formatSecond(root.startSeconds))
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.captionFontSize
    }

    Text {
        id: endLabel

        objectName: "exportRangeEndLabel"
        x: Math.max(0, Math.min(root.width - width, lane.xForSecond(root.endSeconds) - width * 0.5))
        y: root.labelsOverlap ? startLabel.implicitHeight + 6 : 0
        text: qsTr("结束 %1").arg(root.formatSecond(root.endSeconds))
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.captionFontSize
    }

    Item {
        id: lane

        objectName: "exportRangeLane"
        anchors.left: parent.left
        anchors.right: parent.right
        y: root.labelRowsHeight + 4
        height: 24

        readonly property real sideInset: 10
        readonly property real trackY: 9
        readonly property real trackHeight: 6
        readonly property real handleWidth: 12
        readonly property real handleHeight: 22
        readonly property real handleGrabRadius: 11

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

        function endpointAt(x) {
            const startDistance = Math.abs(x - xForSecond(root.startSeconds))
            const endDistance = Math.abs(x - xForSecond(root.endSeconds))
            if (startDistance > handleGrabRadius && endDistance > handleGrabRadius)
                return ""
            return endDistance < startDistance ? "end" : "start"
        }

        Rectangle {
            x: lane.sideInset
            y: lane.trackY
            width: Math.max(1, lane.width - lane.sideInset * 2)
            height: lane.trackHeight
            radius: height * 0.5
            color: Theme.colors.border.control
        }

        Rectangle {
            x: lane.xForSecond(root.startSeconds)
            y: lane.trackY
            width: Math.max(1, lane.xForSecond(root.endSeconds) - x)
            height: lane.trackHeight
            color: Theme.colors.accent.focus
        }

        Rectangle {
            x: lane.xForSecond(root.playheadSeconds) - width * 0.5
            y: 1
            width: 2
            height: lane.height - 2
            color: Theme.colors.syntax.warning

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: -1
                width: 8
                height: 6
                rotation: 45
                color: Theme.colors.syntax.warning
            }
        }

        Item {
            id: startHandle

            objectName: "exportRangeStartHandle"
            x: lane.xForSecond(root.startSeconds) - width * 0.5
            y: (lane.height - height) * 0.5
            width: lane.handleWidth
            height: lane.handleHeight

            Rectangle {
                anchors.fill: parent
                radius: 3
                color: Theme.colors.accent.primary
                border.width: root.draggingEndpoint === "start" ? 2 : 0
                border.color: Theme.colors.accent.soft
            }
        }

        Item {
            id: endHandle

            objectName: "exportRangeEndHandle"
            x: lane.xForSecond(root.endSeconds) - width * 0.5
            y: (lane.height - height) * 0.5
            width: lane.handleWidth
            height: lane.handleHeight

            Rectangle {
                anchors.fill: parent
                radius: 3
                color: Theme.colors.accent.primary
                border.width: root.draggingEndpoint === "end" ? 2 : 0
                border.color: Theme.colors.accent.soft
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            preventStealing: true
            cursorShape: root.draggingEndpoint.length > 0 || lane.endpointAt(mouseX).length > 0
                         ? Qt.SizeHorCursor : Qt.ArrowCursor

            onPressed: {
                const endpoint = lane.endpointAt(mouse.x)
                if (endpoint.length > 0)
                    root.beginEndpointDrag(endpoint)
            }
            onPositionChanged: {
                if (pressed && root.draggingEndpoint.length > 0)
                    root.updateEndpointDrag(lane.secondForX(mouse.x))
            }
            onReleased: root.endEndpointDrag()
            onCanceled: root.endEndpointDrag()
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
