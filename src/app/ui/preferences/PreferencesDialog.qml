import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 偏好设置. Every control writes straight through to the model, which persists
// on each change — there is no OK/Apply, matching the Widgets dialog it
// replaces. Language and theme are the exceptions: they need a restart, so the
// page says so instead of pretending the change already took.
//
// 快捷键 is a page here, not a dialog of its own: a modal opened from a modal
// stacked two scrims over the same settings and put the key capture behind an
// extra Escape.
AppDialog {
    id: root

    required property var preferencesModel
    required property var shortcuts
    required property var preferences
    required property var appBackground

    title: qsTrId("dialog.preferences.title")
    preferredWidth: 700
    preferredHeight: Theme.dialogHeight
    fillBody: true
    footer: DialogFooter {
        cancelText: qsTrId("action.close")
        onRejected: root.reject()
    }

    property int activePage: 0
    // Id of the command whose binding is being recorded; "" when idle.
    property string capturingId: ""
    property var shortcutRows: []

    onActivePageChanged: root.capturingId = ""
    onAboutToShow: {
        root.capturingId = ""
        root.refreshShortcuts()
    }

    // Reassigning the model resets ListView.contentY, which threw the reader
    // back to the top of the list after every recorded binding. Restore the
    // scroll position around the rebuild.
    function refreshShortcuts() {
        const keepY = shortcutList.contentY
        root.shortcutRows = root.shortcuts.editableShortcuts()
        shortcutList.contentY = Math.max(
            0, Math.min(keepY, Math.max(0, shortcutList.contentHeight - shortcutList.height)))
    }

    function describeShortcut(event) {
        // Modifier-only presses keep the capture armed: they are the first half
        // of a chord, not a binding.
        if (event.key === Qt.Key_Control || event.key === Qt.Key_Shift
                || event.key === Qt.Key_Alt || event.key === Qt.Key_Meta)
            return ""
        let parts = []
        if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
        if (event.modifiers & Qt.AltModifier) parts.push("Alt")
        if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
        if (event.modifiers & Qt.MetaModifier) parts.push("Meta")
        parts.push(root.shortcuts.keyName(event.key))
        return parts.join("+")
    }

    body: ColumnLayout {
        spacing: 10

        Row {
            spacing: 4
            Repeater {
                model: [qsTrId("qml.interface"), qsTrId("dialog.preferences.background_group"), qsTrId("dialog.preferences.editor_group"), qsTrId("dialog.preferences.performance_group"), qsTrId("dialog.preferences.shortcuts_group")]
                delegate: AppTab {
                    required property int index
                    required property string modelData
                    panelTab: true
                    text: modelData
                    active: root.activePage === index
                    onClicked: root.activePage = index
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colors.border.normal
        }

        // ---- 界面 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 0
            spacing: 10

            LabeledCombo {
                objectName: "preferencesLanguageCombo"
                label: qsTrId("dialog.preferences.language")
                options: root.preferencesModel.languageOptions
                currentValue: root.preferencesModel.languageToken
                onPicked: function(value) { root.preferencesModel.languageToken = value }
            }
            LabeledCombo {
                objectName: "preferencesThemeCombo"
                label: qsTrId("dialog.preferences.theme")
                options: root.preferencesModel.themeOptions
                currentValue: root.preferencesModel.themeToken
                onPicked: function(value) { root.preferencesModel.themeToken = value }
            }
            LabeledCombo {
                objectName: "preferencesPreviewSideCombo"
                label: qsTrId("dialog.preferences.preview_side")
                options: [{ value: false, label: qsTrId("qml.right") }, { value: true, label: qsTrId("qml.left") }]
                currentValue: root.preferencesModel.previewOnLeft
                onPicked: function(value) { root.preferencesModel.previewOnLeft = value }
            }
            Text {
                objectName: "preferencesRestartHint"
                Layout.fillWidth: true
                visible: root.preferencesModel.restartRequired
                text: qsTrId("qml.theme_changes_take_effect_after_restarting")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                wrapMode: Text.WordWrap
            }
        }

        // ---- 背景 ----
        ColumnLayout {
            objectName: "preferencesBackgroundPage"
            Layout.fillWidth: true
            visible: root.activePage === 1
            spacing: 10

            AppSwitch {
                text: qsTrId("qml.enable_application_background")
                checked: root.appBackground.enabled
                onToggled: root.appBackground.enabled = checked
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: root.appBackground.imagePath.length > 0
                          ? root.appBackground.imagePath
                          : qsTrId("qml.no_background_image_selected")
                    color: root.appBackground.imageReadable
                           ? Theme.colors.text.primary : Theme.colors.text.secondary
                    elide: Text.ElideMiddle
                    font.family: Theme.uiFont
                }
                AppButton {
                    text: qsTrId("cover.choose_image")
                    onClicked: root.appBackground.chooseImage()
                }
                AppButton {
                    text: qsTrId("dialog.preferences.background.clear")
                    enabled: root.appBackground.imagePath.length > 0
                    onClicked: root.appBackground.clearImage()
                }
            }
            Text {
                Layout.fillWidth: true
                visible: root.appBackground.errorMessage.length > 0
                text: root.appBackground.errorMessage
                color: Theme.colors.syntax.error
                font.family: Theme.uiFont
                wrapMode: Text.WordWrap
            }
            LabeledSlider {
                label: qsTrId("qml.image_opacity")
                from: 0.1; to: 0.8; stepSize: 0.01
                value: root.appBackground.opacity
                readout: Math.round(root.appBackground.opacity * 100) + "%"
                onMoved: function(value) { root.appBackground.opacity = value }
            }
            LabeledSlider {
                label: qsTrId("qml.background_mask_opacity")
                from: 0; to: 1; stepSize: 0.01
                value: root.appBackground.panelAlpha / 255.0
                readout: Math.round(root.appBackground.panelAlpha / 255.0 * 100) + "%"
                onMoved: function(value) { root.appBackground.panelAlpha = Math.round(value * 255) }
            }
            LabeledSlider {
                label: qsTrId("qml.blur_radius")
                from: 0; to: 64; stepSize: 1
                value: root.appBackground.blur
                readout: Math.round(root.appBackground.blur)
                onMoved: function(value) { root.appBackground.blur = Math.round(value) }
            }
            LabeledCombo {
                label: qsTrId("qml.scale_mode")
                options: [
                    { value: "cover", label: qsTrId("dialog.preferences.background.scale.cover") },
                    { value: "contain", label: qsTrId("dialog.preferences.background.scale.contain") },
                    { value: "stretch", label: qsTrId("dialog.preferences.background.scale.stretch") },
                    { value: "center", label: qsTrId("dialog.preferences.background.scale.center") },
                    { value: "repeat", label: qsTrId("dialog.preferences.background.scale.repeat") }
                ]
                currentValue: root.appBackground.sizeMode
                onPicked: function(value) { root.appBackground.sizeMode = value }
            }
            LabeledCombo {
                label: qsTrId("dialog.preferences.background.position")
                options: [
                    { value: "center", label: qsTrId("dialog.preferences.background.position.center") },
                    { value: "left", label: qsTrId("dialog.preferences.background.position.left") },
                    { value: "right", label: qsTrId("dialog.preferences.background.position.right") },
                    { value: "top", label: qsTrId("dialog.preferences.background.position.top") },
                    { value: "bottom", label: qsTrId("dialog.preferences.background.position.bottom") },
                    { value: "left_top", label: qsTrId("dialog.preferences.background.position.left_top") },
                    { value: "right_top", label: qsTrId("dialog.preferences.background.position.right_top") },
                    { value: "left_bottom", label: qsTrId("dialog.preferences.background.position.left_bottom") },
                    { value: "right_bottom", label: qsTrId("dialog.preferences.background.position.right_bottom") }
                ]
                currentValue: root.appBackground.position
                onPicked: function(value) { root.appBackground.position = value }
            }

        }

        // ---- 编辑器 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 2
            spacing: 10

            LabeledSlider {
                objectName: "preferencesFontSizeSlider"
                label: qsTrId("dialog.preferences.editor_font_size")
                from: root.preferencesModel.editorFontSizeMinimum
                to: root.preferencesModel.editorFontSizeMaximum
                value: root.preferencesModel.editorFontSize
                readout: root.preferencesModel.editorFontSize + " pt"
                onMoved: function(v) { root.preferencesModel.editorFontSize = Math.round(v) }
            }
            LabeledCombo {
                objectName: "preferencesLineSpacingCombo"
                label: qsTrId("dialog.preferences.editor_line_spacing")
                options: root.preferencesModel.lineSpacingOptions
                currentValue: root.preferencesModel.editorLineSpacing
                onPicked: function(value) { root.preferencesModel.editorLineSpacing = value }
            }
            LabeledCombo {
                objectName: "preferencesInputHandlingCombo"
                label: qsTrId("preferences.input_handling")
                options: [
                    { value: 0, label: qsTrId("preferences.correct_full_width_only") },
                    { value: 1, label: qsTrId("preferences.block_input_methods_and_correct_full_width") },
                    { value: 2, label: qsTrId("preferences.leave_input_unchanged") }
                ]
                currentValue: root.preferencesModel.editorInputHandlingMode
                onPicked: function(value) { root.preferencesModel.editorInputHandlingMode = value }
            }
            AppSwitch {
                objectName: "preferencesAutoCompletionSwitch"
                text: qsTrId("preferences.auto_completion")
                checked: root.preferencesModel.editorAutoCompletion
                onToggled: root.preferencesModel.editorAutoCompletion = checked
            }
            AppSwitch {
                objectName: "preferencesScrollPastEndSwitch"
                text: qsTrId("preferences.editor_scroll_past_end")
                checked: root.preferencesModel.editorScrollPastEnd
                onToggled: root.preferencesModel.editorScrollPastEnd = checked
            }
            AppSwitch {
                objectName: "preferencesSelectionBeatDisplaySwitch"
                text: qsTrId("preferences.editor_selection_beat_display")
                checked: root.preferencesModel.editorSelectionBeatDisplay
                onToggled: root.preferencesModel.editorSelectionBeatDisplay = checked
            }
        }

        // ---- 性能 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 3
            spacing: 10

            LabeledCombo {
                objectName: "preferencesVideoDecodeCombo"
                label: qsTrId("qml.video_decoding")
                options: [{ value: false, label: qsTrId("qml.hardware_decoding") }, { value: true, label: qsTrId("qml.software_decoding") }]
                currentValue: root.preferencesModel.videoDecodePrefersSoftware
                onPicked: function(value) { root.preferencesModel.videoDecodePrefersSoftware = value }
            }
            LabeledCombo {
                objectName: "preferencesCanvasFrameRateCombo"
                label: qsTrId("qml.canvas_frame_rate")
                options: root.preferencesModel.canvasFrameRateOptions
                currentValue: root.preferencesModel.canvasFrameRateMode
                onPicked: function(value) { root.preferencesModel.canvasFrameRateMode = value }
            }
            LabeledCombo {
                objectName: "preferencesPvFrameRateCombo"
                label: qsTrId("qml.pv_frame_rate")
                options: root.preferencesModel.appFrameRateOptions
                currentValue: root.preferencesModel.stageMediaFrameRateMode
                onPicked: function(value) { root.preferencesModel.stageMediaFrameRateMode = value }
            }
            LabeledCombo {
                objectName: "preferencesTimelineFrameRateCombo"
                label: qsTrId("qml.timeline_frame_rate")
                options: root.preferencesModel.appFrameRateOptions
                currentValue: root.preferencesModel.timelineFrameRateMode
                onPicked: function(value) { root.preferencesModel.timelineFrameRateMode = value }
            }
        }

        // ---- 快捷键 ----
        // Clicking a row arms capture: the next key press carrying at least one
        // non-modifier becomes that command's binding. Escape cancels the
        // capture rather than closing 偏好设置, so an accidental arm cannot lose
        // the row being edited.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.activePage === 4
            spacing: 8

            Text {
                Layout.fillWidth: true
                text: root.capturingId.length > 0
                      ? qsTrId("qml.press_a_new_shortcut_esc_cancels")
                      : qsTrId("qml.click_a_row_to_record_a_new_shortcut")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
            }

            ListView {
                id: shortcutList
                objectName: "shortcutList"
                property bool reservesPlainSpace: root.capturingId.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                focus: true
                model: root.shortcutRows
                ScrollBar.vertical: AppScrollBar {}

                Keys.onPressed: function(event) {
                    if (root.capturingId.length === 0)
                        return
                    event.accepted = true
                    if (event.key === Qt.Key_Escape) {
                        root.capturingId = ""
                        return
                    }
                    const text = root.describeShortcut(event)
                    if (text.length === 0)
                        return
                    root.shortcuts.setShortcutText(root.capturingId, text)
                    root.capturingId = ""
                    root.refreshShortcuts()
                }

                // The row is NOT one big button: the reset button sits beside
                // the clickable area, not inside it, so the highlight stops
                // where the recording hit area stops.
                delegate: RowLayout {
                    id: shortcutRow
                    required property var modelData
                    width: ListView.view.width
                    spacing: 8

                    ChromeRow {
                        id: captureArea
                        Layout.fillWidth: true
                        implicitHeight: 34
                        selected: root.capturingId === shortcutRow.modelData.id
                        onClicked: {
                            root.capturingId = shortcutRow.modelData.id
                            shortcutList.forceActiveFocus()
                        }
                        contentItem: RowLayout {
                            spacing: 8
                            Text {
                                Layout.fillWidth: true
                                text: qsTrId(shortcutRow.modelData.labelKey)
                                      || shortcutRow.modelData.labelFallback
                                elide: Text.ElideRight
                                color: Theme.colors.text.active
                                font.family: Theme.uiFont
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                Layout.preferredWidth: 170
                                text: root.capturingId === shortcutRow.modelData.id
                                      ? qsTrId("qml.recording")
                                      : shortcutRow.modelData.shortcutText
                                color: shortcutRow.modelData.isDefault
                                       ? Theme.colors.text.secondary
                                       : Theme.colors.accent.primary
                                font.family: Theme.uiFont
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }

                    AppButton {
                        text: qsTrId("qml.restore_defaults")
                        enabled: !shortcutRow.modelData.isDefault
                        onClicked: {
                            root.shortcuts.resetShortcut(shortcutRow.modelData.id)
                            root.refreshShortcuts()
                        }
                    }
                }
            }

            AppButton {
                objectName: "preferencesResetShortcutsButton"
                Layout.alignment: Qt.AlignLeft
                text: qsTrId("qml.restore_all_defaults")
                onClicked: {
                    root.shortcuts.resetAllShortcuts()
                    root.refreshShortcuts()
                }
            }
        }

        // The shortcut page owns the slack itself; this only pads the pages
        // whose controls are a short stack at the top.
        Item {
            Layout.fillHeight: true
            visible: root.activePage !== 4
        }
    }

}
