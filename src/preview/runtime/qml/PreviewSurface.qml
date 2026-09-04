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
    property color backgroundColor: "#191A1B"
    property color hudTextColor: "#FFFFFF"
    property color hudShadowColor: "#BE000000"
    property color mediaHudTextColor: "#FFFFFF"
    property color mediaHudShadowColor: "#BE000000"
    property var attachedMediaHost: null
    property var attachedVideoOutputObject: null
    property var attachedInnerVideoOutputObject: null
    readonly property string instanceTag: surfaceRole + ":" + Math.round(Math.random() * 1000000000)
    readonly property var hostWindow: root.Window.window ? root.Window.window : null
    readonly property bool stageMediaVisible: root.mediaHost
        && root.mediaHost.mediaVisible
        && root.mediaHost.hasResolvedMedia

    function surfaceGeometryPayload() {
        let payload = "item_x=" + x
            + " item_y=" + y
            + " item_width=" + width
            + " item_height=" + height
        if (parent) {
            payload += " parent_width=" + parent.width
            payload += " parent_height=" + parent.height
        }
        if (hostWindow && hostWindow.contentItem) {
            const sceneTopLeft = root.mapToItem(hostWindow.contentItem, 0, 0)
            payload += " scene_x=" + sceneTopLeft.x
            payload += " scene_y=" + sceneTopLeft.y
        }
        return payload
    }

    function logSurface(action, extra) {
        if (!logger)
            return
        let payload = "role=" + surfaceRole
            + " instance=" + instanceTag
            + " visible=" + (visible ? 1 : 0)
            + " has_runtime=" + (runtime ? 1 : 0)
            + " has_media=" + (mediaHost ? 1 : 0)
        if (hostWindow) {
            payload += " window_visible=" + (hostWindow.visible ? 1 : 0)
            payload += " window_width=" + hostWindow.width
            payload += " window_height=" + hostWindow.height
        }
        if (extra && extra.length > 0)
            payload += " " + extra
        logger.logPreviewInteraction(action, payload)
    }

    function syncVideoOutputBinding() {
        const videoOutput = previewStageMedia.videoOutputObject
        const innerVideoOutput = previewStageMedia.innerVideoOutputObject
        const nextHost = mediaHost
        const hasStableGeometry = width >= 64 && height >= 64
        const shouldAttach = visible && hasStableGeometry && nextHost && videoOutput && hostWindow && hostWindow.visible

        if (attachedMediaHost && (!shouldAttach || attachedMediaHost !== nextHost)) {
            if (attachedInnerVideoOutputObject && attachedMediaHost.detachVideoOutputObjects)
                attachedMediaHost.detachVideoOutputObjects(attachedVideoOutputObject, attachedInnerVideoOutputObject)
            else
                attachedMediaHost.detachVideoOutputObject(attachedVideoOutputObject)
            logSurface(
                "preview_surface_video_output_detach",
                "has_video_output=" + (attachedVideoOutputObject ? 1 : 0)
                    + " has_inner_video_output=" + (attachedInnerVideoOutputObject ? 1 : 0)
                    + " stable_geometry=" + (hasStableGeometry ? 1 : 0)
            )
            attachedMediaHost = null
            attachedVideoOutputObject = null
            attachedInnerVideoOutputObject = null
        }

        if (shouldAttach
                && (attachedMediaHost !== nextHost
                    || attachedVideoOutputObject !== videoOutput
                    || attachedInnerVideoOutputObject !== innerVideoOutput)) {
            if (innerVideoOutput && nextHost.attachVideoOutputObjects)
                nextHost.attachVideoOutputObjects(videoOutput, innerVideoOutput)
            else
                nextHost.attachVideoOutputObject(videoOutput)
            attachedMediaHost = nextHost
            attachedVideoOutputObject = videoOutput
            attachedInnerVideoOutputObject = innerVideoOutput
            logSurface(
                "preview_surface_video_output_attach",
                "has_video_output=" + (videoOutput ? 1 : 0)
                    + " has_inner_video_output=" + (innerVideoOutput ? 1 : 0)
                    + " stable_geometry=" + (hasStableGeometry ? 1 : 0)
            )
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
    onHostWindowChanged: {
        syncVideoOutputBinding()
        logSurface("preview_surface_host_window_changed")
    }
    onRuntimeChanged: logSurface("preview_surface_runtime_changed")
    onXChanged: geometryLogTimer.restart()
    onYChanged: geometryLogTimer.restart()
    onWidthChanged: geometryLogTimer.restart()
    onHeightChanged: geometryLogTimer.restart()
    Component.onCompleted: {
        syncVideoOutputBinding()
        geometryLogTimer.restart()
        logSurface("preview_surface_created", surfaceGeometryPayload())
    }
    Component.onDestruction: {
        logSurface("preview_surface_destroyed")
        if (attachedMediaHost && attachedVideoOutputObject) {
            if (attachedInnerVideoOutputObject && attachedMediaHost.detachVideoOutputObjects)
                attachedMediaHost.detachVideoOutputObjects(attachedVideoOutputObject, attachedInnerVideoOutputObject)
            else
                attachedMediaHost.detachVideoOutputObject(attachedVideoOutputObject)
            attachedMediaHost = null
            attachedVideoOutputObject = null
            attachedInnerVideoOutputObject = null
        }
    }

    Timer {
        id: geometryLogTimer
        interval: 0
        repeat: false
        onTriggered: {
            root.syncVideoOutputBinding()
            root.logSurface("preview_surface_geometry_changed", root.surfaceGeometryPayload())
        }
    }

    Connections {
        target: root.hostWindow

        function onVisibleChanged() {
            root.syncVideoOutputBinding()
            root.logSurface("preview_surface_window_visible_changed")
        }
    }

    PreviewRuntimeQml.PreviewStageMediaItem {
        id: previewStageMedia
        anchors.fill: parent
        z: 0
        mediaHost: root.mediaHost
        // NOTE — keep visible:true. See the comment above
        // PreviewQuickSceneRoot for why hiding QML items in this
        // surface breaks Qt's lazy present cadence. The internal
        // Image/VideoOutput children render against
        // mediaHost.mediaVisible already.
    }

    // NOTE — keep these QQuickItems visible:true. Hiding them sounds
    // like a perf win (skip the QSG scene graph walk) but it broke
    // Qt's lazy present cadence: with the canvas's three big visual
    // items all invisible, the QSG had nothing dirty to push and
    // frameSwapped fired at ~28 Hz instead of 60 Hz. The runtime's
    // tick is gated by frameSwapped (present-driven pacing), so the
    // playhead stalled and chart sprites stopped advancing — the
    // symptom was a "frozen" preview.
    PreviewQuickSceneRoot {
        anchors.fill: parent
        z: 1
        runtime: root.runtime
        backgroundColor: root.backgroundColor
    }

    PreviewQuickHudLayer {
        anchors.fill: parent
        z: 2
        runtime: root.runtime
        textColor: root.stageMediaVisible ? root.mediaHudTextColor : root.hudTextColor
        shadowColor: root.stageMediaVisible ? root.mediaHudShadowColor : root.hudShadowColor
    }

    Item {
        id: introOverlayLayer
        anchors.fill: parent
        z: 3
        clip: true
        visible: root.runtime && root.runtime.introOverlayActive

        function syncIntroOverlayData() {
            if (!introOverlayLoader.item || !root.runtime)
                return
            introOverlayLoader.item.bannerTrack = root.runtime.introBannerTrack
            introOverlayLoader.item.bannerTemplateData = root.runtime.introBannerTemplate
            introOverlayLoader.item.backgroundImage = root.runtime.introBackgroundImage
            introOverlayLoader.item.logoImage = root.runtime.introLogoImage
            // "片头" tab styling (backdropImage / backdropBlurEnabled /
            // cardShadowEnabled), applied key-by-key like the export mount so the
            // 背景虚化/自定义背景/卡片阴影 toggles take effect live in the preview.
            var style = root.runtime.introBannerStyle
            if (style) {
                for (var k in style)
                    introOverlayLoader.item[k] = style[k]
            }
        }

        function syncIntroOverlayFrame() {
            if (!introOverlayLoader.item || !root.runtime)
                return
            introOverlayLoader.item.frame = root.runtime.introOverlayFrame
        }

        Loader {
            id: introOverlayLoader
            active: introOverlayLayer.visible
            source: "qrc:/intro/qml/IntroOverlay.qml"
            width: 1920
            height: 1080
            transformOrigin: Item.Center
            scale: Math.max(introOverlayLayer.width / width, introOverlayLayer.height / height)
            x: introOverlayLayer.width / 2 - width / 2
            y: introOverlayLayer.height / 2 - height / 2
            onLoaded: {
                introOverlayLayer.syncIntroOverlayData()
                introOverlayLayer.syncIntroOverlayFrame()
            }
        }

        Connections {
            target: root.runtime

            function onIntroOverlayDataChanged() {
                introOverlayLayer.syncIntroOverlayData()
            }

            function onIntroOverlayStateChanged() {
                introOverlayLayer.syncIntroOverlayFrame()
            }
        }
    }
}
