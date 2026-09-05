pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

// Note counts under the preview. Column count follows whether six
// value cells fit; never elide numeric values. Transport chrome is
// independent — do not share a width threshold with PreviewTransport.
Item {
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
            // 统计值刷新会替换 QVariantList；数量保持稳定时复用现有单元，
            // 让数值绑定更新，保持图标、文字与布局对象的生命周期连续。
            model: root.statistics.length

            delegate: Item {
                id: cell
                required property int index
                readonly property var modelData: root.statistics[index]

                width: grid.width / root.columns
                height: grid.height / root.rows

                Row {
                    anchors.centerIn: parent
                    width: Math.min(root.contentWidth, cell.width - 8)
                    height: Math.max(24, textColumn.implicitHeight)
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
                        id: textColumn
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
                            // 数值水平缩字时维持行槽高度，名称和图标的中心位置稳定。
                            height: valueWidthProbe.implicitHeight
                            text: cell.modelData.value
                            color: Theme.colors.text.primary
                            font.family: Theme.uiFont
                            font.pixelSize: root.wideLayout ? Theme.uiFontSize : Theme.secondaryFontSize
                            horizontalAlignment: Text.AlignLeft
                            verticalAlignment: Text.AlignVCenter
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: Math.max(1, Theme.secondaryFontSize - 1)
                        }
                    }
                }
            }
        }
    }
}
