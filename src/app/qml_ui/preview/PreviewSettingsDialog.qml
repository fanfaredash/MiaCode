import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 预览设置. Like 音频设置 and 偏好设置, every control writes straight through and
// persists on the spot — there is no OK/Apply.
//
// 视频、玩法和皮肤 all describe the running preview. 性能只含预览刷新率，仍放在
// 偏好设置 → 性能，避免同一设置有两个入口。
Dialog {
    id: root
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize
    required property var previewSettings

    readonly property var values: root.previewSettings.values
    readonly property var labels: root.previewSettings.labels

    property int activePage: 0

    title: UiText.text("预览设置")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 48 : 640)
    footer: DialogFooter {
        cancelText: UiText.text("关闭")
        onRejected: root.reject()
    }
    closePolicy: Popup.CloseOnEscape

    // Drag by the title bar: these change what the preview shows.
    DialogDrag { dialog: root }

    function put(key, value) { root.previewSettings.setValue(key, value) }

    function fontIndexForPath(options, path) {
        const target = path || ""
        if (!options)
            return 0
        for (let index = 0; index < options.length; ++index) {
            if ((options[index].path || "") === target)
                return index
        }
        return 0
    }

    function fontFamilyForPath(options, path) {
        const target = path || ""
        if (!options)
            return ""
        for (let index = 0; index < options.length; ++index) {
            if ((options[index].path || "") === target)
                return options[index].family || ""
        }
        return ""
    }

    // 1.0 / 1.25 / 1.5 — one decimal where that is the whole number, two where
    // the quarter step needs it. Same rule the Widgets dialog used.
    function flowSpeedLabel(speed) {
        const oneDecimal = Math.round(speed * 10) / 10
        return Math.abs(speed - oneDecimal) < 0.001 ? speed.toFixed(1) : speed.toFixed(2)
    }

    contentItem: ColumnLayout {
        spacing: 10

        Row {
            spacing: 4
            Repeater {
                model: [root.previewSettings.videoGroupLabel,
                        root.previewSettings.gameplayGroupLabel,
                        root.previewSettings.skinGroupLabel]
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

        // ---- 视频 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 0
            spacing: 10

            LabeledSlider {
                objectName: "previewBrightnessOuterSlider"
                label: root.labels.brightnessOuter
                from: 0
                to: 100
                value: root.values.brightnessOuter
                onMoved: function(v) { root.put("brightnessOuter", Math.round(v)) }
            }
            LabeledSlider {
                objectName: "previewBrightnessInnerSlider"
                label: root.labels.brightnessInner
                from: 0
                to: 100
                value: root.values.brightnessInner
                onMoved: function(v) { root.put("brightnessInner", Math.round(v)) }
            }
            LabeledSlider {
                objectName: "previewLayoutSquareScaleSlider"
                label: root.labels.layoutSquareScale
                from: root.values.layoutSquareScaleMin
                to: root.values.layoutSquareScaleMax
                stepSize: root.values.layoutSquareScaleStep
                value: root.values.layoutSquareScale
                onMoved: function(v) { root.put("layoutSquareScale", Math.round(v)) }
            }
            LabeledCombo {
                objectName: "previewScaleModeCombo"
                label: root.labels.scaleMode
                options: root.previewSettings.scaleModeOptions
                currentValue: root.values.scaleMode
                onPicked: function(value) { root.put("scaleMode", value) }
            }

            AppSwitch {
                objectName: "previewSmoothBrightnessSwitch"
                text: root.labels.smoothBrightness
                checked: root.values.smoothBrightness
                onToggled: root.put("smoothBrightness", checked)
            }
            AppSwitch {
                objectName: "previewShowTimestampSwitch"
                text: root.labels.showTimestamp
                checked: root.values.showTimestamp
                onToggled: root.put("showTimestamp", checked)
            }
            AppSwitch {
                objectName: "previewTouchPadAuthoringSwitch"
                text: root.labels.touchPadAuthoringShortcut
                checked: root.values.touchPadAuthoringShortcut
                onToggled: root.put("touchPadAuthoringShortcut", checked)
            }
            AppSwitch {
                objectName: "previewForceLabeledJudgeLineSwitch"
                text: root.labels.forceLabeledJudgeLineWhenPaused
                checked: root.values.forceLabeledJudgeLineWhenPaused
                onToggled: root.put("forceLabeledJudgeLineWhenPaused", checked)
            }
            AppSwitch {
                objectName: "previewShowDebugInfoSwitch"
                text: root.labels.showDebugInfo
                checked: root.values.showDebugInfo
                onToggled: root.put("showDebugInfo", checked)
            }
        }

        // ---- 玩法 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 1
            spacing: 10

            // Flow speed is a quarter-step number, not a percentage, so it gets
            // a slider with its own read-out rather than the "%" default.
            LabeledSlider {
                objectName: "previewTapFlowSpeedSlider"
                label: root.labels.tapFlowSpeed
                from: root.values.flowSpeedMin
                to: root.values.flowSpeedMax
                stepSize: root.values.flowSpeedStep
                value: root.values.tapFlowSpeed
                suffix: ""
                decimals: 2
                readout: root.flowSpeedLabel(root.values.tapFlowSpeed)
                onMoved: function(v) { root.put("tapFlowSpeed", v) }
            }
            LabeledSlider {
                objectName: "previewTouchFlowSpeedSlider"
                label: root.labels.touchFlowSpeed
                from: root.values.flowSpeedMin
                to: root.values.flowSpeedMax
                stepSize: root.values.flowSpeedStep
                value: root.values.touchFlowSpeed
                suffix: ""
                decimals: 2
                readout: root.flowSpeedLabel(root.values.touchFlowSpeed)
                onMoved: function(v) { root.put("touchFlowSpeed", v) }
            }
            LabeledCombo {
                objectName: "previewSlideStackOrderCombo"
                label: root.labels.slideEarlierOnTop
                options: root.previewSettings.slideStackOrderOptions
                currentValue: root.values.slideEarlierOnTop
                onPicked: function(value) { root.put("slideEarlierOnTop", value) }
            }
            LabeledCombo {
                objectName: "previewCenterDisplayCombo"
                label: root.labels.centerDisplay
                options: root.previewSettings.centerDisplayOptions
                currentValue: root.values.centerDisplay
                onPicked: function(value) { root.put("centerDisplay", value) }
            }
            LabeledCombo {
                objectName: "previewTapJudgeTextDistanceCombo"
                label: root.labels.tapJudgeTextDistance
                options: root.previewSettings.tapJudgeTextDistanceOptions
                currentValue: root.values.tapJudgeTextDistance
                onPicked: function(value) { root.put("tapJudgeTextDistance", value) }
            }

            // 判定效果显示 is four independent overlays. The Widgets dialog hid
            // them behind a dropdown that summarised the set; there is room for
            // the four boxes here, so all four states are simply visible.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Text {
                    Layout.preferredWidth: 120
                    text: root.labels.judgeEffect
                    color: Theme.colors.text.secondary
                    wrapMode: Text.WordWrap
                }
                Repeater {
                    model: root.previewSettings.judgeEffectOptions
                    delegate: AppCheckBox {
                        required property var modelData
                        text: modelData.label
                        checked: root.values[modelData.value] === true
                        onToggled: root.put(modelData.value, checked)
                    }
                }
                Item { Layout.fillWidth: true }
            }
        }

        // ---- 皮肤 ----
        ColumnLayout {
            Layout.fillWidth: true
            visible: root.activePage === 2
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: UiText.text("皮肤")
                    color: Theme.colors.text.secondary
                    Layout.preferredWidth: 120
                }
                AppComboBox {
                    id: previewSkinCombo
                    objectName: "previewSkinCombo"
                    Layout.fillWidth: true
                    model: root.previewSettings.skinOptions
                    textRole: "label"
                    currentIndex: root.previewSettings.skinIndex
                    Accessible.name: UiText.text("皮肤")
                    onActivated: root.previewSettings.skinIndex = currentIndex
                }
                AppButton {
                    text: UiText.text("打开目录")
                    onClicked: root.previewSettings.openSkinDirectory()
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: UiText.text("判定效果")
                    color: Theme.colors.text.secondary
                    Layout.preferredWidth: 120
                }
                AppComboBox {
                    id: previewSkinJudgeEffectCombo
                    objectName: "previewSkinJudgeEffectCombo"
                    Layout.fillWidth: true
                    model: root.previewSettings.skinJudgeEffectOptions
                    currentIndex: root.previewSettings.skinJudgeEffectIndex
                    Accessible.name: UiText.text("判定效果")
                    onActivated: root.previewSettings.skinJudgeEffectIndex = currentIndex
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: UiText.text("判定线")
                    color: Theme.colors.text.secondary
                    Layout.preferredWidth: 120
                }
                AppComboBox {
                    id: previewOutlineCombo
                    objectName: "previewOutlineCombo"
                    Layout.fillWidth: true
                    model: root.previewSettings.outlineOptions
                    currentIndex: root.previewSettings.outlineIndex
                    Accessible.name: UiText.text("判定线")
                    onActivated: root.previewSettings.outlineIndex = currentIndex
                }
                AppButton {
                    text: UiText.text("打开目录")
                    onClicked: root.previewSettings.openJudgeLineDirectory()
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.topMargin: 2
                height: 1
                color: Theme.colors.border.normal
            }

            Text {
                text: UiText.text("HUD 字体")
                color: Theme.colors.text.active
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                font.bold: true
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: UiText.text("区域")
                    color: Theme.colors.text.secondary
                    Layout.preferredWidth: 120
                }
                AppComboBox {
                    id: previewHudFontAreaCombo
                    objectName: "previewHudFontAreaCombo"
                    Layout.fillWidth: true
                    model: root.previewSettings.hudFontAreaOptions
                    textRole: "label"
                    currentIndex: root.previewSettings.hudFontAreaIndex
                    Accessible.name: UiText.text("HUD 字体区域")
                    onActivated: root.previewSettings.hudFontAreaIndex = currentIndex
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Text {
                    text: UiText.text("字体")
                    color: Theme.colors.text.secondary
                    Layout.preferredWidth: 120
                }
                AppComboBox {
                    id: previewHudFontCombo
                    objectName: "previewHudFontCombo"
                    Layout.fillWidth: true
                    model: root.previewSettings.fontLibraryOptions
                    textRole: "label"
                    currentIndex: root.fontIndexForPath(model, root.previewSettings.hudFontPath)
                    Accessible.name: UiText.text("HUD 字体")
                    onActivated: root.previewSettings.hudFontPath = model[currentIndex].path
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: previewHudFontSample.implicitHeight + 20
                radius: Theme.controlRadius
                color: Theme.colors.background.editor
                border.width: Theme.controlBorderWidth
                border.color: Theme.colors.border.control
                Text {
                    id: previewHudFontSample
                    anchors.fill: parent
                    anchors.margins: 10
                    text: root.previewSettings.hudFontSample
                    color: Theme.colors.text.primary
                    font.family: root.fontFamilyForPath(root.previewSettings.fontLibraryOptions,
                                                        root.previewSettings.hudFontPath) || Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
            }

            RowLayout {
                Layout.fillWidth: true
                AppButton {
                    id: previewHudFontImportButton
                    objectName: "previewHudFontImportButton"
                    text: UiText.text("导入字体…")
                    Accessible.name: UiText.text("导入 HUD 字体")
                    onClicked: root.previewSettings.importHudFont()
                }
                AppButton {
                    id: previewHudFontResetButton
                    objectName: "previewHudFontResetButton"
                    text: UiText.text("还原")
                    Accessible.name: UiText.text("还原 HUD 字体")
                    onClicked: root.previewSettings.resetHudFont()
                }
                Item { Layout.fillWidth: true }
            }
        }
    }

    onOpened: root.previewSettings.refreshFontLibrary()
}
