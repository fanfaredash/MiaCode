import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// Shortcut editor. Clicking a row arms capture: the next key press with at
// least one non-modifier becomes that command's binding. Escape cancels the
// capture rather than closing the dialog, so an accidental arm cannot lose the
// row you were editing.
Dialog {
    id: root

    required property var shortcuts
    // Used only to resolve shortcut label keys.
    required property var preferences

    title: qsTr("编辑快捷键")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(680, Overlay.overlay ? Overlay.overlay.width - 48 : 680)
    height: Math.min(520, Overlay.overlay ? Overlay.overlay.height - 48 : 520)
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape

    property string capturingId: ""
    property var rows: []

    function refresh() {
        root.rows = root.shortcuts.editableShortcuts()
    }

    onAboutToShow: {
        root.capturingId = ""
        root.refresh()
    }

    function describe(event) {
        // Modifier-only presses keep the capture armed: they are the first half
        // of a chord, not a binding.
        if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift
                || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta)
            return ""
        let parts = []
        if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
        if (event.modifiers & Qt.AltModifier) parts.push("Alt")
        if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
        if (event.modifiers & Qt.MetaModifier) parts.push("Meta")
        parts.push(root.shortcuts.keyName(event.key))
        return parts.join("+")
    }

    contentItem: ColumnLayout {
        spacing: 8

        Text {
            Layout.fillWidth: true
            text: root.capturingId.length > 0
                  ? qsTr("按下新的快捷键，Esc 取消。")
                  : qsTr("点击一行以录制新的快捷键。")
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
        }

        ListView {
            id: list
            objectName: "shortcutList"
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            focus: true
            model: root.rows
            ScrollBar.vertical: ScrollBar {}

            Keys.onPressed: function(event) {
                if (root.capturingId.length === 0)
                    return
                event.accepted = true
                if (event.key === Qt.Key_Escape) {
                    root.capturingId = ""
                    return
                }
                const text = root.describe(event)
                if (text.length === 0)
                    return
                root.shortcuts.setShortcutText(root.capturingId, text)
                root.capturingId = ""
                root.refresh()
            }

            delegate: AbstractButton {
                id: shortcutRow
                required property var modelData
                width: ListView.view.width
                implicitHeight: 34
                onClicked: {
                    root.capturingId = modelData.id
                    list.forceActiveFocus()
                }
                background: HoverChrome {
                    hovered: shortcutRow.hovered
                    pressed: shortcutRow.down
                    selected: root.capturingId === shortcutRow.modelData.id
                    tone: "nav"
                }
                contentItem: RowLayout {
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        leftPadding: 8
                        text: root.preferences.localizedText(shortcutRow.modelData.labelKey)
                              || shortcutRow.modelData.labelFallback
                        elide: Text.ElideRight
                        color: Theme.colors.text.active
                        font.family: Theme.uiFont
                    }
                    Text {
                        Layout.preferredWidth: 170
                        text: root.capturingId === shortcutRow.modelData.id
                              ? qsTr("录制中…")
                              : shortcutRow.modelData.shortcutText
                        color: shortcutRow.modelData.isDefault
                               ? Theme.colors.text.secondary
                               : Theme.colors.accent.primary
                        font.family: Theme.uiFont
                    }
                    AppButton {
                        text: qsTr("默认")
                        enabled: !shortcutRow.modelData.isDefault
                        onClicked: {
                            root.shortcuts.resetShortcut(shortcutRow.modelData.id)
                            root.refresh()
                        }
                    }
                }
            }
        }

        AppButton {
            objectName: "shortcutResetAllButton"
            Layout.alignment: Qt.AlignLeft
            text: qsTr("全部恢复默认")
            onClicked: {
                root.shortcuts.resetAllShortcuts()
                root.refresh()
            }
        }
    }
}
