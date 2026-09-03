import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Rectangle {
    id: root

    required property var editor
    required property var controller
    property bool open: false
    property string query: ""
    property string replacement: ""
    property bool caseSensitive: false
    property bool wholeWord: false

    height: open ? implicitHeight : 0
    implicitHeight: 40
    visible: height > 0
    color: Theme.overlayColor(Theme.colors.background.panel, Theme.popupOpacity)

    function selectResult(result) {
        if (result.found) {
            editor.select(result.start, result.end)
            editor.centerCursorInView()
        }
    }
    function find(backwards) {
        selectResult(controller.findForQml(editor.text, editor.selectionStart, editor.selectionEnd,
                                           query, caseSensitive, wholeWord, backwards))
    }
    function replaceOne() {
        const tx = controller.replaceSelectionForQml(editor.text, editor.selectionStart,
                                                     editor.selectionEnd, query, replacement,
                                                     caseSensitive, wholeWord)
        if (tx.consumed)
            editor.applyEditorTransaction(tx)
        find(false)
    }
    function replaceEverything() {
        const tx = controller.replaceAllForQml(editor.text, query, replacement,
                                               caseSensitive, wholeWord)
        if (tx.consumed) {
            editor.applyEditorTransaction(tx)
            editor.centerCursorInView()
        }
    }
    function show() {
        open = true
        Qt.callLater(() => queryField.forceActiveFocus())
    }
    function dismiss() {
        open = false
        editor.forceActiveFocus()
    }

    Keys.onEscapePressed: event => {
        dismiss()
        event.accepted = true
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 6
        AppTextField {
            id: queryField
            Layout.preferredWidth: 150
            placeholderText: UiText.text("查找")
            text: root.query
            onTextEdited: root.query = text
            Keys.onReturnPressed: event => root.find((event.modifiers & Qt.ShiftModifier) !== 0)
        }
        AppTextField {
            Layout.preferredWidth: 130
            placeholderText: UiText.text("替换")
            text: root.replacement
            onTextEdited: root.replacement = text
            Keys.onReturnPressed: root.replaceOne()
        }
        AppSwitch { text: UiText.text("区分大小写"); checked: root.caseSensitive; onToggled: root.caseSensitive = checked }
        AppSwitch { text: UiText.text("全词"); checked: root.wholeWord; onToggled: root.wholeWord = checked }
        AppButton { text: UiText.text("上一个"); onClicked: root.find(true) }
        AppButton { text: UiText.text("下一个"); onClicked: root.find(false) }
        AppButton { text: UiText.text("替换"); onClicked: root.replaceOne() }
        AppButton { text: UiText.text("全部替换"); onClicked: root.replaceEverything() }
        Item { Layout.fillWidth: true }
        AppButton { text: UiText.text("关闭"); onClicked: root.dismiss() }
    }
}
