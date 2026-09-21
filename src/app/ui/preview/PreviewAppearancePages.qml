import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// Shared 视频 / 玩法 / 皮肤 form. Preview Settings and the export page both
// host this and write the same PreviewSettingsModel.
ColumnLayout {
    id: root

    required property var previewSettings
    property int pageIndex: 0

    readonly property var values: root.previewSettings ? root.previewSettings.values : ({})
    readonly property var labels: root.previewSettings ? root.previewSettings.labels : ({})

    spacing: 10

    function put(key, value) {
        if (root.previewSettings)
            root.previewSettings.setValue(key, value)
    }

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

    function flowSpeedLabel(speed) {
        const oneDecimal = Math.round(speed * 10) / 10
        return Math.abs(speed - oneDecimal) < 0.001 ? speed.toFixed(1) : speed.toFixed(2)
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.pageIndex === 0
        spacing: 10

        LabeledSlider {
            objectName: "previewBrightnessOuterSlider"
            label: root.labels.brightnessOuter || ""
            from: 0
            to: 100
            value: root.values.brightnessOuter || 0
            onMoved: function(v) { root.put("brightnessOuter", Math.round(v)) }
        }
        LabeledSlider {
            objectName: "previewBrightnessInnerSlider"
            label: root.labels.brightnessInner || ""
            from: 0
            to: 100
            value: root.values.brightnessInner || 0
            onMoved: function(v) { root.put("brightnessInner", Math.round(v)) }
        }
        LabeledSlider {
            objectName: "previewLayoutSquareScaleSlider"
            label: root.labels.layoutSquareScale || ""
            from: root.values.layoutSquareScaleMin || 50
            to: root.values.layoutSquareScaleMax || 100
            stepSize: root.values.layoutSquareScaleStep || 5
            value: root.values.layoutSquareScale || 95
            onMoved: function(v) { root.put("layoutSquareScale", Math.round(v)) }
        }
        LabeledCombo {
            objectName: "previewScaleModeCombo"
            label: root.labels.scaleMode || ""
            options: root.previewSettings ? root.previewSettings.scaleModeOptions : []
            currentValue: root.values.scaleMode
            onPicked: function(value) { root.put("scaleMode", value) }
        }

        AppSwitch {
            objectName: "previewSmoothBrightnessSwitch"
            text: root.labels.smoothBrightness || ""
            checked: root.values.smoothBrightness === true
            onToggled: root.put("smoothBrightness", checked)
        }
        AppSwitch {
            objectName: "previewShowTimestampSwitch"
            text: root.labels.showTimestamp || ""
            checked: root.values.showTimestamp === true
            onToggled: root.put("showTimestamp", checked)
        }
        AppSwitch {
            objectName: "previewTouchPadAuthoringSwitch"
            text: root.labels.touchPadAuthoringShortcut || ""
            checked: root.values.touchPadAuthoringShortcut === true
            onToggled: root.put("touchPadAuthoringShortcut", checked)
        }
        AppSwitch {
            objectName: "previewForceLabeledJudgeLineSwitch"
            text: root.labels.forceLabeledJudgeLineWhenPaused || ""
            checked: root.values.forceLabeledJudgeLineWhenPaused === true
            onToggled: root.put("forceLabeledJudgeLineWhenPaused", checked)
        }
        AppSwitch {
            objectName: "previewShowDebugInfoSwitch"
            text: root.labels.showDebugInfo || ""
            checked: root.values.showDebugInfo === true
            onToggled: root.put("showDebugInfo", checked)
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.pageIndex === 1
        spacing: 10

        LabeledSlider {
            objectName: "previewTapFlowSpeedSlider"
            label: root.labels.tapFlowSpeed || ""
            from: root.values.flowSpeedMin || 0
            to: root.values.flowSpeedMax || 12
            stepSize: root.values.flowSpeedStep || 0.25
            value: root.values.tapFlowSpeed || 0
            suffix: ""
            decimals: 2
            readout: root.flowSpeedLabel(root.values.tapFlowSpeed || 0)
            onMoved: function(v) { root.put("tapFlowSpeed", v) }
        }
        LabeledSlider {
            objectName: "previewTouchFlowSpeedSlider"
            label: root.labels.touchFlowSpeed || ""
            from: root.values.flowSpeedMin || 0
            to: root.values.flowSpeedMax || 12
            stepSize: root.values.flowSpeedStep || 0.25
            value: root.values.touchFlowSpeed || 0
            suffix: ""
            decimals: 2
            readout: root.flowSpeedLabel(root.values.touchFlowSpeed || 0)
            onMoved: function(v) { root.put("touchFlowSpeed", v) }
        }
        LabeledCombo {
            objectName: "previewSlideStackOrderCombo"
            label: root.labels.slideEarlierOnTop || ""
            options: root.previewSettings ? root.previewSettings.slideStackOrderOptions : []
            currentValue: root.values.slideEarlierOnTop
            onPicked: function(value) { root.put("slideEarlierOnTop", value) }
        }
        LabeledCombo {
            objectName: "previewCenterDisplayCombo"
            label: root.labels.centerDisplay || ""
            options: root.previewSettings ? root.previewSettings.centerDisplayOptions : []
            currentValue: root.values.centerDisplay
            onPicked: function(value) { root.put("centerDisplay", value) }
        }
        LabeledCombo {
            objectName: "previewTapJudgeTextDistanceCombo"
            label: root.labels.tapJudgeTextDistance || ""
            options: root.previewSettings ? root.previewSettings.tapJudgeTextDistanceOptions : []
            currentValue: root.values.tapJudgeTextDistance
            onPicked: function(value) { root.put("tapJudgeTextDistance", value) }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Text {
                Layout.preferredWidth: 120
                text: root.labels.judgeEffect || ""
                color: Theme.colors.text.secondary
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.previewSettings ? root.previewSettings.judgeEffectOptions : []
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

    ColumnLayout {
        Layout.fillWidth: true
        visible: root.pageIndex === 2
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTrId("dialog.render_settings.video.skin")
                color: Theme.colors.text.secondary
                Layout.preferredWidth: 120
            }
            AppComboBox {
                id: previewSkinCombo
                objectName: "previewSkinCombo"
                Layout.fillWidth: true
                model: root.previewSettings ? root.previewSettings.skinOptions : []
                textRole: "label"
                currentIndex: root.previewSettings ? root.previewSettings.skinIndex : -1
                Accessible.name: qsTrId("dialog.render_settings.video.skin")
                onActivated: if (root.previewSettings) root.previewSettings.skinIndex = currentIndex
            }
            AppButton {
                text: qsTrId("dialog.skin_settings.open_directory")
                onClicked: if (root.previewSettings) root.previewSettings.openSkinDirectory()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTrId("dialog.skin_settings.chart_effect")
                color: Theme.colors.text.secondary
                Layout.preferredWidth: 120
            }
            AppComboBox {
                id: previewSkinJudgeEffectCombo
                objectName: "previewSkinJudgeEffectCombo"
                Layout.fillWidth: true
                model: root.previewSettings ? root.previewSettings.skinJudgeEffectOptions : []
                currentIndex: root.previewSettings ? root.previewSettings.skinJudgeEffectIndex : 0
                Accessible.name: qsTrId("dialog.skin_settings.chart_effect")
                onActivated: if (root.previewSettings) root.previewSettings.skinJudgeEffectIndex = currentIndex
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTrId("dialog.render_settings.gameplay.judge_line")
                color: Theme.colors.text.secondary
                Layout.preferredWidth: 120
            }
            AppComboBox {
                id: previewOutlineCombo
                objectName: "previewOutlineCombo"
                Layout.fillWidth: true
                model: root.previewSettings ? root.previewSettings.outlineOptions : []
                textRole: "label"
                currentIndex: root.previewSettings ? root.previewSettings.outlineIndex : 1
                Accessible.name: qsTrId("dialog.render_settings.gameplay.judge_line")
                onActivated: if (root.previewSettings) root.previewSettings.outlineIndex = currentIndex
            }
            AppButton {
                text: qsTrId("dialog.skin_settings.open_directory")
                onClicked: if (root.previewSettings) root.previewSettings.openJudgeLineDirectory()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.topMargin: 2
            height: 1
            color: Theme.colors.border.normal
        }

        Text {
            text: qsTrId("dialog.video_export.option.hud_font")
            color: Theme.colors.text.section
            font.family: Theme.uiFont
            font.pixelSize: Theme.sectionTitleFontSize
            font.bold: true
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTrId("dialog.video_export.option.hud_font_area")
                color: Theme.colors.text.secondary
                Layout.preferredWidth: 120
            }
            AppComboBox {
                id: previewHudFontAreaCombo
                objectName: "previewHudFontAreaCombo"
                Layout.fillWidth: true
                model: root.previewSettings ? root.previewSettings.hudFontAreaOptions : []
                textRole: "label"
                currentIndex: root.previewSettings ? root.previewSettings.hudFontAreaIndex : 0
                Accessible.name: qsTrId("qml.hud_font_area")
                onActivated: if (root.previewSettings) root.previewSettings.hudFontAreaIndex = currentIndex
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: qsTrId("cover.font")
                color: Theme.colors.text.secondary
                Layout.preferredWidth: 120
            }
            AppComboBox {
                id: previewHudFontCombo
                objectName: "previewHudFontCombo"
                Layout.fillWidth: true
                model: root.previewSettings ? root.previewSettings.fontLibraryOptions : []
                textRole: "label"
                currentIndex: root.fontIndexForPath(model, root.previewSettings ? root.previewSettings.hudFontPath : "")
                Accessible.name: qsTrId("dialog.video_export.option.hud_font")
                onActivated: if (root.previewSettings) root.previewSettings.hudFontPath = model[currentIndex].path
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: previewHudFontSample.implicitHeight + 20
            radius: Theme.controlRadius
            color: Theme.overlayColor(Theme.colors.background.surface)
            Text {
                id: previewHudFontSample
                anchors.fill: parent
                anchors.margins: 10
                text: root.previewSettings ? root.previewSettings.hudFontSample : ""
                color: Theme.colors.text.primary
                font.family: root.fontFamilyForPath(
                                 root.previewSettings ? root.previewSettings.fontLibraryOptions : [],
                                 root.previewSettings ? root.previewSettings.hudFontPath : "") || Theme.uiFont
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
                text: qsTrId("card_font.import")
                Accessible.name: qsTrId("qml.import_hud_font")
                onClicked: if (root.previewSettings) root.previewSettings.importHudFont()
            }
            AppButton {
                id: previewHudFontResetButton
                objectName: "previewHudFontResetButton"
                text: qsTrId("action.reset")
                Accessible.name: qsTrId("qml.reset_hud_font")
                onClicked: if (root.previewSettings) root.previewSettings.resetHudFont()
            }
            Item { Layout.fillWidth: true }
        }
    }
}
