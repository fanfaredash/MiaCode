import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared slider — geometry mirrors v1 formSliderStyleSheet.
// Control 会把 background 拉到整颗滑条的尺寸，轨道必须画在内层，不能写在
// background 根上，否则轨道会变成整块色条。
Slider {
    id: root

    property bool rangeMarkersVisible: false
    property bool rangeHighlightVisible: false
    property real rangeStartValue: 0
    property real rangeEndValue: 0

    function positionForValue(value) {
        if (root.to <= root.from)
            return root.handle.width / 2
        const fraction = Math.max(0, Math.min(1,
            (value - root.from) / (root.to - root.from)))
        return root.handle.width / 2
            + fraction * Math.max(0, root.availableWidth - root.handle.width)
    }

    function rangeEdgeX(value) {
        if (value <= root.from)
            return 0
        if (value >= root.to)
            return track.width
        return positionForValue(value)
    }

    hoverEnabled: true
    implicitHeight: 24
    padding: 0

    background: Item {
        implicitWidth: 200
        implicitHeight: 24

        Rectangle {
            id: track
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            height: 6
            radius: 3
            color: Theme.overlayColor(Theme.colors.border.control)

            Rectangle {
                visible: !root.rangeHighlightVisible
                width: root.visualPosition * parent.width
                height: parent.height
                radius: 3
                color: Theme.colors.accent.primary
            }

            Rectangle {
                visible: root.rangeHighlightVisible && root.to > root.from
                x: root.rangeEdgeX(root.rangeStartValue)
                width: Math.max(0, root.rangeEdgeX(root.rangeEndValue) - x)
                height: parent.height
                radius: parent.radius
                color: Theme.colors.state.followHighlight
            }

            Rectangle {
                visible: root.rangeHighlightVisible && root.to > root.from
                x: root.rangeEdgeX(root.rangeStartValue)
                width: Math.max(0, root.rangeEdgeX(Math.max(root.rangeStartValue,
                    Math.min(root.value, root.rangeEndValue))) - x)
                height: parent.height
                radius: parent.radius
                color: Theme.colors.accent.primary
            }
        }

        Rectangle {
            visible: root.rangeMarkersVisible && root.to > root.from
            x: root.positionForValue(root.rangeStartValue) - width / 2
            y: track.y - height
            width: 2
            height: 4
            radius: 1
            color: Theme.colors.accent.primary
        }

        Rectangle {
            visible: root.rangeMarkersVisible && root.to > root.from
            x: root.positionForValue(root.rangeEndValue) - width / 2
            y: track.y - height
            width: 2
            height: 4
            radius: 1
            color: Theme.colors.accent.primary
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.visualPosition * (root.availableWidth - width)
        y: root.topPadding + root.availableHeight / 2 - height / 2
        implicitWidth: 14
        implicitHeight: 14
        width: 14
        height: 14
        radius: 7
        color: Theme.overlayColor(Theme.colors.background.elevated)
        border.width: Theme.controlBorderWidth
        border.color: (root.pressed || root.hovered)
                      ? Theme.colors.accent.primary
                      : Theme.colors.border.control
    }
}
