import QtQuick
import QtQuick.Controls
import MiaCode.UI

Popup {
    id: root
    required property var editor
    required property var controller

    parent: Overlay.overlay
    modal: false
    focus: false
    closePolicy: Popup.NoAutoClose
    padding: Theme.menuPadding
    width: Math.max(156, candidateList.implicitWidth + 2 * padding)
    height: Math.min(260, candidateList.contentHeight + 2 * padding)
    x: editor.mapToItem(parent, editor.cursorRectangle.x, editor.cursorRectangle.y + editor.cursorRectangle.height).x
    y: editor.mapToItem(parent, editor.cursorRectangle.x, editor.cursorRectangle.y + editor.cursorRectangle.height).y
    visible: controller.completionActive && controller.completionCandidates.length > 0

    background: Rectangle {
        radius: Theme.controlRadius
        color: Theme.colors.background.elevated
        border.width: Theme.controlBorderWidth
        border.color: Theme.colors.border.control
    }

    contentItem: ListView {
        id: candidateList
        implicitWidth: contentWidth
        model: root.controller.completionCandidates
        clip: true
        interactive: contentHeight > root.height
        delegate: ItemDelegate {
            required property string modelData
            width: candidateList.width
            height: Math.max(Theme.controlMinHeight, implicitHeight)
            text: modelData
            font: Theme.codeFont
            leftPadding: 10
            rightPadding: 10
            highlighted: index === root.controller.completionIndex
            hoverEnabled: true
            contentItem: Text {
                text: parent.text
                color: Theme.colors.text.primary
                font: parent.font
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideNone
            }
            background: Rectangle {
                radius: Theme.itemRadius
                color: parent.highlighted || parent.hovered ? Theme.colors.state.menuSelection : "transparent"
            }
            onClicked: {
                root.controller.selectCompletionIndex(index)
                root.editor.acceptCompletionFromPopup()
            }
        }
    }
}
