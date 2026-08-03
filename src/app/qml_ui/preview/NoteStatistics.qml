pragma ComponentBehavior: Bound

import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    required property var statistics

    implicitHeight: 55
    color: Theme.colors.background.workbench

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.colors.border.normal
    }

    Row {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4

        Repeater {
            model: root.statistics

            delegate: Item {
                id: statistic
                required property var modelData
                width: (root.width - 8) / 6
                height: root.height

                Column {
                    anchors.centerIn: parent
                    spacing: 2

                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: statistic.modelData.name
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                    }
                    Text {
                        anchors.horizontalCenter: parent.horizontalCenter
                        text: statistic.modelData.value
                        color: Theme.colors.text.primary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize
                    }
                }
            }
        }
    }
}

