import QtQuick
import QtMultimedia

Item {
    id: root
    property var mediaHost: null
    property alias videoOutputObject: previewStageVideoOutput

    objectName: "previewStageMediaItem"
    anchors.fill: parent

    Rectangle {
        anchors.fill: parent
        color: root.mediaHost && root.mediaHost.hasResolvedMedia ? "#000000" : "transparent"
        visible: root.mediaHost && root.mediaHost.hasResolvedMedia
    }

    Image {
        id: previewStageImage
        objectName: "previewStageImage"
        anchors.fill: parent
        visible: root.mediaHost && root.mediaHost.hasResolvedMedia && !root.mediaHost.hasVideoMedia
        fillMode: root.mediaHost && root.mediaHost.backgroundScaleMode === 1
            ? Image.PreserveAspectFit
            : Image.PreserveAspectCrop
        source: root.mediaHost ? root.mediaHost.imageSource : ""
        asynchronous: true
        cache: true
        smooth: true
        mipmap: true
    }

    VideoOutput {
        id: previewStageVideoOutput
        objectName: "previewStageVideoOutput"
        anchors.fill: parent
        visible: root.mediaHost && root.mediaHost.hasVideoMedia && root.mediaHost.hasVideoFrame
        fillMode: root.mediaHost && root.mediaHost.backgroundScaleMode === 1
            ? VideoOutput.PreserveAspectFit
            : VideoOutput.PreserveAspectCrop
    }
}
