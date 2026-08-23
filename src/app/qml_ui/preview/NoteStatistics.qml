pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

// Note counts under the preview. Column count follows whether six
// value cells fit; never elide numeric values. Transport chrome is
// independent — do not share a width threshold with PreviewTransport.
Rectangle {
    id: root

    required property var statistics

    readonly property int iconSlotWidth: 24
    readonly property int contentSpacing: 6
    readonly property real textColumnWidth: Math.max(nameWidthProbe.implicitWidth,
                                                     valueWidthProbe.implicitWidth)
    readonly property real contentWidth: iconSlotWidth + contentSpacing + textColumnWidth
    // The row can contract its text track; keep the six-column breakpoint compact.
    readonly property int minCellWidth: 88
    readonly property bool wideLayout: width >= minCellWidth * 6
    readonly property int columns: wideLayout ? 6 : 3
    readonly property int rows: wideLayout ? 1 : 2

    implicitHeight: wideLayout ? 55 : 96
    height: implicitHeight
    color: Theme.colors.background.surface

    Text {
        id: nameWidthProbe
        visible: false
        text: "Touch"
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize
    }

    Text {
        id: valueWidthProbe
        visible: false
        text: "8888/8888"
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }

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

                Row {
                    anchors.centerIn: parent
                    width: Math.min(root.contentWidth, cell.width - 8)
                    spacing: root.contentSpacing

                    Item {
                        width: root.iconSlotWidth
                        height: 24
                        anchors.verticalCenter: parent.verticalCenter

                        Image {
                            anchors.fill: parent
                            visible: cell.modelData.kind !== "total"
                            source: cell.modelData.iconSource
                            sourceSize: Qt.size(32, 32)
                            fillMode: Image.PreserveAspectFit
                            smooth: true
                        }

                        ControlsImpl.IconImage {
                            anchors.centerIn: parent
                            width: 20
                            height: 20
                            visible: cell.modelData.kind === "total"
                            source: Qt.resolvedUrl("icons/chart.svg")
                            sourceSize: Qt.size(width, height)
                            color: Theme.colors.text.secondary
                        }
                    }

                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 2
                        width: parent.width - root.iconSlotWidth - parent.spacing

                        Text {
                            id: nameLabel
                            width: parent.width
                            text: cell.modelData.name
                            color: Theme.colors.text.secondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.secondaryFontSize
                            horizontalAlignment: Text.AlignLeft
                            elide: Text.ElideRight
                        }
                        Text {
                            id: valueLabel
                            width: parent.width
                            text: cell.modelData.value
                            color: Theme.colors.text.primary
                            font.family: Theme.uiFont
                            font.pixelSize: root.wideLayout ? Theme.uiFontSize : Theme.secondaryFontSize
                            horizontalAlignment: Text.AlignLeft
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: Math.max(1, Theme.secondaryFontSize - 1)
                        }
                    }
                }
            }
        }
    }
}
