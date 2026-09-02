import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// An app-styled question with any number of answers.
//
// QtQuick.Dialogs' MessageDialog renders as the platform's own alert — an
// NSAlert on macOS — which takes none of the shell's typography or buttons and
// reads as a foreign window dropped into the middle of the app. This is the
// same dialog chrome as everything else here, and unlike DialogFooter it is not
// limited to accept/cancel: the middle answer of 保存 / 放弃 / 取消 is neither.
//
// `choices` is [{ id, label, role }] in button order, role being
// "accept" | "destructive" | "reject". Dismissing — Escape, the scrim — resolves
// to `dismissChoiceId`, so a closed window can never stand for an answer the
// caller did not offer.
Dialog {
    id: root
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    property string message: ""
    property string details: ""
    property var choices: []
    property string dismissChoiceId: ""

    signal chosen(string choiceId)

    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, Overlay.overlay ? Overlay.overlay.width - 48 : 460)
    closePolicy: Popup.CloseOnEscape

    DialogDrag { dialog: root }

    function resolve(choiceId) {
        root.chosen(choiceId)
        root.close()
    }

    onRejected: root.chosen(root.dismissChoiceId)

    footer: Item {
        implicitHeight: choiceRow.implicitHeight + 24

        Row {
            id: choiceRow
            anchors.right: parent.right
            anchors.rightMargin: 16
            anchors.verticalCenter: parent.verticalCenter
            spacing: 8

            Repeater {
                model: root.choices
                delegate: AppButton {
                    required property var modelData
                    objectName: "choiceDialogButton_" + modelData.id
                    text: modelData.label
                    emphasized: modelData.role === "accept"
                    onClicked: root.resolve(modelData.id)
                }
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: 10

        Text {
            objectName: "choiceDialogMessage"
            Layout.fillWidth: true
            text: root.message
            color: Theme.colors.text.active
            wrapMode: Text.WordWrap
        }
        Text {
            Layout.fillWidth: true
            Layout.maximumHeight: 220
            visible: root.details.length > 0
            text: root.details
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.secondaryFontSize
            wrapMode: Text.WordWrap
            elide: Text.ElideRight
        }
    }
}
