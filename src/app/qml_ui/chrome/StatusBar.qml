import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    property string difficulty: "Master 13+"
    property string documentName: "maidata.txt"
    property int cursorLine: 1
    property int cursorColumn: 1

    implicitHeight: 23
    color: Theme.colors.background.workbench

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.colors.border.normal
    }

    Row {
        anchors.left: parent.left
        anchors.leftMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        StatusText { text: root.difficulty }
        StatusText { text: root.documentName }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 14

        StatusText { text: qsTr("行 %1，列 %2").arg(root.cursorLine).arg(root.cursorColumn) }
        StatusText { text: "simai" }
    }

    component StatusText: Text {
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize
    }
}

