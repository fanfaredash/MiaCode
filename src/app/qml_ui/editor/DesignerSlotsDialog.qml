import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 谱师名义管理: the seven &des_1..7 slots plus the "all difficulties share one
// name" mode, replacing v1's per-difficulty designer dialog and the v2 toolbar
// switch. Nothing reaches the document until 确定 — the rows and the checkbox
// are edited on a local copy, so 取消 is a clean no-op.
AppDialog {
    id: root

    required property var documentSession
    required property var commands

    // Local copy of the mode and the name it settled on. Committed together
    // with the rows as one backend transaction.
    property bool unified: false
    property string canonicalName: ""

    title: UiText.text("document.designer_management")
    preferredWidth: 460
    preferredHeight: implicitHeight

    footer: DialogFooter {
        acceptText: UiText.text("确定")
        cancelText: UiText.text("取消")
        onAccepted: root.accept()
        onRejected: root.reject()
    }

    onAboutToShow: root.reloadSlots()
    onAccepted: {
        const slots = []
        for (let i = 0; i < slotModel.count; ++i) {
            const entry = slotModel.get(i)
            slots.push({ id: entry.slotId, designer: entry.designer })
        }
        root.commands.applyDesignerSlots(slots, root.unified, root.canonicalName)
    }

    // Seeds the rows from the document every time the dialog opens, so a
    // cancelled edit never survives into the next visit.
    function reloadSlots() {
        slotModel.clear()
        const slots = root.documentSession.designerSlots
        for (let i = 0; i < slots.length; ++i) {
            slotModel.append({
                slotId: slots[i].id,
                slotName: slots[i].name,
                designer: slots[i].designer,
                hasChart: slots[i].hasChart
            })
        }
        root.unified = root.documentSession.unifiedDesignerEnabled
        root.canonicalName = root.unified ? root.documentSession.metadataDesigner : ""
        root.syncRowEditors()
    }

    // The row editors are written imperatively rather than bound: typing in a
    // TextField breaks a `text:` binding, and unifying has to be able to push
    // the shared name back into a field the user already touched.
    function syncRowEditors() {
        for (let i = 0; i < slotRepeater.count; ++i) {
            const row = slotRepeater.itemAt(i)
            if (row)
                row.editorText = slotModel.get(i).designer
        }
    }

    // Distinct non-empty names across the top &des and the seven rows, in the
    // order they appear — the candidates a unify has to choose between.
    function distinctNames() {
        const names = []
        const consider = function(value) {
            const trimmed = (value || "").trim()
            if (trimmed.length > 0 && names.indexOf(trimmed) < 0)
                names.push(trimmed)
        }
        consider(root.documentSession.metadataDesigner)
        for (let i = 0; i < slotModel.count; ++i)
            consider(slotModel.get(i).designer)
        return names
    }

    // Every charted difficulty — and any slot the user has already named —
    // takes the shared name. A chart-less, unnamed slot stays empty so
    // unifying never materializes a `&des_N` for a slot nobody touched.
    function applyUnifiedName(name) {
        root.unified = true
        root.canonicalName = name
        for (let i = 0; i < slotModel.count; ++i) {
            const entry = slotModel.get(i)
            const slotGetsName = entry.hasChart || entry.designer.length > 0
            slotModel.setProperty(i, "designer", slotGetsName ? name : "")
        }
        root.syncRowEditors()
    }

    function beginUnify() {
        const candidates = root.distinctNames()
        if (candidates.length <= 1) {
            root.applyUnifiedName(candidates.length === 1 ? candidates[0] : "")
            return
        }
        canonicalPicker.candidates = candidates
        canonicalPicker.open()
    }

    ListModel {
        id: slotModel
    }

    body: ColumnLayout {
        spacing: 10

        Repeater {
            id: slotRepeater
            model: slotModel

            delegate: RowLayout {
                id: slotRow
                required property int index
                required property int slotId
                required property string slotName
                required property bool hasChart
                property alias editorText: slotEditor.text

                Layout.fillWidth: true
                spacing: 8

                // A slot without a chart is dimmed and explains itself on
                // hover, so a name there reads as a deliberate designer-only
                // record rather than a difficulty that failed to load.
                Label {
                    Layout.preferredWidth: 96
                    text: slotRow.slotName
                    color: slotRow.hasChart
                        ? Theme.colors.text.primary
                        : Theme.colors.text.disabled
                    elide: Text.ElideRight
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
                    ToolTip.text: UiText.text("document.no_chart_yet_records_des")
                        .arg(slotRow.slotId)
                    ToolTip.visible: !slotRow.hasChart && slotLabelHover.hovered
                    HoverHandler { id: slotLabelHover }
                }

                AppTextField {
                    id: slotEditor
                    Layout.fillWidth: true
                    enabled: !root.unified
                    placeholderText: "&des_%1=".arg(slotRow.slotId)
                    onTextEdited: slotModel.setProperty(slotRow.index, "designer", text)
                }
            }
        }

        AppCheckBox {
            id: unifiedBox
            Layout.fillWidth: true
            text: UiText.text("document.all_difficulties_share_this_designer")

            onToggled: {
                if (checked === root.unified)
                    return
                if (!checked) {
                    root.unified = false
                    return
                }
                // The picker answers asynchronously, so the box follows
                // root.unified rather than the click that opened it.
                checked = root.unified
                root.beginUnify()
            }

            Binding {
                target: unifiedBox
                property: "checked"
                value: root.unified
            }
        }
    }

    AppDialog {
        id: canonicalPicker

        property var candidates: []
        // "直接清除" sits past the last real candidate, and means an empty
        // shared name: every &des_N is cleared instead of picking a winner.
        readonly property int clearAllIndex: candidates.length

        title: UiText.text("document.pick_the_canonical_designer")
        preferredWidth: 420
        preferredHeight: Theme.dialogCompactHeight

        footer: DialogFooter {
            acceptText: UiText.text("确定")
            cancelText: UiText.text("取消")
            onAccepted: canonicalPicker.accept()
            onRejected: canonicalPicker.reject()
        }

        onAboutToShow: designerChoice.currentIndex = 0
        onAccepted: root.applyUnifiedName(
            designerChoice.currentIndex === canonicalPicker.clearAllIndex
                ? "" : canonicalPicker.candidates[designerChoice.currentIndex])

        body: ColumnLayout {
            spacing: 8

            Label {
                Layout.fillWidth: true
                text: UiText.text("document.multiple_distinct_designer_names_were_detected")
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                wrapMode: Text.WordWrap
            }

            AppComboBox {
                id: designerChoice
                Layout.fillWidth: true
                model: canonicalPicker.candidates.concat([UiText.text("document.clear_all")])
            }
        }
    }
}
