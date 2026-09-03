import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

CheckBox {
    id: root

    property bool compact: false

    spacing: compact ? 4 : 6
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    implicitHeight: 24
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    font.family: Theme.uiFont
    font.pixelSize: compact ? Theme.compactFontSize : Theme.secondaryFontSize
    indicator: Item {
        implicitWidth: root.compact ? 13 : 15
        implicitHeight: root.compact ? 13 : 15
        x: root.leftPadding
        y: (root.height - height) / 2

        Rectangle {
            anchors.fill: parent
            radius: 4
            color: Theme.overlayColor(root.checked ? Theme.colors.accent.primary : "transparent")
            border.width: 1
            border.color: root.checked || root.hovered
                ? Theme.colors.accent.primary
                : Theme.colors.border.control
        }

        ControlsImpl.IconImage {
            anchors.centerIn: parent
            width: root.compact ? 9 : 11
            height: root.compact ? 9 : 11
            visible: root.checked
            source: "qrc:/icons/checkmark.svg"
            sourceSize: Qt.size(width, height)
            color: Theme.colors.text.onAccent
        }

        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            visible: root.visualFocus
            radius: 6
            color: "transparent"
            border.width: 1
            border.color: Theme.colors.accent.focus
        }
    }

    contentItem: Text {
        id: label

        leftPadding: root.indicator.width + root.spacing
        text: root.text
        font: root.font
        color: root.checked ? Theme.colors.text.active : Theme.colors.text.secondary
        verticalAlignment: Text.AlignVCenter
    }
}
