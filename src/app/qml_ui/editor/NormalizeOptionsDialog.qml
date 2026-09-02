import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// Options for 整谱规范化. Opens over the editor, reports the chosen options and
// leaves the actual transform to the caller — the dialog owns no document state.
Dialog {
    id: root
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    // Human-readable description of what will be normalized (whole chart, or a
    // line/column range), supplied by the caller.
    property string selectionDescription: ""

    // Option lists come from the document model so the labels stay the ones the
    // Widgets dialog used ("每 4 小节" / "不分段" / "日向").
    required property var documentSession

    // Seed values; the caller reads the same-named properties back on accept.
    property bool reduceTo384Grid: true
    property int sectionMeasureCount: 4
    property string syntax: "fpd"

    readonly property var gridOptions: documentSession.normalizeGridOptions()
    readonly property var sectionOptions: documentSession.normalizeSectionOptions()
    readonly property var syntaxOptions: documentSession.normalizeSyntaxOptions()

    function indexOfValue(options, value) {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i
        }
        return 0
    }

    title: UiText.text("整谱规范化")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(420, Overlay.overlay ? Overlay.overlay.width - 48 : 420)
    footer: DialogFooter {
        acceptText: UiText.text("确定")
        cancelText: UiText.text("取消")
        onAccepted: root.accept()
        onRejected: root.reject()
    }
    closePolicy: Popup.CloseOnEscape

    onAccepted: {
        root.reduceTo384Grid = root.gridOptions[reduceCombo.currentIndex].value
        root.sectionMeasureCount = root.sectionOptions[sectionCombo.currentIndex].value
        root.syntax = root.syntaxOptions[syntaxCombo.currentIndex].value
    }

    onAboutToShow: {
        reduceCombo.currentIndex = root.indexOfValue(root.gridOptions, root.reduceTo384Grid)
        sectionCombo.currentIndex = root.indexOfValue(root.sectionOptions, root.sectionMeasureCount)
        syntaxCombo.currentIndex = root.indexOfValue(root.syntaxOptions, root.syntax)
    }

    contentItem: ColumnLayout {
        spacing: 12

        Text {
            objectName: "normalizeSelectionDescription"
            Layout.fillWidth: true
            text: root.selectionDescription
            color: Theme.colors.text.secondary
            wrapMode: Text.WordWrap
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: UiText.text("对齐到 384 分网格")
                color: Theme.colors.text.secondary
            }
            AppComboBox {
                id: reduceCombo
                objectName: "normalizeReduce384Combo"
                Layout.fillWidth: true
                textRole: "label"
                model: root.gridOptions
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: UiText.text("谱面分段")
                color: Theme.colors.text.secondary
            }
            AppComboBox {
                id: sectionCombo
                objectName: "normalizeSectionCombo"
                Layout.fillWidth: true
                textRole: "label"
                model: root.sectionOptions
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: UiText.text("整理语法")
                color: Theme.colors.text.secondary
            }
            AppComboBox {
                id: syntaxCombo
                objectName: "normalizeSyntaxCombo"
                Layout.fillWidth: true
                textRole: "label"
                model: root.syntaxOptions
            }
        }
    }
}
