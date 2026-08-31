import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    property string title
    property bool showMore: false
    default property alias trailing: trailingRow.data

    implicitHeight: 34
    color: Theme.surfaceColor("panel", Theme.colors.background.surface)

    Text {
        anchors.left: parent.left
        anchors.leftMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        text: root.title
        color: Theme.colors.text.primary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }

    Row {
        id: trailingRow
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5
    }

    Text {
        anchors.right: parent.right
        anchors.rightMargin: 10
        anchors.verticalCenter: parent.verticalCenter
        visible: root.showMore && trailingRow.children.length === 0
        text: "..."
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }
}
