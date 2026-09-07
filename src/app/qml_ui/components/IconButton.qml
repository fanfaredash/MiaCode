import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

AbstractButton {
    id: root

    property url iconSource
    property string label: ""
    // Optional text glyph (e.g. "+" / "−") when no iconSource is set.
    property string glyph: ""
    property string tooltip
    property bool active: false
    property bool compact: false
    property int iconWidth: compact ? 14 : 16
    property int iconHeight: compact ? 14 : 16
    property var stateColors: Theme.colors.buttonState
    property Item keyForwardTarget: null
    readonly property real horizontalInset: chrome.horizontalInset

    Keys.priority: Keys.BeforeItem
    Keys.forwardTo: root.keyForwardTarget ? [root.keyForwardTarget] : []

    readonly property real contentImplicitWidth: glyph.length > 0
        ? glyphLabel.implicitWidth
        : iconWidth + (label.length > 0 ? labelText.implicitWidth + contentRow.spacing : 0)
    implicitWidth: compact ? Theme.compactControlHeight : Math.max(Theme.controlMinHeight,
                            contentImplicitWidth
                            + 2 * (Theme.chromePadding + Theme.chromeInsetX))
    implicitHeight: compact ? Theme.compactControlHeight : Math.max(Theme.controlMinHeight,
                             (glyph.length > 0 ? glyphLabel.implicitHeight : iconHeight)
                             + 2 * (Theme.chromePadding + Theme.chromeInsetY))
    hoverEnabled: true
    Accessible.name: root.tooltip

    readonly property color glyphColor: !root.enabled ? Theme.colors.text.disabled
                                       : (root.active || root.checked || root.hovered || root.visualFocus) ? Theme.colors.text.active
                                       : Theme.colors.text.secondary

    contentItem: Item {
        anchors.fill: parent

        Row {
            id: contentRow
            anchors.centerIn: parent
            spacing: 5
            visible: root.glyph.length === 0

            ControlsImpl.IconImage {
                anchors.verticalCenter: parent.verticalCenter
                width: root.iconWidth
                height: root.iconHeight
                source: root.iconSource
                sourceSize: Qt.size(root.iconWidth, root.iconHeight)
                color: root.glyphColor
            }

            Text {
                id: labelText
                anchors.verticalCenter: parent.verticalCenter
                visible: root.label.length > 0
                text: root.label
                color: root.glyphColor
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                verticalAlignment: Text.AlignVCenter
            }
        }

        Text {
            id: glyphLabel
            anchors.centerIn: parent
            visible: root.glyph.length > 0
            text: root.glyph
            color: root.glyphColor
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize + 2
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: HoverChrome {
        id: chrome
        contentWidth: root.contentImplicitWidth
        contentHeight: root.glyph.length > 0 ? glyphLabel.implicitHeight : root.iconHeight
        stateColors: root.stateColors
        selected: root.active || root.checked
        hovered: root.hovered
        pressed: root.down
        focused: root.visualFocus
    }

    Tooltip {
        visible: root.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
}
