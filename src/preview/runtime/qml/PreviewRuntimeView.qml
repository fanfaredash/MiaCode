import QtQuick
import MiaCode.Preview

Rectangle {
    id: root

    property var runtime: null

    color: "#191A1B"
    implicitWidth: 480
    implicitHeight: 480

    PreviewQuickSceneRoot {
        id: previewQuickSceneRoot
        objectName: "previewQuickSceneRoot"
        anchors.fill: parent
        z: 1
        runtime: root.runtime
    }

    PreviewQuickHudLayer {
        id: previewQuickHudLayer
        objectName: "previewQuickHudLayer"
        anchors.fill: parent
        z: 2
        runtime: root.runtime
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
