import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    required property string title
    required property string description

    color: Theme.colors.background.workbench

    PanelHeader {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: root.title
        showMore: false
    }

    Text {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.margins: 16
        text: root.description
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize
        wrapMode: Text.Wrap
    }
}

