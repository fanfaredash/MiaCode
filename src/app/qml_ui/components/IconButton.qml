import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

AbstractButton {
    id: root

    property url iconSource
    property string tooltip
    property bool active: false
    property int iconWidth: 16
    property int iconHeight: 16

    implicitWidth: 28
    implicitHeight: 27
    hoverEnabled: true

    contentItem: ControlsImpl.IconImage {
        anchors.centerIn: parent
        width: root.iconWidth
        height: root.iconHeight
        source: root.iconSource
        sourceSize: Qt.size(root.iconWidth, root.iconHeight)
        color: Theme.colors.text.secondary
        opacity: root.enabled ? 1.0 : 0.42
    }

    background: Rectangle {
        color: root.down ? Theme.colors.state.pressed
              : root.hovered ? Theme.colors.state.hover
              : "transparent"
        radius: 4
    }

    Tooltip {
        visible: root.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
}

