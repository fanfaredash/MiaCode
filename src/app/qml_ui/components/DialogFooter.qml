import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared actions for settings, confirmations and multi-choice notices.
Item {
    id: root

    property string acceptText: ""
    property string cancelText: ""
    property bool acceptEnabled: true
    property bool acceptEmphasized: true
    property var choices: {
        const actions = []
        if (cancelText.length > 0)
            actions.push({ id: "reject", label: cancelText })
        if (acceptText.length > 0)
            actions.push({ id: "accept", label: acceptText,
                           role: acceptEmphasized ? "accept" : "", enabled: acceptEnabled })
        return actions
    }

    signal accepted()
    signal rejected()
    signal chosen(string choiceId)

    implicitHeight: row.implicitHeight + 2 * Theme.dialogPadding

    Flow {
        id: row
        anchors.right: parent.right
        anchors.rightMargin: Theme.dialogPadding
        y: Theme.dialogPadding
        width: parent.width - 2 * Theme.dialogPadding
        spacing: Theme.panelPadding
        layoutDirection: Qt.RightToLeft

        Repeater {
            model: root.choices.slice().reverse()
            delegate: AppButton {
                required property var modelData
                objectName: modelData.id === "accept" ? "dialogFooterAccept"
                          : modelData.id === "reject" ? "dialogFooterCancel"
                          : "choiceDialogButton_" + modelData.id
                text: modelData.label
                width: Math.min(implicitWidth, row.width)
                enabled: modelData.enabled !== false
                emphasized: modelData.role === "accept"
                onClicked: {
                    root.chosen(modelData.id)
                    if (modelData.id === "accept") root.accepted()
                    else if (modelData.id === "reject") root.rejected()
                }
            }
        }
    }
}
