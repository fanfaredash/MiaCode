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
        accessibleName: UiText.text("最小化")
        onClicked: root.hostWindow.showMinimized()
    }

    CaptionButton {
        glyph: root.hostWindow.visibility === Window.Maximized ? "\uE923" : "\uE922"
        // 还原 here means "restore the window", not the 还原 the HUD-font and
        // export pages use for "reset". The Chinese source is the same string,
        // so this one call site passes the key explicitly rather than relying on
        // the shared source-to-key table.
        accessibleName: root.hostWindow.visibility === Window.Maximized
                        ? UiText.text("window.restore") : UiText.text("最大化")
        onClicked: {
            if (root.hostWindow.visibility === Window.Maximized)
                root.hostWindow.showNormal()
            else
                root.hostWindow.showMaximized()
        }
    }

    CaptionButton {
        glyph: "\uE8BB"
        accessibleName: UiText.text("关闭")
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
            color: button.closeButton && button.hovered ? "#FFFFFF"
                   : (button.hovered ? Theme.colors.text.active : Theme.colors.text.secondary)
            font.family: "Segoe Fluent Icons"
            font.pixelSize: 10
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }

        background: Item {
            HoverChrome {
                anchors.fill: parent
                visible: !button.closeButton
                hovered: button.hovered
                pressed: button.down
                tone: "icon"
            }
            Rectangle {
                anchors.fill: parent
                visible: button.closeButton
                color: {
                    if (button.down)
                        return "#A92317"
                    if (button.hovered)
                        return "#C42B1C"
                    return "transparent"
                }
            }
        }
    }
}
