pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import MiaCode.UI

Row {
    id: root

    required property var hostWindow

    height: parent ? parent.height : 34

    CaptionButton {
        glyph: "\uE921"
        accessibleName: qsTr("最小化")
        onClicked: root.hostWindow.showMinimized()
    }

    CaptionButton {
        glyph: root.hostWindow.visibility === Window.Maximized ? "\uE923" : "\uE922"
        accessibleName: root.hostWindow.visibility === Window.Maximized
                        ? qsTr("还原") : qsTr("最大化")
        onClicked: {
            if (root.hostWindow.visibility === Window.Maximized)
                root.hostWindow.showNormal()
            else
                root.hostWindow.showMaximized()
        }
    }

    CaptionButton {
        glyph: "\uE8BB"
        accessibleName: qsTr("关闭")
        closeButton: true
        onClicked: root.hostWindow.close()
    }

    component CaptionButton: AbstractButton {
        id: button

        required property string glyph
        required property string accessibleName
        property bool closeButton: false

        width: 46
        height: root.height
        hoverEnabled: true
        Accessible.name: accessibleName

        contentItem: Text {
            text: button.glyph
            color: button.closeButton && button.hovered ? "#FFFFFF" : Theme.colors.text.primary
            font.family: "Segoe Fluent Icons"
            font.pixelSize: 10
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Rectangle {
            color: {
                if (button.closeButton) {
                    if (button.down)
                        return "#A92317"
                    if (button.hovered)
                        return "#C42B1C"
                    return "transparent"
                }
                if (button.down)
                    return Theme.colors.state.pressed
                if (button.hovered)
                    return Theme.colors.state.hover
                return "transparent"
            }
        }
    }
}

