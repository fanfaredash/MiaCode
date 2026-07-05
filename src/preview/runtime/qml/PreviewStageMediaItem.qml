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
        visible: root.mediaHost && root.mediaHost.mediaVisible && root.mediaHost.hasResolvedMedia
    }

    Item {
        id: mediaFrame
        objectName: "previewStageMediaFrame"
        property bool squareFit: root.mediaHost
            && (root.mediaHost.backgroundScaleMode === 2 || root.mediaHost.backgroundScaleMode === 3)
        width: squareFit ? Math.min(root.width, root.height) : root.width
        height: squareFit ? Math.min(root.width, root.height) : root.height
        anchors.centerIn: parent
        clip: squareFit

        Image {
            id: previewStageImage
            objectName: "previewStageImage"
            anchors.fill: parent
            visible: root.mediaHost
                && root.mediaHost.mediaVisible
                && root.mediaHost.hasResolvedMedia
                && !root.mediaHost.hasVideoMedia
            fillMode: root.mediaHost
                && (root.mediaHost.backgroundScaleMode === 1
                    || root.mediaHost.backgroundScaleMode === 2
                    || root.mediaHost.backgroundScaleMode === 3)
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
            visible: root.mediaHost
                && root.mediaHost.mediaVisible
                && root.mediaHost.hasVideoMedia
                && root.mediaHost.hasVideoFrame
            fillMode: root.mediaHost
                && (root.mediaHost.backgroundScaleMode === 1
                    || root.mediaHost.backgroundScaleMode === 2
                    || root.mediaHost.backgroundScaleMode === 3)
                ? VideoOutput.PreserveAspectFit
                : VideoOutput.PreserveAspectCrop
        }
    }
}
