import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

AbstractButton {
    id: root

    property url iconSource
    // Optional text glyph (e.g. "+" / "−") when no iconSource is set.
    property string glyph: ""
    property string tooltip
    property bool active: false
    property int iconWidth: 16
    property int iconHeight: 16

    implicitWidth: glyph.length > 0 ? 24 : 28
    implicitHeight: glyph.length > 0 ? 24 : 27
    hoverEnabled: true

    readonly property color glyphColor: !root.enabled ? Theme.colors.text.disabled
                                       : (root.active || root.hovered) ? Theme.colors.text.active
                                       : Theme.colors.text.secondary

    contentItem: Item {
        anchors.fill: parent

        ControlsImpl.IconImage {
            anchors.centerIn: parent
            width: root.iconWidth
            height: root.iconHeight
            visible: root.glyph.length === 0
            source: root.iconSource
            sourceSize: Qt.size(root.iconWidth, root.iconHeight)
            color: root.glyphColor
        }

        Text {
            anchors.centerIn: parent
            visible: root.glyph.length > 0
            text: root.glyph
            color: root.glyphColor
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize + 2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: HoverChrome {
        hovered: root.hovered
        pressed: root.down
        tone: "icon"
    }

    Tooltip {
        visible: root.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
}
