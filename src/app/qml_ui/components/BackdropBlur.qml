import QtQuick
import QtQuick.Controls
import QtQuick.Templates as T
import QtQuick.Effects
import QtQuick.Window
import MiaCode.UI

Item {
    id: root

    required property Item sourceItem
    required property T.Popup popup
    property int blurRadius: Theme.popupBlurRadius
    readonly property real padding: blurRadius
    readonly property real sampleWidth: width + 2 * padding
    readonly property real sampleHeight: height + 2 * padding
    // A dropdown in a dialog also samples that dialog, which is a sibling of
    // the dropdown in Overlay. The current popup never enters its own source.
    readonly property Item overlaySource: {
        const overlay = root.popup.Overlay.overlay
        let item = root.popup.parent
        while (item && item !== overlay) {
            if (item.parent === overlay)
                return item
            item = item.parent
        }
        return null
    }
    property rect sceneRect
    property rect overlayRect

    function sampleRect(item) {
        const p = root.mapToItem(item, -padding, -padding)
        return Qt.rect(p.x, p.y, sampleWidth, sampleHeight)
    }

    function updateSourceRects() {
        sceneRect = sampleRect(sourceItem)
        if (overlaySource)
            overlayRect = sampleRect(overlaySource)
    }

    Component.onCompleted: updateSourceRects()
    // Follow anchor movement and final popup placement on frames the window
    // already renders. Unchanged rects keep the source textures clean.
    Connections {
        target: root.Window.window
        function onAfterAnimating() { root.updateSourceRects() }
    }

    Rectangle {
        id: capture
        width: Math.ceil(root.sampleWidth * Theme.popupBlurScale)
        height: Math.ceil(root.sampleHeight * Theme.popupBlurScale)
        visible: false
        color: Theme.colors.background.surface

        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.sourceItem
            sourceRect: root.sceneRect
            textureSize: Qt.size(capture.width, capture.height)
        }
        ShaderEffectSource {
            anchors.fill: parent
            sourceItem: root.overlaySource
            sourceRect: root.overlayRect
            textureSize: Qt.size(capture.width, capture.height)
        }
    }

    ShaderEffectSource {
        id: sample
        width: capture.width
        height: capture.height
        sourceItem: capture
        textureSize: Qt.size(capture.width, capture.height)
        visible: false
    }

    Item {
        id: mask
        width: root.sampleWidth
        height: root.sampleHeight
        visible: false
        layer.enabled: true
        Rectangle {
            x: root.padding
            y: root.padding
            width: root.width
            height: root.height
            radius: Theme.popupRadius
            color: "white"
        }
    }

    MultiEffect {
        x: -root.padding
        y: -root.padding
        width: root.sampleWidth
        height: root.sampleHeight
        source: sample
        autoPaddingEnabled: false
        blurEnabled: true
        blurMax: root.blurRadius * Theme.popupBlurScale
        blur: 1.0
        maskEnabled: true
        maskSource: mask
        maskThresholdMin: 0.5
        maskSpreadAtMin: 1.0
    }
}
