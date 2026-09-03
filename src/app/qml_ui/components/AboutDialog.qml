import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 关于 MiaCode. Everything shown is a fact about the build, read through
// preferences.aboutInfo() so the version macro and UiText stay the sources.
AppDialog {
    id: root

    enter: FadeTransition {}
    exit: FadeTransition { appearing: false }
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    required property var preferences

    readonly property var info: preferences.aboutInfo()

    title: root.info.title
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, Overlay.overlay ? Overlay.overlay.width - 48 : 420)
    footer: DialogFooter {
        acceptText: UiText.text("确定")
        acceptEmphasized: true
        onAccepted: root.accept()
    }
    closePolicy: Popup.CloseOnEscape

    contentItem: ColumnLayout {
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Image {
                Layout.preferredWidth: 48
                Layout.preferredHeight: 48
                source: "qrc:/icons/app.png"
                sourceSize: Qt.size(96, 96)
                fillMode: Image.PreserveAspectFit
            }

            ColumnLayout {
                spacing: 2
                Text {
                    text: "MiaCode"
                    color: Theme.colors.text.active
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize + 6
                    font.bold: true
                }
                Text {
                    objectName: "aboutVersion"
                    text: "v" + root.info.version
                    color: Theme.colors.text.secondary
                }
            }
            Item { Layout.fillWidth: true }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 12
            rowSpacing: 6

            InfoLabel { text: root.info.platformLabel }
            // Selectable: these are the two lines a bug report asks for.
            InfoValue { text: root.info.platform }
            InfoLabel { text: root.info.buildTypeLabel }
            InfoValue { text: root.info.buildType }
        }
    }

    component InfoLabel: Text {
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize
    }

    component InfoValue: TextEdit {
        Layout.fillWidth: true
        readOnly: true
        selectByMouse: true
        color: Theme.colors.text.primary
        selectionColor: Theme.colors.state.textSelection
        selectedTextColor: Theme.colors.text.active
        font.family: Theme.uiFont
        font.pixelSize: Theme.secondaryFontSize
        wrapMode: TextEdit.Wrap
    }
}
