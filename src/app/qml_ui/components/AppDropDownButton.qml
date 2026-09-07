import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

AbstractButton {
    id: root

    property bool expanded: false
    property bool compact: false
    property string tooltip
    property var sizeToLabels: []

    font.family: Theme.uiFont
    font.pixelSize: Theme.captionFontSize
    topPadding: 0
    bottomPadding: 0
    leftPadding: compact ? 6 : 8
    rightPadding: compact ? 6 : 8
    implicitWidth: leftPadding + rightPadding + root.longestLabelWidth + 6 + root.chevronSize
    implicitHeight: compact ? Theme.compactControlHeight : Math.max(Theme.controlMinHeight,
                             Math.max(label.implicitHeight, chevronSize)
                             + 2 * (Theme.chromePadding + Theme.chromeInsetY))
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
            id: label
            anchors.left: parent.left
            anchors.right: chevron.left
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            text: root.text
            font: root.font
            color: root.enabled ? Theme.colors.text.primary : Theme.colors.text.disabled
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
            color: !root.enabled ? Theme.colors.text.disabled
                : root.expanded || root.hovered || root.visualFocus
                ? Theme.colors.text.active
                : Theme.colors.text.secondary
        }
    }

    background: HoverChrome {
        stateColors: Theme.colors.buttonState
        contentHeight: Math.max(label.implicitHeight, root.chevronSize)
        baseColor: Theme.colors.background.elevated
        selected: root.expanded
        hovered: root.hovered
        pressed: root.down
        focused: root.visualFocus
    }

    Tooltip {
        visible: root.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
}
