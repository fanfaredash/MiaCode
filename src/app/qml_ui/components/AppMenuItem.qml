import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import QtQuick.Layouts
import MiaCode.UI

// Shared menu row — gray HoverChrome; text active / secondary / disabled.
MenuItem {
    id: root

    implicitHeight: 28
    leftPadding: 12
    rightPadding: subMenu ? 22 : 16
    topPadding: 3
    bottomPadding: 3
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    // Where the full identity goes when the label is deliberately short — the
    // recent-charts and backup lists show a folder name or a timestamp, and the
    // path they stand for is too wide to be a menu row.
    property string tooltip: ""
    // Dynamic menus own their rows directly, rather than wrapping them in an
    // Action. Keep their shortcut spelling on the visual item so Qt can both
    // insert it into Menu and render the binding.
    property string shortcutText: ""
    property int difficultyId: 0
    readonly property real textWidth: label.contentWidth
    readonly property real chromeWidth: leftPadding + rightPadding
                                        + (checkable ? 12 + row.spacing : 0)
                                        + (difficultyId > 0 ? Theme.difficultySwatchSize + row.spacing : 0)

    readonly property color labelColor: {
        if (!root.enabled)
            return Theme.colors.text.disabled
        if (root.highlighted || root.checked)
            return Theme.colors.text.active
        return Theme.colors.text.secondary
    }

    Tooltip {
        visible: root.tooltip.length > 0 && root.hovered
        text: root.tooltip
    }

    contentItem: RowLayout {
        id: row
        spacing: 10

        Item {
            Layout.preferredWidth: root.checkable ? 12 : 0
            visible: root.checkable
        }

        DifficultySwatch {
            Layout.preferredWidth: implicitWidth
            Layout.preferredHeight: implicitHeight
            Layout.alignment: Qt.AlignVCenter
            visible: root.difficultyId > 0
            difficultyId: root.difficultyId
        }

        ControlsImpl.MnemonicLabel {
            id: label
            Layout.fillWidth: true
            Layout.preferredWidth: contentWidth
            text: root.text
            font: root.font
            color: root.labelColor
            mnemonicVisible: true
            elide: Text.ElideRight
        }

        Text {
            // Static rows receive their spelling from an Action; dynamic rows
            // provide shortcutText directly because Repeater must create a
            // visual MenuItem, not a non-visual Action.
            readonly property string shortcutLabel:
                root.shortcutText.length > 0
                    ? root.shortcutText
                    : (root.action && root.action.shortcutText ? root.action.shortcutText : "")
            visible: shortcutLabel.length > 0
            text: shortcutLabel
            font: root.font
            color: Theme.colors.text.disabled
            opacity: root.enabled ? 1 : 0.55
        }
    }

    background: HoverChrome {
        selected: root.checked
        hovered: root.highlighted
        pressed: root.down
        tone: "nav"
    }

    indicator: Text {
        x: root.mirrored ? root.width - width - root.rightPadding : root.leftPadding
        y: root.topPadding + (root.availableHeight - height) / 2
        width: 12
        height: implicitHeight
        visible: root.checkable
        text: root.checked ? "✓" : ""
        color: Theme.colors.text.active
        font: root.font
        horizontalAlignment: Text.AlignHCenter
    }

    // Same placement contract as Qt Basic MenuItem; sized down slightly.
    arrow: ControlsImpl.ColorImage {
        x: root.mirrored ? root.leftPadding : root.width - width - root.rightPadding + 6
        y: root.topPadding + (root.availableHeight - height) / 2
        width: 8
        height: 12
        visible: root.subMenu
        mirror: root.mirrored
        source: root.subMenu
                ? "qrc:/qt-project.org/imports/QtQuick/Controls/Basic/images/arrow-indicator.png"
                : ""
        color: root.labelColor
        opacity: 0.85
    }
}
