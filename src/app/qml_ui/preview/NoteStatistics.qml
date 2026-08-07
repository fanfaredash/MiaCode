pragma ComponentBehavior: Bound

import QtQuick
import MiaCode.UI

// Note counts under the preview. Column count follows whether six
// value cells fit; never elide numeric values. Transport chrome is
// independent — do not share a width threshold with PreviewTransport.
Rectangle {
    id: root

    required property var statistics

    // Enough for values like "999/999" at uiFontSize + side padding.
    readonly property int minCellWidth: 64
    readonly property bool wideLayout: width >= minCellWidth * 6
    readonly property int columns: wideLayout ? 6 : 3
    readonly property int rows: wideLayout ? 1 : 2

    implicitHeight: wideLayout ? 55 : 96
    height: implicitHeight
    color: Theme.colors.background.surface

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: 1
        color: Theme.colors.border.normal
    }

    Grid {
        id: grid
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        anchors.topMargin: 4
        anchors.bottomMargin: 4
        columns: root.columns
        rows: root.rows
        columnSpacing: 0
        rowSpacing: 0

        Repeater {
            model: root.statistics

            delegate: Item {
                id: cell
                required property var modelData

                width: grid.width / root.columns
                height: grid.height / root.rows

                Column {
                    anchors.centerIn: parent
                    spacing: 2
                    width: parent.width - 4

                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        text: cell.modelData.name
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                        elide: Text.ElideRight
                    }
                    Text {
                        width: parent.width
                        horizontalAlignment: Text.AlignHCenter
                        // Numeric values must stay complete — switch layout instead of eliding.
                        text: cell.modelData.value
                        color: Theme.colors.text.primary
                        font.family: Theme.uiFont
                        font.pixelSize: root.wideLayout ? Theme.uiFontSize : Theme.secondaryFontSize
                    }
                }
            }
        }
    }
}
