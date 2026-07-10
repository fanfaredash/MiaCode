import QtQuick
import QtQuick.Controls

ToolButton {
    id: root

    property var paletteMap: ({})
    property var metricsMap: ({})
    property bool fullscreenMode: false
    property int shellRadius: 6

    padding: 0
    focusPolicy: Qt.NoFocus
    hoverEnabled: true

    Theme {
        id: shellTheme
        paletteMap: root.paletteMap
        metricsMap: root.metricsMap
        fullscreenMode: root.fullscreenMode
    }

    background: Rectangle {
        color: shellTheme.transportButtonFillColor(root.down || root.checked)
        border.color: shellTheme.transportButtonBorderColor()
        radius: root.shellRadius
    }
}
