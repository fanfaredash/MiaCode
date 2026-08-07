import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    property string title
    property bool showMore: false

    implicitHeight: 34
    color: Theme.colors.background.surface

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: Theme.colors.text.primary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        visible: root.showMore
        text: "..."
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }
}

