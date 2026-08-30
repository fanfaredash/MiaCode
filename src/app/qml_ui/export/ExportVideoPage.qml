pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

Rectangle {
    id: root

    required property var pages
    required property var previewSession
    readonly property var session: pages && pages.exportSession ? pages.exportSession : null
    readonly property bool introSettingsEnabled: !!root.session
                                                  && root.session.introEnabled
                                                  && (root.session.activeTab === "batch"
                                                      || root.session.fullRangeExport)

    color: Theme.colors.background.surface
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: root.session ? root.session.difficulties : []
                delegate: ChromeRow {
                    id: badge
                    required property var modelData
                    implicitHeight: 28
                    implicitWidth: badgeLabel.implicitWidth + leftPadding + rightPadding
                    checkable: true
                    checked: root.session && root.session.selectedDifficultyId === modelData.id
                    selected: badge.checked
                    onClicked: if (root.session) root.session.selectDifficulty(modelData.id)
                    contentItem: Text {
                        id: badgeLabel
                        text: badge.modelData.name
                        color: badge.checked ? Theme.colors.text.active : Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                        font.bold: true
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }
        }

        Row {
            spacing: 4
            AppTab {
                panelTab: true
                text: qsTr("导出")
                active: root.session && root.session.activeTab === "export"
                onClicked: if (root.session) root.session.activeTab = "export"
            }
            AppTab {
                panelTab: true
                text: qsTr("批量导出")
                active: root.session && root.session.activeTab === "batch"
                onClicked: if (root.session) root.session.activeTab = "batch"
            }
        }

        Rectangle {
            Layout.fillWidth: true
            height: 1
            color: Theme.colors.border.normal
        }

        Text {
            Layout.fillWidth: true
            visible: !!(root.session && root.session.unavailableReason)
            text: root.session ? root.session.unavailableReason : ""
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            wrapMode: Text.WordWrap
        }

        // ---- Shared single/batch export settings ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: root.session && !(root.session && root.session.unavailableReason)
            spacing: 8

            // Batch-only inputs stay above the shared settings tabs. The
            // settings form below is the single source for both export modes.
            ColumnLayout {
                Layout.fillWidth: true
                visible: root.session && root.session.activeTab === "batch"
                spacing: 10

                Text {
                    text: qsTr("难度")
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 8
                    Repeater {
                        model: root.session ? root.session.batchDifficultyChecks : []
                        delegate: AppSwitch {
                            required property var modelData
                            text: modelData.name
                            checked: modelData.checked
                            onToggled: if (root.session) root.session.setBatchDifficultyChecked(modelData.id, checked)
                        }
                    }
                }

                Text {
                    text: qsTr("输出文件夹")
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                }
                RowLayout {
                    Layout.fillWidth: true
                    AppTextField {
                        Layout.fillWidth: true
                        text: root.session ? root.session.batchOutputDirectory : ""
                        onEditingFinished: if (root.session) root.session.batchOutputDirectory = text
                    }
                    AppButton {
                        text: qsTr("浏览...")
                        onClicked: if (root.session) root.session.browseBatchOutputDirectory()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: qsTr("谱面文件夹")
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        Layout.fillWidth: true
                    }
                    AppButton {
                        text: qsTr("添加")
                        onClicked: if (root.session) root.session.addChartDirectories()
                    }
                    AppButton {
                        text: qsTr("清空")
                        onClicked: if (root.session) root.session.clearChartDirectories()
                    }
                }
                ListView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.min(contentHeight, root.height * 0.2)
                    clip: true
                    model: root.session ? root.session.chartDirectories : []
                    ScrollBar.vertical: ScrollBar {}
                    delegate: RowLayout {
                        width: ListView.view.width
                        required property int index
                        required property string modelData
                        Text {
                            Layout.fillWidth: true
                            text: modelData
                            elide: Text.ElideMiddle
                            color: Theme.colors.text.active
                            font.family: Theme.uiFont
                        }
                        AppButton {
                            text: qsTr("移除")
                            onClicked: if (root.session) root.session.removeChartDirectory(index)
                        }
                    }
                }
            }

            Row {
                spacing: 4
                Repeater {
                    model: {
                        const tabs = [
                            { id: "output", label: qsTr("输出") },
                            { id: "video", label: qsTr("视频") },
                            { id: "gameplay", label: qsTr("游戏") },
                            { id: "skin", label: qsTr("皮肤") },
                            { id: "intro", label: qsTr("片头") }
                        ]
                        if (root.session && root.session.activeTab === "export")
                            tabs.push({ id: "range", label: qsTr("导出区间") })
                        return tabs
                    }
                    delegate: AppTab {
                        required property var modelData
                        panelTab: true
                        text: modelData.label
                        active: root.session && root.session.settingsTab === modelData.id
                        onClicked: if (root.session) root.session.settingsTab = modelData.id
                    }
                }
            }

            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentHeight: settingsBody.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar {}

                ColumnLayout {
                    id: settingsBody
                    width: parent.width
                    spacing: 12

                    // Output
                    GridLayout {
                        visible: root.session && root.session.settingsTab === "output"
                        columns: 2
                        columnSpacing: 12
                        rowSpacing: 10
                        Layout.fillWidth: true

                        Text {
                            visible: root.session && root.session.activeTab === "export"
                            text: qsTr("输出")
                            color: Theme.colors.text.secondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize
                        }
                        RowLayout {
                            visible: root.session && root.session.activeTab === "export"
                            Layout.fillWidth: true
                            AppTextField {
                                Layout.fillWidth: true
                                text: root.session ? root.session.outputPath : ""
                                onEditingFinished: if (root.session) root.session.outputPath = text
                            }
                            AppButton {
                                text: qsTr("浏览...")
                                onClicked: if (root.session) root.session.browseOutputPath()
                            }
                        }

                        Text {
                            text: qsTr("分辨率")
                            color: Theme.colors.text.secondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize
                        }
                        AppComboBox {
                            Layout.fillWidth: true
                            model: root.session ? root.session.resolutionOptions : []
                            textRole: "label"
                            currentIndex: root.session ? root.session.resolutionIndex : 0
                            onActivated: if (root.session) root.session.resolutionIndex = currentIndex
                        }

                        Text {
                            text: qsTr("帧率")
                            color: Theme.colors.text.secondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize
                        }
                        AppComboBox {
                            Layout.fillWidth: true
                            model: root.session ? root.session.fpsOptions : []
                            currentIndex: {
                                if (!root.session) return 1
                                const opts = root.session.fpsOptions
                                for (let i = 0; i < opts.length; ++i)
                                    if (opts[i] === root.session.fps) return i
                                return 1
                            }
                            displayText: root.session ? (root.session.fps + " FPS") : ""
                            onActivated: if (root.session) root.session.fps = model[currentIndex]
                        }

                        Text {
                            text: qsTr("音频码率")
                            color: Theme.colors.text.secondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize
                        }
                        AppComboBox {
                            Layout.fillWidth: true
                            model: root.session ? root.session.audioBitrateOptions : []
                            currentIndex: {
                                if (!root.session) return 2
                                const opts = root.session.audioBitrateOptions
                                for (let i = 0; i < opts.length; ++i)
                                    if (opts[i] === root.session.audioBitrateKbps) return i
                                return 2
                            }
                            displayText: root.session ? (root.session.audioBitrateKbps + " kbps") : ""
                            onActivated: if (root.session) root.session.audioBitrateKbps = model[currentIndex]
                        }

                        Text {
                            text: qsTr("导出质量")
                            color: Theme.colors.text.secondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize
                        }
                        AppComboBox {
                            Layout.fillWidth: true
                            model: root.session ? root.session.presetOptions : []
                            currentIndex: root.session ? root.session.presetIndex : 1
                            onActivated: if (root.session) root.session.presetIndex = currentIndex
                        }

                        Text {
                            text: qsTr("文件体积")
                            color: Theme.colors.text.secondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize
                        }
                        AppComboBox {
                            Layout.fillWidth: true
                            model: root.session ? root.session.sizePresetOptions : []
                            currentIndex: root.session ? root.session.sizePresetIndex : 0
                            onActivated: if (root.session) root.session.sizePresetIndex = currentIndex
                        }
                    }

                    // Video
                    ColumnLayout {
                        visible: root.session && root.session.settingsTab === "video"
                        spacing: 10
                        Layout.fillWidth: true

                        RowLayout {
                            Text {
                                text: qsTr("外圈亮度")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppSlider {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: root.session ? root.session.backgroundBrightnessOuter * 100 : 50
                                onMoved: if (root.session) root.session.backgroundBrightnessOuter = value / 100
                            }
                        }
                        RowLayout {
                            Text {
                                text: qsTr("内圈亮度")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppSlider {
                                Layout.fillWidth: true
                                from: 0
                                to: 100
                                value: root.session ? root.session.backgroundBrightnessInner * 100 : 20
                                onMoved: if (root.session) root.session.backgroundBrightnessInner = value / 100
                            }
                        }
                        RowLayout {
                            Text {
                                text: qsTr("Layout 整图大小")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppSlider {
                                Layout.fillWidth: true
                                from: 50
                                to: 100
                                stepSize: 5
                                value: root.session ? root.session.layoutSquareScale * 100 : 95
                                onMoved: if (root.session) root.session.layoutSquareScale = value / 100
                            }
                        }
                        RowLayout {
                            Text {
                                text: qsTr("背景缩放")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.session ? root.session.backgroundScaleModeOptions : []
                                currentIndex: root.session ? root.session.backgroundScaleModeIndex : 0
                                onActivated: if (root.session) root.session.backgroundScaleModeIndex = currentIndex
                            }
                        }
                        AppSwitch {
                            text: qsTr("平滑亮度")
                            checked: root.session ? root.session.smoothBrightness : false
                            onToggled: if (root.session) root.session.smoothBrightness = checked
                        }
                        AppSwitch {
                            text: qsTr("显示左下角时间戳")
                            checked: root.session ? root.session.showTimestamp : true
                            onToggled: if (root.session) root.session.showTimestamp = checked
                        }
                        AppSwitch {
                            text: qsTr("显示物量统计")
                            checked: root.session ? root.session.showObjectStatsHud : false
                            onToggled: if (root.session) root.session.showObjectStatsHud = checked
                        }
                        AppSwitch {
                            text: qsTr("显示谱面信息")
                            checked: root.session ? root.session.showChartInfoHud : false
                            onToggled: if (root.session) root.session.showChartInfoHud = checked
                        }
                        AppSwitch {
                            text: qsTr("修正 HUD 文本布局")
                            checked: root.session ? root.session.fixHudTextLayout : false
                            onToggled: if (root.session) root.session.fixHudTextLayout = checked
                        }
                        AppSwitch {
                            text: qsTr("启用 clock_count")
                            checked: root.session ? root.session.clockCountEnabled : false
                            onToggled: if (root.session) root.session.clockCountEnabled = checked
                        }
                    }

                    // Gameplay (task-local speeds)
                    ColumnLayout {
                        visible: root.session && root.session.settingsTab === "gameplay"
                        spacing: 10
                        Layout.fillWidth: true
                        RowLayout {
                            Text {
                                text: qsTr("Tap 流速")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppTextField {
                                Layout.preferredWidth: 80
                                text: root.session ? root.session.tapFlowSpeed.toFixed(2) : "7.50"
                                onEditingFinished: {
                                    if (!root.session) return
                                    var value = Number(text)
                                    if (isFinite(value)) root.session.tapFlowSpeed = value
                                    text = root.session.tapFlowSpeed.toFixed(2)
                                }
                            }
                        }
                        RowLayout {
                            Text {
                                text: qsTr("Touch 流速")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppTextField {
                                Layout.preferredWidth: 80
                                text: root.session ? root.session.touchFlowSpeed.toFixed(2) : "7.50"
                                onEditingFinished: {
                                    if (!root.session) return
                                    var value = Number(text)
                                    if (isFinite(value)) root.session.touchFlowSpeed = value
                                    text = root.session.touchFlowSpeed.toFixed(2)
                                }
                            }
                        }
                    }

                    // Skin (owner-live preview settings)
                    ColumnLayout {
                        visible: root.session && root.session.settingsTab === "skin"
                        spacing: 10
                        Layout.fillWidth: true
                        RowLayout {
                            Text {
                                text: qsTr("皮肤")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.session ? root.session.skinOptions : []
                                textRole: "label"
                                currentIndex: root.session ? root.session.skinIndex : -1
                                onActivated: if (root.session) root.session.skinIndex = currentIndex
                            }
                            AppButton {
                                text: qsTr("打开目录")
                                onClicked: if (root.session) root.session.openSkinDirectory()
                            }
                        }
                        RowLayout {
                            Text {
                                text: qsTr("判定效果")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.session ? root.session.judgeEffectOptions : []
                                currentIndex: root.session ? root.session.judgeEffectIndex : 0
                                onActivated: if (root.session) root.session.judgeEffectIndex = currentIndex
                            }
                        }
                        RowLayout {
                            Text {
                                text: qsTr("判定线")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.session ? root.session.outlineOptions : []
                                currentIndex: root.session ? root.session.outlineIndex : 1
                                onActivated: if (root.session) root.session.outlineIndex = currentIndex
                            }
                            AppButton {
                                text: qsTr("打开目录")
                                onClicked: if (root.session) root.session.openJudgeLineDirectory()
                            }
                        }
                    }

                    // Intro
                    ColumnLayout {
                        visible: root.session && root.session.settingsTab === "intro"
                        spacing: 10
                        Layout.fillWidth: true
                        AppSwitch {
                            text: qsTr("添加片头")
                            checked: root.session ? root.session.introEnabled : false
                            enabled: root.session
                                     ? root.session.activeTab === "batch" || root.session.fullRangeExport
                                     : false
                            onToggled: if (root.session) root.session.introEnabled = checked
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            enabled: root.introSettingsEnabled

                            Text {
                                text: root.session ? root.session.introSoundLabel : ""
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppComboBox {
                                id: introSoundCombo
                                objectName: "introSoundCombo"
                                Layout.fillWidth: true
                                model: root.session ? root.session.introSoundOptions : []
                                textRole: "label"
                                currentIndex: root.session ? root.session.introSoundIndex : 0
                                focusPolicy: Qt.StrongFocus
                                Accessible.name: root.session ? root.session.introSoundLabel : ""
                                onActivated: if (root.session) root.session.introSoundIndex = currentIndex
                            }
                            AppButton {
                                id: introSoundImportButton
                                objectName: "introSoundImportButton"
                                text: root.session ? root.session.introSoundImportLabel : ""
                                focusPolicy: Qt.StrongFocus
                                Accessible.name: root.session
                                                 ? root.session.introSoundImportLabel + " "
                                                   + root.session.introSoundLabel
                                                 : ""
                                onClicked: if (root.session) root.session.importIntroSound()
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            enabled: root.introSettingsEnabled

                            Text {
                                text: root.session ? root.session.introSoundVolumeLabel : ""
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppSlider {
                                id: introSoundVolumeSlider
                                objectName: "introSoundVolumeSlider"
                                Layout.fillWidth: true
                                from: 0
                                to: 200
                                stepSize: 1
                                value: root.session ? root.session.introSoundVolume * 100 : 100
                                focusPolicy: Qt.StrongFocus
                                Accessible.name: root.session ? root.session.introSoundVolumeLabel : ""
                                Accessible.description: Math.round(value) + "%"
                                onMoved: if (root.session) root.session.introSoundVolume = value / 100
                            }
                            Text {
                                Layout.preferredWidth: 52
                                text: Math.round(introSoundVolumeSlider.value) + "%"
                                color: Theme.colors.text.active
                                font.family: Theme.uiFont
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                        RowLayout {
                            enabled: root.introSettingsEnabled
                            Text {
                                text: qsTr("背景")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: [qsTr("曲绘"), qsTr("自定义")]
                                currentIndex: root.session ? root.session.introBackgroundModeIndex : 0
                                onActivated: if (root.session) root.session.introBackgroundModeIndex = currentIndex
                            }
                        }
                        RowLayout {
                            visible: root.session && root.session.introBackgroundModeIndex === 1
                            enabled: root.introSettingsEnabled
                            AppTextField {
                                Layout.fillWidth: true
                                text: root.session ? root.session.introCustomBackgroundPath : ""
                                onEditingFinished: if (root.session) root.session.introCustomBackgroundPath = text
                            }
                            AppButton {
                                text: qsTr("浏览...")
                                onClicked: if (root.session) root.session.browseIntroBackground()
                            }
                        }
                        AppSwitch {
                            enabled: root.introSettingsEnabled
                            text: qsTr("背景虚化")
                            checked: root.session ? root.session.introBlurBackground : true
                            onToggled: if (root.session) root.session.introBlurBackground = checked
                        }
                        RowLayout {
                            enabled: root.introSettingsEnabled
                            Text {
                                text: qsTr("谱面类型")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 120
                            }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: [qsTr("自动"), "DX", "SD"]
                                currentIndex: root.session ? root.session.introModeIndex : 0
                                onActivated: if (root.session) root.session.introModeIndex = currentIndex
                            }
                        }
                        AppSwitch {
                            enabled: root.introSettingsEnabled
                            text: qsTr("难度卡阴影")
                            checked: root.session ? root.session.introCardShadow : false
                            onToggled: if (root.session) root.session.introCardShadow = checked
                        }
                        AppSwitch {
                            enabled: root.introSettingsEnabled
                            text: qsTr("等级文本渲染")
                            checked: root.session ? root.session.introLevelTextRender : false
                            onToggled: if (root.session) root.session.introLevelTextRender = checked
                        }
                    }

                    // Range
                    ColumnLayout {
                        visible: root.session && root.session.activeTab === "export"
                                 && root.session.settingsTab === "range"
                        spacing: 10
                        Layout.fillWidth: true
                        ExportRangeSelector {
                            objectName: "exportRangeSelector"
                            Layout.fillWidth: true
                            exportSession: root.session
                            previewSession: root.previewSession
                        }
                        RowLayout {
                            Text {
                                text: qsTr("开始")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 80
                            }
                            AppTextField {
                                id: exportRangeStartField

                                objectName: "exportRangeStartField"
                                Layout.preferredWidth: 100
                                text: root.session ? root.session.exportStartSeconds.toFixed(3) : "0"
                                onEditingFinished: if (root.session) text = root.session.setExportStartText(text)
                            }
                        }
                        RowLayout {
                            Text {
                                text: qsTr("结束")
                                color: Theme.colors.text.secondary
                                font.family: Theme.uiFont
                                Layout.preferredWidth: 80
                            }
                            AppTextField {
                                id: exportRangeEndField

                                objectName: "exportRangeEndField"
                                Layout.preferredWidth: 100
                                text: root.session ? root.session.exportEndSeconds.toFixed(3) : "0"
                                onEditingFinished: if (root.session) text = root.session.setExportEndText(text)
                            }
                        }
                        Text {
                            text: root.session
                                  ? qsTr("总时长 %1 s").arg(root.session.contentDurationSeconds.toFixed(3))
                                  : ""
                            color: Theme.colors.text.secondary
                            font.family: Theme.uiFont
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 1 }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: root.session && root.session.exportRunning ? qsTr("取消导出") : qsTr("开始导出")
                    emphasized: !(root.session && root.session.exportRunning)
                    onClicked: {
                        if (!root.session) return
                        if (root.session.exportRunning)
                            root.session.cancelExport()
                        else
                            root.session.startExport()
                    }
                }
            }
        }

    }

    Connections {
        target: root.session

        function onRangeChanged() {
            if (!exportRangeStartField.activeFocus)
                exportRangeStartField.text = root.session.exportStartSeconds.toFixed(3)
            if (!exportRangeEndField.activeFocus)
                exportRangeEndField.text = root.session.exportEndSeconds.toFixed(3)
        }
    }
}
