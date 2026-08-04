import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import QtQuick.Layouts
import MiaCode.UI

// Shared menu row — gray HoverChrome; text active / secondary / disabled.
MenuItem {
    id: root

    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    implicitHeight: 28
    leftPadding: 12
    rightPadding: subMenu ? 22 : 16
    topPadding: 3
    bottomPadding: 3

    readonly property color labelColor: {
        if (!root.enabled)
            return Theme.colors.text.disabled
        if (root.highlighted || root.checked)
            return Theme.colors.text.active
        return Theme.colors.text.secondary
    }

    contentItem: RowLayout {
        spacing: 10

        Text {
            Layout.preferredWidth: root.checkable ? 12 : 0
            visible: root.checkable
            text: root.checked ? "✓" : ""
            color: Theme.colors.text.active
            font: root.font
            horizontalAlignment: Text.AlignHCenter
        }

        ControlsImpl.MnemonicLabel {
            Layout.fillWidth: true
            text: root.text
            font: root.font
            color: root.labelColor
            mnemonicVisible: true
            elide: Text.ElideRight
        }

        Text {
            visible: root.shortcut.length > 0
            text: root.shortcut
            font: root.font
            color: Theme.colors.text.disabled
            opacity: root.enabled ? 1 : 0.55
        }
    }

    background: HoverChrome {
        selected: root.highlighted
        tone: "nav"
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
