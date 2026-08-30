import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// Footer for the shell's dialogs. Dialog.standardButtons renders through the
// Qt Quick Controls style rather than the app's own controls, which is why
// those buttons had no hover state — this uses AppButton so every dialog's
// footer behaves like the rest of the UI.
//
// An empty label omits that button; dialogs that only dismiss set cancelText
// alone.
Item {
    id: root

    property string acceptText: ""
    property string cancelText: ""
    property bool acceptEnabled: true
    property bool acceptEmphasized: true

    signal accepted()
    signal rejected()

    implicitHeight: row.implicitHeight + 24
    implicitWidth: row.implicitWidth + 32

    RowLayout {
        id: row
        anchors.right: parent.right
        anchors.rightMargin: 16
        anchors.verticalCenter: parent.verticalCenter
        spacing: 8

        AppButton {
            objectName: "dialogFooterCancel"
            visible: root.cancelText.length > 0
            text: root.cancelText
            onClicked: root.rejected()
        }
        AppButton {
            objectName: "dialogFooterAccept"
            visible: root.acceptText.length > 0
            enabled: root.acceptEnabled
            emphasized: root.acceptEmphasized
            text: root.acceptText
            onClicked: root.accepted()
        }
    }
}
