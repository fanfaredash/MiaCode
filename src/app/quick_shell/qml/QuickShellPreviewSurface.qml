import QtQuick
import QtQuick.Window
import "qrc:/preview/runtime/qml" as PreviewRuntimeQml
import MiaCode.Preview

Item {
    id: root

    property var runtime: null
    property var mediaHost: null
    property var logger: null
    property string surfaceRole: "unknown"
    property var attachedMediaHost: null
    property var attachedVideoOutputObject: null
    readonly property string instanceTag: surfaceRole + ":" + Math.round(Math.random() * 1000000000)

    function logSurface(action, extra) {
        if (!logger)
            return
        let payload = "role=" + surfaceRole
            + " instance=" + instanceTag
            + " visible=" + (visible ? 1 : 0)
            + " has_runtime=" + (runtime ? 1 : 0)
            + " has_media=" + (mediaHost ? 1 : 0)
        if (window) {
            payload += " window_visible=" + (window.visible ? 1 : 0)
            payload += " window_width=" + window.width
            payload += " window_height=" + window.height
        }
        if (extra && extra.length > 0)
            payload += " " + extra
        logger.logPreviewInteraction(action, payload)
    }

    function syncVideoOutputBinding() {
        const videoOutput = previewStageMedia.videoOutputObject
        const nextHost = mediaHost
        const shouldAttach = visible && nextHost && videoOutput

        if (attachedMediaHost && (!shouldAttach || attachedMediaHost !== nextHost)) {
            attachedMediaHost.detachVideoOutputObject(attachedVideoOutputObject)
            logSurface("preview_surface_video_output_detach", "has_video_output=" + (attachedVideoOutputObject ? 1 : 0))
            attachedMediaHost = null
            attachedVideoOutputObject = null
        }

        if (shouldAttach && attachedMediaHost !== nextHost) {
            nextHost.attachVideoOutputObject(videoOutput)
            attachedMediaHost = nextHost
            attachedVideoOutputObject = videoOutput
            logSurface("preview_surface_video_output_attach", "has_video_output=" + (videoOutput ? 1 : 0))
        }
    }

    onVisibleChanged: {
        syncVideoOutputBinding()
        logSurface("preview_surface_visible_changed")
    }
    onMediaHostChanged: {
        syncVideoOutputBinding()
        logSurface("preview_surface_media_host_changed")
    }
    onRuntimeChanged: logSurface("preview_surface_runtime_changed")
    Component.onCompleted: {
        syncVideoOutputBinding()
        logSurface("preview_surface_created")
    }
    Component.onDestruction: {
        logSurface("preview_surface_destroyed")
        if (attachedMediaHost && attachedVideoOutputObject) {
            attachedMediaHost.detachVideoOutputObject(attachedVideoOutputObject)
        }
    }

    PreviewRuntimeQml.PreviewStageMediaItem {
        id: previewStageMedia
        anchors.fill: parent
        z: 0
        mediaHost: root.mediaHost
    }

    PreviewQuickSceneRoot {
        anchors.fill: parent
        z: 1
        runtime: root.runtime
    }

    PreviewQuickHudLayer {
        anchors.fill: parent
        z: 2
        runtime: root.runtime
    }
}
