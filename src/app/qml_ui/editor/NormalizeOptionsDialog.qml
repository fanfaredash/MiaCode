import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// Options for 整谱规范化. Opens over the editor, reports the chosen options and
// leaves the actual transform to the caller — the dialog owns no document state.
Dialog {
    id: root

    // Human-readable description of what will be normalized (whole chart, or a
    // line/column range), supplied by the caller.
    property string selectionDescription: ""

    // Seed values; the caller reads the same-named properties back on accept.
    property bool reduceTo384Grid: true
    property bool splitEveryFourMeasures: true
    property int sectionMeasureCount: 4
    property string syntax: "fpd"

    readonly property var sectionOptions: [4, 8]
    readonly property var syntaxOptions: ["fpd", "hinata"]

    title: qsTr("整谱规范化")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, Overlay.overlay ? Overlay.overlay.width - 48 : 420)
    standardButtons: Dialog.Ok | Dialog.Cancel
    closePolicy: Popup.CloseOnEscape

    // Splitting into sections is what the measure count applies to; without it
    // the number has no meaning, so bind availability rather than letting the
    // user set a value that is silently ignored.
    onAccepted: {
        root.reduceTo384Grid = reduceCheck.checked
        root.splitEveryFourMeasures = splitCheck.checked
        root.sectionMeasureCount = root.sectionOptions[sectionCombo.currentIndex]
        root.syntax = root.syntaxOptions[syntaxCombo.currentIndex]
    }

    onAboutToShow: {
        reduceCheck.checked = root.reduceTo384Grid
        splitCheck.checked = root.splitEveryFourMeasures
        sectionCombo.currentIndex = Math.max(0, root.sectionOptions.indexOf(root.sectionMeasureCount))
        syntaxCombo.currentIndex = Math.max(0, root.syntaxOptions.indexOf(root.syntax))
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            objectName: "normalizeSelectionDescription"
            Layout.fillWidth: true
            text: root.selectionDescription
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            wrapMode: Text.WordWrap
        }

        AppCheckBox {
            id: reduceCheck
            objectName: "normalizeReduce384Check"
            Layout.fillWidth: true
            text: qsTr("对齐到 384 分网格")
        }

        AppCheckBox {
            id: splitCheck
            objectName: "normalizeSplitCheck"
            Layout.fillWidth: true
            text: qsTr("按小节分段")
        }

        RowLayout {
            Layout.fillWidth: true
            enabled: splitCheck.checked
            Text {
                text: qsTr("每段小节数")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
            }
            AppComboBox {
                id: sectionCombo
                objectName: "normalizeSectionCombo"
                Layout.fillWidth: true
                model: root.sectionOptions
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTr("语法")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
            }
            AppComboBox {
                id: syntaxCombo
                objectName: "normalizeSyntaxCombo"
                Layout.fillWidth: true
                model: [qsTr("FPD"), qsTr("Hinata")]
            }
        }
    }
}
