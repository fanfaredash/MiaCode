import QtQuick
import QtQuick.Controls

Slider {
    id: root

    property var paletteMap: ({})
    property var metricsMap: ({})
    property bool fullscreenMode: false
    property real displayedProgress: position
    property int handleDiameter: 18
    property int grooveHeight: 6

    focusPolicy: Qt.NoFocus

    Theme {
        id: shellTheme
        paletteMap: root.paletteMap
        metricsMap: root.metricsMap
        fullscreenMode: root.fullscreenMode
    }

    background: Item {
        x: root.leftPadding
        y: Math.round((parent.height - root.grooveHeight) / 2)
        width: root.availableWidth
        height: root.grooveHeight

        Rectangle {
            anchors.fill: parent
            radius: height / 2
            color: shellTheme.transportTrackColor()
        }

        Rectangle {
            width: Math.max(root.grooveHeight, root.displayedProgress * parent.width)
            height: parent.height
            radius: height / 2
            color: shellTheme.tone("accent", "#2e77d0")
        }
    }

    handle: Rectangle {
        x: root.leftPadding + root.displayedProgress * (root.availableWidth - width)
        y: Math.round((root.height - height) / 2)
        width: root.handleDiameter
        height: root.handleDiameter
        radius: root.handleDiameter / 2
        color: shellTheme.transportHandleFillColor()
        border.color: shellTheme.transportHandleBorderColor()
        border.width: 1
    }
}
