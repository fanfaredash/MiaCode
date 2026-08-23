import QtQuick
import MiaCode.UI
import "qrc:/quick_shell/qml" as Shell

Rectangle {
    id: root

    required property var previewSession
    required property var shellController
    signal fullscreenRequested()

    // Mirror QuickShellMain: backend aspect is authoritative (export page
    // widens the canvas; chart/latency stay 1:1). Prefer width/height >= 1.
    readonly property real canvasAspectRatio: {
        const ratio = root.shellController && root.shellController.previewCanvasAspectRatio !== undefined
                      ? root.shellController.previewCanvasAspectRatio
                      : 1.0
        return Math.max(1.0, ratio)
    }

    color: Theme.colors.background.surface
    clip: true

    function fittedFrameWidth(hostWidth, hostHeight) {
        const safeWidth = Math.max(1, hostWidth)
        const safeHeight = Math.max(1, hostHeight)
        return Math.max(1, Math.min(safeWidth, safeHeight * root.canvasAspectRatio))
    }

    function fittedFrameHeight(hostWidth, hostHeight) {
        const frameWidth = fittedFrameWidth(hostWidth, hostHeight)
        return Math.max(1, Math.min(hostHeight, frameWidth / root.canvasAspectRatio))
    }

    PanelHeader {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: qsTr("预览")
    }

    Item {
        id: stageArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: transport.top
        clip: true

        Shell.QuickShellPreviewSurface {
            anchors.centerIn: parent
            width: root.fittedFrameWidth(parent.width, parent.height)
            height: root.fittedFrameHeight(parent.width, parent.height)
            runtime: root.previewSession.runtime
            mediaHost: root.previewSession.mediaHost
            logger: root.shellController
            surfaceRole: "workspace"
        }
    }

    PreviewTransport {
        id: transport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statistics.top
        previewSession: root.previewSession
        shellController: root.shellController
        onFullscreenRequested: root.fullscreenRequested()
    }

    NoteStatistics {
        id: statistics
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        statistics: root.previewSession.statistics
    }
}
