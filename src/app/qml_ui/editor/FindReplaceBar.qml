import QtQuick
import QtQuick.Controls
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
    implicitHeight: controls.height + 12
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

    Item {
        id: controls
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        readonly property real gap: 6
        height: actionArea.y + actionArea.height

        Item {
            id: searchArea
            width: parent.width
            readonly property real wideWidth:
                300 + caseSwitch.implicitWidth + wholeWordSwitch.implicitWidth + 3 * controls.gap
            readonly property real pairedFieldsWidth: 300 + controls.gap
            readonly property real pairedSwitchesWidth:
                caseSwitch.implicitWidth + wholeWordSwitch.implicitWidth + controls.gap
            readonly property real pairedWidth: pairedFieldsWidth >= pairedSwitchesWidth
                ? pairedFieldsWidth : pairedSwitchesWidth
            readonly property bool wide: width >= wideWidth
            readonly property bool paired: !wide && width >= pairedWidth
            readonly property real wideRowHeight: {
                const fieldHeight = queryField.implicitHeight
                const switchHeight = caseSwitch.implicitHeight >= wholeWordSwitch.implicitHeight
                    ? caseSwitch.implicitHeight : wholeWordSwitch.implicitHeight
                return fieldHeight >= switchHeight ? fieldHeight : switchHeight
            }
            readonly property real switchRowHeight:
                caseSwitch.implicitHeight >= wholeWordSwitch.implicitHeight
                ? caseSwitch.implicitHeight : wholeWordSwitch.implicitHeight
            readonly property real fieldWidth: wide
                ? (width - caseSwitch.implicitWidth - wholeWordSwitch.implicitWidth
                   - 3 * controls.gap) / 2
                : paired ? (width - controls.gap) / 2 : width

            height: wide ? wideRowHeight
                : paired
                    ? queryField.implicitHeight + controls.gap + switchRowHeight
                    : queryField.implicitHeight + replacementField.implicitHeight
                      + caseSwitch.implicitHeight + wholeWordSwitch.implicitHeight
                      + 3 * controls.gap

            AppTextField {
                id: queryField
                x: 0
                y: searchArea.wide
                    ? (searchArea.wideRowHeight - height) / 2 : 0
                width: searchArea.fieldWidth
                placeholderText: UiText.text("查找")
                text: root.query
                onTextEdited: root.query = text
                Keys.onReturnPressed: event => root.find((event.modifiers & Qt.ShiftModifier) !== 0)
            }
            AppTextField {
                id: replacementField
                x: searchArea.wide || searchArea.paired
                    ? queryField.x + queryField.width + controls.gap : 0
                y: searchArea.wide
                    ? (searchArea.wideRowHeight - height) / 2
                    : searchArea.paired ? 0 : queryField.y + queryField.height + controls.gap
                width: searchArea.fieldWidth
                placeholderText: UiText.text("替换")
                text: root.replacement
                onTextEdited: root.replacement = text
                Keys.onReturnPressed: root.replaceOne()
            }
            AppSwitch {
                id: caseSwitch
                x: searchArea.wide
                    ? replacementField.x + replacementField.width + controls.gap : 0
                y: searchArea.wide
                    ? (searchArea.wideRowHeight - height) / 2
                    : searchArea.paired
                        ? queryField.height + controls.gap
                          + (searchArea.switchRowHeight - height) / 2
                        : replacementField.y + replacementField.height + controls.gap
                text: UiText.text("区分大小写")
                checked: root.caseSensitive
                onToggled: root.caseSensitive = checked
            }
            AppSwitch {
                id: wholeWordSwitch
                x: searchArea.wide
                    ? caseSwitch.x + caseSwitch.width + controls.gap
                    : searchArea.paired ? searchArea.width - width : 0
                y: searchArea.wide
                    ? (searchArea.wideRowHeight - height) / 2
                    : searchArea.paired
                        ? queryField.height + controls.gap
                          + (searchArea.switchRowHeight - height) / 2
                        : caseSwitch.y + caseSwitch.height + controls.gap
                text: UiText.text("全词")
                checked: root.wholeWord
                onToggled: root.wholeWord = checked
            }
        }

        Item {
            id: actionArea
            x: 0
            y: searchArea.height + controls.gap
            width: parent.width
            readonly property real firstButtonWidth:
                previousButton.implicitWidth >= nextButton.implicitWidth
                ? previousButton.implicitWidth : nextButton.implicitWidth
            readonly property real secondButtonWidth:
                replaceButton.implicitWidth >= replaceAllButton.implicitWidth
                ? replaceButton.implicitWidth : replaceAllButton.implicitWidth
            readonly property real actionButtonWidth:
                firstButtonWidth >= secondButtonWidth ? firstButtonWidth : secondButtonWidth
            readonly property int mode:
                width >= 4 * actionButtonWidth + closeButton.implicitWidth
                         + 4 * controls.gap ? 0
                : width >= 4 * actionButtonWidth + 3 * controls.gap ? 1
                : width >= 2 * actionButtonWidth + controls.gap ? 2 : 3
            readonly property real buttonWidth: actionButtonWidth

            height: closeButton.y + closeButton.height

            AppButton {
                id: previousButton
                x: 0
                y: 0
                width: actionArea.buttonWidth
                text: UiText.text("上一个")
                onClicked: root.find(true)
            }
            AppButton {
                id: nextButton
                x: actionArea.mode < 3
                    ? previousButton.x + previousButton.width + controls.gap : 0
                y: actionArea.mode < 3
                    ? 0 : previousButton.y + previousButton.height + controls.gap
                width: actionArea.buttonWidth
                text: UiText.text("下一个")
                onClicked: root.find(false)
            }
            AppButton {
                id: replaceButton
                x: actionArea.mode <= 1
                    ? nextButton.x + nextButton.width + controls.gap : 0
                y: actionArea.mode <= 1
                    ? 0 : nextButton.y + nextButton.height + controls.gap
                width: actionArea.buttonWidth
                text: UiText.text("替换")
                onClicked: root.replaceOne()
            }
            AppButton {
                id: replaceAllButton
                x: actionArea.mode <= 2
                    ? replaceButton.x + replaceButton.width + controls.gap : 0
                y: actionArea.mode <= 1 ? 0
                    : actionArea.mode === 2 ? replaceButton.y
                    : replaceButton.y + replaceButton.height + controls.gap
                width: actionArea.buttonWidth
                text: UiText.text("全部替换")
                emphasized: true
                onClicked: root.replaceEverything()
            }
            AppButton {
                id: closeButton
                x: parent.width - width
                y: actionArea.mode === 0
                    ? 0 : replaceAllButton.y + replaceAllButton.height + controls.gap
                text: UiText.text("关闭")
                onClicked: root.dismiss()
            }
        }
    }
}
