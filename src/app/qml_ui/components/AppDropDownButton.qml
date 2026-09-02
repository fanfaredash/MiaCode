import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

AbstractButton {
    id: root

    property bool expanded: false
    property string tooltip
    property var sizeToLabels: []

    font.family: Theme.uiFont
    font.pixelSize: Theme.captionFontSize
    topPadding: 0
    bottomPadding: 0
    leftPadding: 8
    rightPadding: 8
    implicitWidth: leftPadding + rightPadding + root.longestLabelWidth + 6 + root.chevronSize
    implicitHeight: 22
    hoverEnabled: true
    focusPolicy: Qt.TabFocus
    Accessible.name: root.tooltip.length > 0 ? root.tooltip : root.text

    readonly property int chevronSize: 12
    property int longestLabelWidth: 0

    function syncLongestLabelWidth() {
        let widest = currentProbe.contentWidth
        for (let i = 0; i < labelProbes.count; i++) {
            const probe = labelProbes.itemAt(i)
            if (probe)
                widest = Math.max(widest, probe.contentWidth)
        }
        root.longestLabelWidth = Math.ceil(widest)
    }

    Column {
        x: -10000
        y: -10000
        enabled: false
        opacity: 0

        Text {
            id: currentProbe
            font: root.font
            text: root.text
            onContentWidthChanged: root.syncLongestLabelWidth()
        }

        Repeater {
            id: labelProbes
            model: root.sizeToLabels
            delegate: Text {
                required property var modelData
                font: root.font
                text: String(modelData)
                onContentWidthChanged: root.syncLongestLabelWidth()
            }
            onItemAdded: root.syncLongestLabelWidth()
            onItemRemoved: root.syncLongestLabelWidth()
        }
    }

    Component.onCompleted: root.syncLongestLabelWidth()

    contentItem: Item {
        Text {
            anchors.left: parent.left
            anchors.right: chevron.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font: root.font
            color: Theme.colors.text.primary
            verticalAlignment: Text.AlignVCenter
        }

        ControlsImpl.IconImage {
            id: chevron
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            width: root.chevronSize
            height: root.chevronSize
            source: Qt.resolvedUrl("icons/chevron-down.svg")
            sourceSize: Qt.size(width, height)
            color: root.expanded || root.hovered
                ? Theme.colors.text.active
                : Theme.colors.text.secondary
        }
    }

    background: Rectangle {
        radius: Theme.itemRadius
        color: root.down || root.expanded
            ? Theme.colors.state.pressed
            : root.hovered
                ? Theme.colors.state.hover
                : Theme.surfaceColor("input", Theme.colors.background.elevated)
        border.width: Theme.controlBorderWidth
        border.color: root.visualFocus || root.expanded || root.hovered
            ? Theme.colors.accent.primary
            : Theme.colors.border.control
    }

    Tooltip {
        visible: root.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
}
