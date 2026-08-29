import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 偏好设置. Every control writes straight through to the model, which persists
// on each change — there is no OK/Apply, matching the Widgets dialog it
// replaces. Language and theme are the exceptions: they need a restart, so the
// page says so instead of pretending the change already took.
Dialog {
    id: root

    required property var preferencesModel
    required property var shortcuts
    required property var preferences

    title: qsTr("偏好设置")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 48 : 640)
    height: Math.min(520, Overlay.overlay ? Overlay.overlay.height - 48 : 520)
    standardButtons: Dialog.Close
    closePolicy: Popup.CloseOnEscape

    property int activePage: 0

    function indexOfValue(options, value) {
        for (let i = 0; i < options.length; ++i) {
            if (options[i].value === value)
                return i
        }
        return 0
    }

    contentItem: ColumnLayout {
        spacing: 10

        Row {
            spacing: 4
            Repeater {
                model: [qsTr("界面"), qsTr("编辑器"), qsTr("性能"), qsTr("快捷键")]
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
            height: 1
            color: Theme.colors.border.normal
        }

        // ---- 界面 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 0
            spacing: 10

            LabeledCombo {
                objectName: "preferencesLanguageCombo"
                label: qsTr("语言")
                options: root.preferencesModel.languageOptions
                currentValue: root.preferencesModel.languageToken
                onPicked: function(value) { root.preferencesModel.languageToken = value }
            }
            LabeledCombo {
                objectName: "preferencesThemeCombo"
                label: qsTr("主题")
                options: root.preferencesModel.themeOptions
                currentValue: root.preferencesModel.themeToken
                onPicked: function(value) { root.preferencesModel.themeToken = value }
            }
            LabeledCombo {
                objectName: "preferencesPreviewSideCombo"
                label: qsTr("预览位置")
                options: [{ value: false, label: qsTr("右侧") }, { value: true, label: qsTr("左侧") }]
                currentValue: root.preferencesModel.previewOnLeft
                onPicked: function(value) { root.preferencesModel.previewOnLeft = value }
            }
            Text {
                objectName: "preferencesRestartHint"
                Layout.fillWidth: true
                visible: root.preferencesModel.restartRequired
                text: qsTr("语言与主题的更改将在重启后生效。")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                wrapMode: Text.WordWrap
            }
        }

        // ---- 编辑器 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 1
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.preferredWidth: 120
                    text: qsTr("字号")
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                }
                AppSlider {
                    objectName: "preferencesFontSizeSlider"
                    Layout.fillWidth: true
                    from: root.preferencesModel.editorFontSizeMinimum
                    to: root.preferencesModel.editorFontSizeMaximum
                    stepSize: 1
                    value: root.preferencesModel.editorFontSize
                    onMoved: root.preferencesModel.editorFontSize = Math.round(value)
                }
                Text {
                    text: root.preferencesModel.editorFontSize + " pt"
                    color: Theme.colors.text.active
                    font.family: Theme.uiFont
                }
            }
            LabeledCombo {
                objectName: "preferencesLineSpacingCombo"
                label: qsTr("行距")
                options: root.preferencesModel.lineSpacingOptions
                currentValue: root.preferencesModel.editorLineSpacing
                onPicked: function(value) { root.preferencesModel.editorLineSpacing = value }
            }
            AppSwitch {
                objectName: "preferencesAutoCompletionSwitch"
                text: qsTr("自动补全")
                checked: root.preferencesModel.editorAutoCompletion
                onToggled: root.preferencesModel.editorAutoCompletion = checked
            }
            AppSwitch {
                objectName: "preferencesHalfWidthSwitch"
                text: qsTr("半角输入转换")
                checked: root.preferencesModel.editorHalfWidthInput
                onToggled: root.preferencesModel.editorHalfWidthInput = checked
            }
            AppSwitch {
                objectName: "preferencesImeSwitch"
                text: qsTr("禁用输入法")
                checked: root.preferencesModel.editorImeDisabled
                onToggled: root.preferencesModel.editorImeDisabled = checked
            }
        }

        // ---- 性能 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 2
            spacing: 10

            LabeledCombo {
                objectName: "preferencesVideoDecodeCombo"
                label: qsTr("视频解码")
                options: [{ value: false, label: qsTr("硬件解码") }, { value: true, label: qsTr("软件解码") }]
                currentValue: root.preferencesModel.videoDecodePrefersSoftware
                onPicked: function(value) { root.preferencesModel.videoDecodePrefersSoftware = value }
            }
            LabeledCombo {
                objectName: "preferencesCanvasFrameRateCombo"
                label: qsTr("画布帧率")
                options: root.preferencesModel.canvasFrameRateOptions
                currentValue: root.preferencesModel.canvasFrameRateMode
                onPicked: function(value) { root.preferencesModel.canvasFrameRateMode = value }
            }
            LabeledCombo {
                objectName: "preferencesPvFrameRateCombo"
                label: qsTr("PV 帧率")
                options: root.preferencesModel.appFrameRateOptions
                currentValue: root.preferencesModel.stageMediaFrameRateMode
                onPicked: function(value) { root.preferencesModel.stageMediaFrameRateMode = value }
            }
            LabeledCombo {
                objectName: "preferencesTimelineFrameRateCombo"
                label: qsTr("时间轴帧率")
                options: root.preferencesModel.appFrameRateOptions
                currentValue: root.preferencesModel.timelineFrameRateMode
                onPicked: function(value) { root.preferencesModel.timelineFrameRateMode = value }
            }
        }

        // ---- 快捷键 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 3
            spacing: 10

            AppButton {
                objectName: "preferencesEditShortcutsButton"
                text: qsTr("编辑快捷键...")
                onClicked: shortcutEditor.open()
            }
            AppButton {
                objectName: "preferencesResetShortcutsButton"
                text: qsTr("全部恢复默认")
                onClicked: root.shortcuts.resetAllShortcuts()
            }
        }

        Item { Layout.fillHeight: true }
    }

    component LabeledCombo: RowLayout {
        id: labeledCombo
        required property string label
        property var options: []
        property var currentValue
        signal picked(var value)
        Layout.fillWidth: true
        Text {
            Layout.preferredWidth: 120
            text: labeledCombo.label
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
        }
        AppComboBox {
            Layout.fillWidth: true
            textRole: "label"
            model: labeledCombo.options
            currentIndex: root.indexOfValue(labeledCombo.options, labeledCombo.currentValue)
            onActivated: function(index) {
                labeledCombo.picked(labeledCombo.options[index].value)
            }
        }
    }

    ShortcutEditorDialog {
        id: shortcutEditor
        objectName: "shortcutEditorDialog"
        shortcuts: root.shortcuts
        preferences: root.preferences
    }
}
