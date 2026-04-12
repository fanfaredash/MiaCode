import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: root

    property var controller: null
    property var paletteMap: ({})
    property var metricsMap: ({})

    signal focusRequested()

    function metric(key, fallback) {
        return metricsMap && metricsMap[key] !== undefined ? metricsMap[key] : fallback
    }

    function tone(key, fallback) {
        return paletteMap && paletteMap[key] !== undefined ? paletteMap[key] : fallback
    }

    readonly property int wideCols: metric("previewStatsWideLayoutCols", 3)
    readonly property int narrowCols: metric("previewStatsNarrowLayoutCols", 2)
    readonly property int horizontalSpacing: metric("previewStatsHorizontalSpacing", 10)
    readonly property int verticalSpacing: metric("previewStatsVerticalSpacing", 6)
    readonly property int chipHeight: metric("previewStatsChipHeight", 30)
    readonly property int itemCount: controller ? controller.previewStatsTexts.length : 0
    readonly property int wideThreshold:
        metric("previewStatsWideLayoutMinChipWidth", 118) * wideCols
        + horizontalSpacing * Math.max(0, wideCols - 1)
        + 4
    readonly property int resolvedColumns: width >= wideThreshold ? wideCols : narrowCols
    readonly property int resolvedRows: Math.max(1, Math.ceil(Math.max(1, itemCount) / Math.max(1, resolvedColumns)))

    implicitHeight: 20 + resolvedRows * chipHeight + Math.max(0, resolvedRows - 1) * verticalSpacing
    radius: metric("previewStatsCardRadius", 10)
    color: tone("cardBg", "#ffffff")
    border.color: tone("border", "#d5e0ec")

    TapHandler {
        acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
        onTapped: root.focusRequested()
    }

    GridLayout {
        id: statsGrid

        anchors.fill: parent
        anchors.margins: 8
        columns: Math.max(1, root.resolvedColumns)
        columnSpacing: root.horizontalSpacing
        rowSpacing: root.verticalSpacing

        Repeater {
            model: controller ? controller.previewStatsTexts : []

            delegate: Rectangle {
                readonly property bool totalChip: index === 5

                Layout.fillWidth: true
                Layout.preferredHeight: root.chipHeight
                radius: root.metric("previewStatsChipRadius", 9)
                color: totalChip ? root.tone("menuHoverBg", "#eef5ff") : root.tone("windowAltBg", "#f0f4f8")
                border.color: totalChip ? root.tone("accent", "#2e77d0") : root.tone("borderSoft", "#ccd6e2")

                Label {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    anchors.topMargin: 2
                    anchors.bottomMargin: 2
                    text: modelData
                    color: totalChip ? root.tone("textPrimary", "#203040") : root.tone("textSecondary", "#52667a")
                    font.family: Qt.platform.os === "windows" ? "Cascadia Mono" : "monospace"
                    font.pixelSize: 13
                    font.weight: totalChip ? Font.DemiBold : Font.Medium
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }
        }
    }
}
