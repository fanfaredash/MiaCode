import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Rectangle {
    id: root

    property string difficulty: ""
    property string documentName: ""
    property int cursorLine: 1
    property int cursorColumn: 1
    property string selectionBeatText: ""
    property string selectionBeatTooltip: ""

    implicitHeight: 23
    color: Theme.colors.background.statusBar

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.colors.border.status
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 14

        StatusText {
            visible: root.difficulty.length > 0
            text: root.difficulty
            Layout.preferredWidth: implicitWidth
        }
        StatusText {
            Layout.fillWidth: true
            text: root.documentName
            visible: text.length > 0
            elide: Text.ElideMiddle
        }
        Item { Layout.fillWidth: root.documentName.length === 0 }

        StatusText {
            text: UiText.text("行 %1，列 %2").arg(root.cursorLine).arg(root.cursorColumn)
            Layout.preferredWidth: implicitWidth
        }
        StatusText {
            text: root.selectionBeatText
            tooltipText: root.selectionBeatTooltip
            visible: text.length > 0
            Layout.preferredWidth: implicitWidth
        }
        StatusText {
            text: "simai"
            Layout.preferredWidth: implicitWidth
        }
    }

    component StatusText: Text {
        id: statusText
        property string tooltipText: ""
        color: Theme.colors.text.status
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            visible: statusText.tooltipText.length > 0
            ToolTip.visible: containsMouse
            ToolTip.text: statusText.tooltipText
        }
    }
}
