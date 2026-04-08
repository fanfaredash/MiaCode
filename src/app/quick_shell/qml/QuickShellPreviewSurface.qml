import QtQuick
import "qrc:/preview/runtime/qml" as PreviewRuntimeQml
import MiaCode.Preview

Item {
    id: root

    property var runtime: null
    property var mediaHost: null
    property var attachedMediaHost: null
    property var attachedVideoOutputObject: null

    function syncVideoOutputBinding() {
        const videoOutput = previewStageMedia.videoOutputObject
        const nextHost = mediaHost
        const shouldAttach = visible && nextHost && videoOutput

        if (attachedMediaHost && (!shouldAttach || attachedMediaHost !== nextHost)) {
            attachedMediaHost.detachVideoOutputObject(attachedVideoOutputObject)
            attachedMediaHost = null
            attachedVideoOutputObject = null
        }

        if (shouldAttach && attachedMediaHost !== nextHost) {
            nextHost.attachVideoOutputObject(videoOutput)
            attachedMediaHost = nextHost
            attachedVideoOutputObject = videoOutput
        }
    }

    onVisibleChanged: syncVideoOutputBinding()
    onMediaHostChanged: syncVideoOutputBinding()
    Component.onCompleted: syncVideoOutputBinding()
    Component.onDestruction: {
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
