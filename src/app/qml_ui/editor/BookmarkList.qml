import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Bookmarks are derived from the persisted `||` comment syntax; this view never
// creates an independent serialized bookmark store.
ListView {
    id: root
    required property var editor
    property var bookmarks: []
    clip: true
    Rectangle {
        anchors.fill: parent
        z: -1
        color: Theme.colors.background.elevated
        border.color: Theme.colors.border.normal
    }
    model: bookmarks
    delegate: ItemDelegate {
        required property var modelData
        width: ListView.view.width
        text: qsTr("第 %1 行：%2").arg(modelData.line).arg(modelData.title)
        onClicked: editor.jumpToLine(modelData.line)
        Accessible.name: text
        Accessible.role: Accessible.ListItem
        Keys.onDeletePressed: editor.deleteBookmarkAtLine(modelData.line)
        Keys.onPressed: event => {
            if (event.key === Qt.Key_F2) {
                editor.promptRenameBookmark(modelData.line)
                event.accepted = true
            }
        }
    }
}
