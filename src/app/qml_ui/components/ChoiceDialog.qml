import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// Shared notices/questions. Choices are { id, label, role } in display order;
// Escape resolves to dismissChoiceId, preserving the caller's cancellation policy.
AppDialog {
    id: root

    preferredHeight: implicitHeight

    property string message: ""
    property string details: ""
    property var choices: []
    property string dismissChoiceId: ""

    signal chosen(string choiceId)

    function resolve(choiceId) {
        root.chosen(choiceId)
        root.close()
    }

    onRejected: root.chosen(root.dismissChoiceId)

    footer: DialogFooter {
        choices: root.choices
        onChosen: choiceId => root.resolve(choiceId)
    }

    body: ColumnLayout {
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
            visible: root.details.length > 0
            text: root.details
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.secondaryFontSize
            wrapMode: Text.WordWrap
        }
    }
}
