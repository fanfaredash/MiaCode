import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Themed switch — geometry/colors aligned with v2 Theme (not stock Fusion).
Switch {
    id: root

    // A switch that heads a group of settings (片头 → 添加片头) reads as that
    // group's section caption rather than as one row inside it.
    property bool sectionTitle: false

    font.family: Theme.uiFont
    font.pixelSize: root.sectionTitle ? Theme.sectionTitleFontSize : Theme.uiFontSize
    font.bold: root.sectionTitle
    hoverEnabled: true
    leftPadding: 0
    rightPadding: 0
    topPadding: 0
    bottomPadding: 0
    implicitHeight: Theme.controlMinHeight
    implicitWidth: Math.ceil(leftPadding + rightPadding
                             + (indicator ? indicator.implicitWidth : 0)
                             + (root.text.length > 0 ? spacing + labelMetrics.advanceWidth : 0))

    TextMetrics {
        id: labelMetrics
        font: root.font
        text: root.text
    }

    indicator: Rectangle {
        implicitWidth: 36
        implicitHeight: 20
        x: root.leftPadding
        y: parent.height / 2 - height / 2
        radius: height / 2
        color: Theme.overlayColor(root.checked ? Theme.colors.accent.primary : Theme.colors.border.control)
        opacity: root.enabled ? 1 : 0.45

        Rectangle {
            x: root.checked ? parent.width - width - 2 : 2
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 8
            color: Theme.colors.text.active
            Behavior on x { NumberAnimation { duration: 100 } }
        }
    }

    contentItem: Text {
        text: root.text
        font: root.font
        color: !root.enabled ? Theme.colors.text.disabled
               : root.sectionTitle ? Theme.colors.text.section
               : root.checked ? Theme.colors.text.active
               : Theme.colors.text.secondary
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
    }
}
