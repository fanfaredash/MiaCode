import QtQuick
import QtQuick.Layouts
import MiaCode.UI

// A settings page is a stack of these: a divider, a bold caption, then the
// controls underneath. Every page in ExportVideoPage hand-rolled this same
// three-piece shape, so it moved here once the third copy showed up.
ColumnLayout {
    id: root

    required property string title
    // The page's own leading divider (if any) already separates the tab bar
    // from the first section, so the first section skips its own to avoid a
    // doubled-up line.
    property bool first: false
    default property alias content: contentColumn.data

    Rectangle {
        visible: !root.first
        Layout.fillWidth: true
        Layout.topMargin: 2
        height: 1
        color: Theme.colors.border.normal
    }

    Text {
        text: root.title
        color: Theme.colors.text.active
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        font.bold: true
    }

    ColumnLayout {
        id: contentColumn
        Layout.fillWidth: true
        spacing: 10
    }
}
