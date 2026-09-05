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

    title: UiText.text("dialog.preferences.title")
    preferredWidth: 700
    preferredHeight: Theme.dialogHeight
    fillBody: true
    footer: DialogFooter {
        cancelText: UiText.text("关闭")
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
                model: [UiText.text("界面"), UiText.text("背景"), UiText.text("编辑器"), UiText.text("性能"), UiText.text("快捷键")]
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
                label: UiText.text("语言")
                options: root.preferencesModel.languageOptions
                currentValue: root.preferencesModel.languageToken
                onPicked: function(value) { root.preferencesModel.languageToken = value }
            }
            LabeledCombo {
                objectName: "preferencesThemeCombo"
                label: UiText.text("主题")
                options: root.preferencesModel.themeOptions
                currentValue: root.preferencesModel.themeToken
                onPicked: function(value) { root.preferencesModel.themeToken = value }
            }
            LabeledCombo {
                objectName: "preferencesPreviewSideCombo"
                label: UiText.text("预览位置")
                options: [{ value: false, label: UiText.text("右侧") }, { value: true, label: UiText.text("左侧") }]
                currentValue: root.preferencesModel.previewOnLeft
                onPicked: function(value) { root.preferencesModel.previewOnLeft = value }
            }
            Text {
                objectName: "preferencesRestartHint"
                Layout.fillWidth: true
                visible: root.preferencesModel.restartRequired
                text: UiText.text("语言与主题的更改将在重启后生效。")
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
                text: UiText.text("启用应用背景")
                checked: root.appBackground.enabled
                onToggled: root.appBackground.enabled = checked
            }
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: root.appBackground.imagePath.length > 0
                          ? root.appBackground.imagePath
                          : UiText.text("未选择背景图片")
                    color: root.appBackground.imageReadable
                           ? Theme.colors.text.primary : Theme.colors.text.secondary
                    elide: Text.ElideMiddle
                    font.family: Theme.uiFont
                }
                AppButton {
                    text: UiText.text("选择图片")
                    onClicked: root.appBackground.chooseImage()
                }
                AppButton {
                    text: UiText.text("清除")
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
                label: UiText.text("不透明度")
                from: 0; to: 1; stepSize: 0.01
                value: root.appBackground.opacity
                readout: Math.round(root.appBackground.opacity * 100) + "%"
                onMoved: function(value) { root.appBackground.opacity = value }
            }
            LabeledSlider {
                label: UiText.text("模糊")
                from: 0; to: 32; stepSize: 1
                value: root.appBackground.blur
                readout: Math.round(root.appBackground.blur)
                onMoved: function(value) { root.appBackground.blur = Math.round(value) }
            }
            LabeledCombo {
                label: UiText.text("缩放模式")
                options: root.appBackground.sizeModeOptions
                currentValue: root.appBackground.sizeMode
                onPicked: function(value) { root.appBackground.sizeMode = value }
            }
            LabeledCombo {
                label: UiText.text("位置")
                options: root.appBackground.positionOptions
                currentValue: root.appBackground.position
                onPicked: function(value) { root.appBackground.position = value }
            }

            Text {
                Layout.fillWidth: true
                text: UiText.text("背景覆盖层")
                color: Theme.colors.text.active
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                font.bold: true
            }
            LabeledSlider { label: UiText.text("面板（深色）"); from: 0; to: 255; value: root.appBackground.panelAlphaDark; readout: root.appBackground.panelAlphaDark; onMoved: function(v) { root.appBackground.panelAlphaDark = Math.round(v) } }
            LabeledSlider { label: UiText.text("面板（浅色）"); from: 0; to: 255; value: root.appBackground.panelAlphaLight; readout: root.appBackground.panelAlphaLight; onMoved: function(v) { root.appBackground.panelAlphaLight = Math.round(v) } }
        }

        // ---- 编辑器 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 2
            spacing: 10

            LabeledSlider {
                objectName: "preferencesFontSizeSlider"
                label: UiText.text("字号")
                from: root.preferencesModel.editorFontSizeMinimum
                to: root.preferencesModel.editorFontSizeMaximum
                value: root.preferencesModel.editorFontSize
                readout: root.preferencesModel.editorFontSize + " pt"
                onMoved: function(v) { root.preferencesModel.editorFontSize = Math.round(v) }
            }
            LabeledCombo {
                objectName: "preferencesLineSpacingCombo"
                label: UiText.text("行距")
                options: root.preferencesModel.lineSpacingOptions
                currentValue: root.preferencesModel.editorLineSpacing
                onPicked: function(value) { root.preferencesModel.editorLineSpacing = value }
            }
            AppSwitch {
                objectName: "preferencesAutoCompletionSwitch"
                text: UiText.text("自动补全")
                checked: root.preferencesModel.editorAutoCompletion
                onToggled: root.preferencesModel.editorAutoCompletion = checked
            }
            AppSwitch {
                objectName: "preferencesHalfWidthSwitch"
                text: UiText.text("半角输入转换")
                checked: root.preferencesModel.editorHalfWidthInput
                onToggled: root.preferencesModel.editorHalfWidthInput = checked
            }
            AppSwitch {
                objectName: "preferencesImeSwitch"
                text: UiText.text("禁用输入法")
                checked: root.preferencesModel.editorImeDisabled
                onToggled: root.preferencesModel.editorImeDisabled = checked
            }
        }

        // ---- 性能 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 3
            spacing: 10

            LabeledCombo {
                objectName: "preferencesVideoDecodeCombo"
                label: UiText.text("视频解码")
                options: [{ value: false, label: UiText.text("硬件解码") }, { value: true, label: UiText.text("软件解码") }]
                currentValue: root.preferencesModel.videoDecodePrefersSoftware
                onPicked: function(value) { root.preferencesModel.videoDecodePrefersSoftware = value }
            }
            LabeledCombo {
                objectName: "preferencesCanvasFrameRateCombo"
                label: UiText.text("画布帧率")
                options: root.preferencesModel.canvasFrameRateOptions
                currentValue: root.preferencesModel.canvasFrameRateMode
                onPicked: function(value) { root.preferencesModel.canvasFrameRateMode = value }
            }
            LabeledCombo {
                objectName: "preferencesPvFrameRateCombo"
                label: UiText.text("PV 帧率")
                options: root.preferencesModel.appFrameRateOptions
                currentValue: root.preferencesModel.stageMediaFrameRateMode
                onPicked: function(value) { root.preferencesModel.stageMediaFrameRateMode = value }
            }
            LabeledCombo {
                objectName: "preferencesTimelineFrameRateCombo"
                label: UiText.text("时间轴帧率")
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
                      ? UiText.text("按下新的快捷键，Esc 取消。")
                      : UiText.text("点击一行以录制新的快捷键。")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
            }

            ListView {
                id: shortcutList
                objectName: "shortcutList"
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
                                text: root.preferences.localizedText(shortcutRow.modelData.labelKey)
                                      || shortcutRow.modelData.labelFallback
                                elide: Text.ElideRight
                                color: Theme.colors.text.active
                                font.family: Theme.uiFont
                                verticalAlignment: Text.AlignVCenter
                            }
                            Text {
                                Layout.preferredWidth: 170
                                text: root.capturingId === shortcutRow.modelData.id
                                      ? UiText.text("录制中…")
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
                        text: UiText.text("恢复默认")
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
                text: UiText.text("全部恢复默认")
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
