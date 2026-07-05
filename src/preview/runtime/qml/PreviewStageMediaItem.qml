import QtQuick
import QtMultimedia
import QtQuick.Effects

Item {
    id: root
    property var mediaHost: null
    property alias videoOutputObject: previewStageVideoOutput
    property alias innerVideoOutputObject: previewStageInnerVideoOutput

    objectName: "previewStageMediaItem"
    anchors.fill: parent
    readonly property int scaleMode: root.mediaHost ? root.mediaHost.backgroundScaleMode : 0
    readonly property bool innerCircleFitOuterFill: scaleMode === 3
    readonly property bool squareFit: scaleMode === 2
    readonly property bool fitContain: scaleMode === 1 || scaleMode === 2
    readonly property real innerCircleSide: Math.max(
        1,
        root.height * (root.mediaHost ? root.mediaHost.layoutSquareScale : 0.95)
    )

    Rectangle {
        anchors.fill: parent
        color: root.mediaHost && root.mediaHost.hasResolvedMedia ? "#000000" : "transparent"
        visible: root.mediaHost && root.mediaHost.mediaVisible && root.mediaHost.hasResolvedMedia
    }

    Item {
        id: mediaFrame
        objectName: "previewStageMediaFrame"
        width: root.squareFit ? Math.min(root.width, root.height) : root.width
        height: root.squareFit ? Math.min(root.width, root.height) : root.height
        anchors.centerIn: parent
        clip: root.squareFit

        Image {
            id: previewStageImage
            objectName: "previewStageImage"
            anchors.fill: parent
            visible: root.mediaHost
                && root.mediaHost.mediaVisible
                && root.mediaHost.hasResolvedMedia
                && !root.mediaHost.hasVideoMedia
            fillMode: root.fitContain ? Image.PreserveAspectFit : Image.PreserveAspectCrop
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
            fillMode: root.fitContain ? VideoOutput.PreserveAspectFit : VideoOutput.PreserveAspectCrop
        }
    }

    Item {
        id: innerCircleFitSource
        width: root.innerCircleSide
        height: root.innerCircleSide
        visible: root.innerCircleFitOuterFill
            && root.mediaHost
            && root.mediaHost.mediaVisible
            && root.mediaHost.hasResolvedMedia

        Image {
            anchors.fill: parent
            visible: innerCircleFitSource.visible
                && root.mediaHost
                && !root.mediaHost.hasVideoMedia
            fillMode: Image.PreserveAspectFit
            source: root.mediaHost ? root.mediaHost.imageSource : ""
            asynchronous: true
            cache: true
            smooth: true
            mipmap: true
        }

        VideoOutput {
            id: previewStageInnerVideoOutput
            objectName: "previewStageInnerVideoOutput"
            anchors.fill: parent
            visible: innerCircleFitSource.visible
                && root.mediaHost
                && root.mediaHost.hasVideoMedia
                && root.mediaHost.hasVideoFrame
            fillMode: VideoOutput.PreserveAspectFit
        }
    }

    Item {
        id: innerCircleMaskSource
        width: root.innerCircleSide
        height: root.innerCircleSide
        visible: root.innerCircleFitOuterFill

        Rectangle {
            anchors.fill: parent
            radius: width / 2
            antialiasing: true
            color: "#FFFFFF"
        }
    }

    ShaderEffectSource {
        id: innerCircleFitTexture
        sourceItem: innerCircleFitSource
        hideSource: true
        live: root.innerCircleFitOuterFill
        visible: false
    }

    ShaderEffectSource {
        id: innerCircleMaskTexture
        sourceItem: innerCircleMaskSource
        hideSource: true
        live: root.innerCircleFitOuterFill
        visible: false
    }

    MultiEffect {
        anchors.centerIn: parent
        width: root.innerCircleSide
        height: root.innerCircleSide
        visible: root.innerCircleFitOuterFill
            && root.mediaHost
            && root.mediaHost.mediaVisible
            && root.mediaHost.hasResolvedMedia
        source: innerCircleFitTexture
        maskEnabled: true
        maskSource: innerCircleMaskTexture
        maskThresholdMin: 0.5
    }
}
