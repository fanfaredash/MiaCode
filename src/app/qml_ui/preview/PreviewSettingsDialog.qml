import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 预览设置. Like 音频设置 and 偏好设置, every control writes straight through and
// persists on the spot — there is no OK/Apply.
//
// Two tabs, 视频 and 玩法, matching the Widgets dialog. Its third tab (性能) held
// only 预览刷新率, and that control already lives in 偏好设置 → 性能 under v2;
// repeating it here would give one setting two homes.
Dialog {
    id: root

    required property var previewSettings

    readonly property var values: root.previewSettings.values
    readonly property var labels: root.previewSettings.labels

    property int activePage: 0

    title: qsTr("预览设置")
    modal: true
    anchors.centerIn: Overlay.overlay
    width: Math.min(640, Overlay.overlay ? Overlay.overlay.width - 48 : 640)
    footer: DialogFooter {
        cancelText: qsTr("关闭")
        onRejected: root.reject()
    }
    closePolicy: Popup.CloseOnEscape

    function put(key, value) { root.previewSettings.setValue(key, value) }

    contentItem: ColumnLayout {
        spacing: 10

        Row {
            spacing: 4
            Repeater {
                model: [root.previewSettings.videoGroupLabel,
                        root.previewSettings.gameplayGroupLabel]
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
                readout: root.values.tapFlowSpeed.toFixed(2)
                onMoved: function(v) { root.put("tapFlowSpeed", v) }
            }
            LabeledSlider {
                objectName: "previewTouchFlowSpeedSlider"
                label: root.labels.touchFlowSpeed
                from: root.values.flowSpeedMin
                to: root.values.flowSpeedMax
                stepSize: root.values.flowSpeedStep
                value: root.values.touchFlowSpeed
                readout: root.values.touchFlowSpeed.toFixed(2)
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
                    font.family: Theme.uiFont
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
    }
}
