pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Shapes
import MiaCode.UI

Row {
    id: root

    required property var hostWindow

    height: parent ? parent.height : 34

    CaptionButton {
        buttonType: "minimize"
        accessibleName: qsTrId("qml.minimize")
        onClicked: root.hostWindow.showMinimized()
    }

    CaptionButton {
        buttonType: root.hostWindow.visibility === Window.Maximized ? "restore" : "maximize"
        // 还原 here means "restore the window", not the 还原 the HUD-font and
        // export pages use for "reset". The Chinese source is the same string,
        // so this one call site passes the key explicitly rather than relying on
        // the shared source-to-key table.
        accessibleName: root.hostWindow.visibility === Window.Maximized
                        ? qsTrId("window.restore") : qsTrId("qml.maximize")
        onClicked: {
            if (root.hostWindow.visibility === Window.Maximized)
                root.hostWindow.showNormal()
            else
                root.hostWindow.showMaximized()
        }
    }

    CaptionButton {
        buttonType: "close"
        isClose: true
        accessibleName: qsTrId("action.close")
        onClicked: root.hostWindow.close()
    }

    component CaptionButton: AbstractButton {
        id: button

        required property string buttonType
        required property string accessibleName
        property bool isClose: false

        width: 46
        height: root.height
        hoverEnabled: true
        Accessible.name: accessibleName

        readonly property color iconColor: button.isClose && (button.hovered || button.down)
            ? "#FFFFFF"
            : (button.hovered || button.visualFocus ? Theme.colors.text.active : Theme.colors.text.secondary)

        contentItem: Item {
            implicitWidth: 10
            implicitHeight: 10

            Item {
                anchors.centerIn: parent
                width: 10
                height: 10

                // Minimize glyph: 10px horizontal line
                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 1
                    color: button.iconColor
                    visible: button.buttonType === "minimize"
                }

                // Maximize glyph: 10x10 square outline
                Rectangle {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    color: "transparent"
                    border.color: button.iconColor
                    border.width: 1
                    visible: button.buttonType === "maximize"
                }

                // Restore glyph: two overlapping 8x8 squares
                Item {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    visible: button.buttonType === "restore"

                    // Back square top and right edges
                    Rectangle {
                        x: 2; y: 0; width: 8; height: 1
                        color: button.iconColor
                    }
                    Rectangle {
                        x: 9; y: 0; width: 1; height: 8
                        color: button.iconColor
                    }
                    Rectangle {
                        x: 2; y: 0; width: 1; height: 3
                        color: button.iconColor
                    }
                    Rectangle {
                        x: 7; y: 7; width: 3; height: 1
                        color: button.iconColor
                    }

                    // Front square
                    Rectangle {
                        x: 0; y: 2; width: 8; height: 8
                        color: "transparent"
                        border.color: button.iconColor
                        border.width: 1
                    }
                }

                // Close glyph: 10x10 cross (X)
                Shape {
                    anchors.centerIn: parent
                    width: 10
                    height: 10
                    visible: button.buttonType === "close"
                    preferredRendererType: Shape.CurveRenderer

                    ShapePath {
                        strokeColor: button.iconColor
                        strokeWidth: 1
                        capStyle: ShapePath.FlatCap
                        startX: 0.5; startY: 0.5
                        PathLine { x: 9.5; y: 9.5 }
                    }
                    ShapePath {
                        strokeColor: button.iconColor
                        strokeWidth: 1
                        capStyle: ShapePath.FlatCap
                        startX: 9.5; startY: 0.5
                        PathLine { x: 0.5; y: 9.5 }
                    }
                }
            }
        }

        background: Rectangle {
            color: button.isClose
                ? (button.down ? "#B32617" : button.hovered ? "#C42B1C" : "transparent")
                : (button.down ? Theme.overlayColor(Theme.colors.activityState.pressed)
                               : button.hovered ? Theme.overlayColor(Theme.colors.activityState.hover)
                                                : "transparent")
        }
    }
}
