import QtQuick
import QtQuick.Window
import MiaCode.UI

// Restore the backing pixels outside the arc in a corner-sized pass. The
// editor and live preview remain in the main scene, outside any texture layer.
ShaderEffect {
    id: root

    required property Item backgroundSource
    required property point backgroundOffset
    property real radius: Theme.workspaceRadius
    readonly property color baseColor: Theme.colors.background.surface
    readonly property color surfaceColor: Theme.surfaceColor(baseColor)
    readonly property var source: ShaderEffectSource {
        sourceItem: root.visible ? root.backgroundSource : null
        sourceRect: Qt.rect(root.backgroundOffset.x, root.backgroundOffset.y,
                            root.width, root.height)
        textureSize: Qt.size(Math.ceil(root.width * root.Screen.devicePixelRatio),
                             Math.ceil(root.height * root.Screen.devicePixelRatio))
        visible: false
    }

    width: radius
    height: radius
    enabled: false
    fragmentShader: "qrc:/src/app/qml_ui/shaders/corner_mask.frag.qsb"
}
