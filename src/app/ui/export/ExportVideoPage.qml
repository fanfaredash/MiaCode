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

    // The batch-only inputs used to sit above the settings tab row and eat the
    // height the tab body needed. They are a settings tab of their own now, so
    // this list simply gains one entry while batch mode is active.
    readonly property var settingsTabs: {
        const tabs = []
        if (root.session && root.session.activeTab === "batch")
            tabs.push({ id: "batch", label: qsTrId("qml.batch") })
        tabs.push({ id: "output", label: qsTrId("video_export.output") })
        tabs.push({ id: "video", label: qsTrId("video_export.video") })
        tabs.push({ id: "gameplay", label: qsTrId("video_export.gameplay") })
        tabs.push({ id: "skin", label: qsTrId("video_export.skin") })
        tabs.push({ id: "intro", label: qsTrId("video_export.intro") })
        return tabs
    }

    // A settings tab the current export mode does not offer would leave the tab
    // body blank, so the two ends are kept in step: entering batch opens the
    // batch tab (its output folder and chart folders gate the run, and nothing
    // else advertises that they are required), and leaving batch falls back to
    // the output tab instead of stranding the page on a tab that just vanished.
    function normalizeSettingsTab() {
        if (!root.session)
            return
        if (root.session.activeTab === "batch")
            root.session.settingsTab = "batch"
        else if (root.session.settingsTab === "batch")
            root.session.settingsTab = "output"
    }

    function fontIndexForPath(options, path) {
        if (!options)
            return 0
        for (let index = 0; index < options.length; ++index) {
            if (options[index].path === path)
                return index
        }
        return 0
    }

    function fontFamilyForPath(options, path) {
        const index = fontIndexForPath(options, path)
        return options && options.length > index ? options[index].family : ""
    }

    color: Theme.surfaceColor(Theme.colors.background.panel)
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
                text: qsTrId("sidebar.export")
                active: root.session && root.session.activeTab === "export"
                onClicked: if (root.session) root.session.activeTab = "export"
            }
            AppTab {
                panelTab: true
                text: qsTrId("action.batch_export")
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

            Row {
                spacing: 4
                Repeater {
                    model: root.settingsTabs
                    delegate: AppTab {
                        required property var modelData
                        objectName: "exportSettingsTab_" + modelData.id
                        panelTab: true
                        text: modelData.label
                        active: root.session && root.session.settingsTab === modelData.id
                        onClicked: if (root.session) root.session.settingsTab = modelData.id
                    }
                }
            }

            Flickable {
                id: settingsFlickable
                objectName: "exportSettingsFlickable"
                Layout.fillWidth: true
                Layout.fillHeight: true
                // Nothing mode-specific sits above this any more, so every
                // tab now opens on the same viewport. The floor stays as the
                // short-window guard: squeezed down to a couple of rows the
                // pane reads as content that vanished rather than as a panel
                // with a scrollbar.
                Layout.minimumHeight: 200
                clip: true
                contentHeight: settingsBody.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: AppScrollBar {
                    id: settingsScrollBar
                    // Default AsNeeded fades out when idle; on a squeezed
                    // pane that reads as "the rest of the content is gone"
                    // rather than "scroll for more". Keep it visible for as
                    // long as there is anything to scroll to.
                    policy: settingsFlickable.contentHeight > settingsFlickable.height
                        ? ScrollBar.AlwaysOn : ScrollBar.AsNeeded
                }

                ColumnLayout {
                    id: settingsBody
                    // Leave room for the scrollbar so it rides beside the
                    // content instead of overlapping the text underneath it.
                    width: settingsFlickable.width - settingsScrollBar.width - 4
                    spacing: 12

                    // Batch (only reachable while batch export is the active mode)
                    ColumnLayout {
                        objectName: "exportBatchSettingsPage"
                        visible: root.session && root.session.activeTab === "batch"
                                 && root.session.settingsTab === "batch"
                        spacing: 10
                        Layout.fillWidth: true

                        SettingsSection {
                            title: qsTrId("dialog.batch_export.difficulty")
                            first: true

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
                        }

                        SettingsSection {
                            title: qsTrId("qml.output_folder")

                            RowLayout {
                                Layout.fillWidth: true
                                AppTextField {
                                    objectName: "batchOutputDirectoryField"
                                    Layout.fillWidth: true
                                    text: root.session ? root.session.batchOutputDirectory : ""
                                    onEditingFinished: if (root.session) root.session.batchOutputDirectory = text
                                }
                                AppButton {
                                    text: qsTrId("action.browse")
                                    onClicked: if (root.session) root.session.browseBatchOutputDirectory()
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTrId("dialog.batch_export.chart_folders")

                            // SettingsSection's title row has no slot for trailing
                            // controls, so Add/Clear sit on their own right-aligned
                            // row directly above the list instead of riding the
                            // caption itself.
                            RowLayout {
                                Layout.fillWidth: true
                                Item { Layout.fillWidth: true }
                                AppButton {
                                    text: qsTrId("qml.add")
                                    onClicked: if (root.session) root.session.addChartDirectories()
                                }
                                AppButton {
                                    text: qsTrId("dialog.batch_export.clear")
                                    onClicked: if (root.session) root.session.clearChartDirectories()
                                }
                            }

                            // This was a ListView capped at 112px because it shared
                            // a column with the settings tabs and would otherwise
                            // starve them. On its own tab there is nothing left to
                            // starve, so the rows lay out at full height and the
                            // tab's own Flickable scrolls them — which also keeps a
                            // second scrollable from nesting inside that one.
                            Rectangle {
                                id: chartDirectoryGroove
                                objectName: "batchChartDirectoryList"
                                Layout.fillWidth: true
                                color: Theme.overlayColor(Theme.colors.background.surface)
                                radius: Theme.controlRadius
                                border.width: 1
                                border.color: Theme.colors.border.normal

                                readonly property var directories: root.session ? root.session.chartDirectories : []
                                // An empty groove that collapsed to zero height would
                                // read as the section vanishing, so it holds a floor
                                // and stays visible as an empty list.
                                implicitHeight: chartDirectoryGroove.directories.length > 0
                                                 ? directoryColumn.implicitHeight
                                                 : Theme.controlMinHeight * 3

                                ColumnLayout {
                                    id: directoryColumn
                                    anchors.left: parent.left
                                    anchors.right: parent.right
                                    anchors.top: parent.top
                                    spacing: 0

                                    Repeater {
                                        model: chartDirectoryGroove.directories
                                        delegate: ColumnLayout {
                                            id: chartDirectoryDelegate
                                            required property int index
                                            required property string modelData
                                            Layout.fillWidth: true
                                            spacing: 0

                                            Rectangle {
                                                // Inset to match the row's own padding rather
                                                // than running edge-to-edge, so it reads as a
                                                // row separator and not a second groove border.
                                                visible: chartDirectoryDelegate.index > 0
                                                Layout.fillWidth: true
                                                Layout.leftMargin: Theme.rowPaddingX
                                                Layout.rightMargin: Theme.rowPaddingX
                                                height: 1
                                                color: Theme.colors.border.normal
                                            }

                                            ChromeRow {
                                                id: chartDirectoryRow
                                                Layout.fillWidth: true
                                                implicitHeight: Theme.controlMinHeight

                                                contentItem: RowLayout {
                                                    spacing: 8

                                                    Text {
                                                        id: chartDirectoryPath
                                                        Layout.fillWidth: true
                                                        text: chartDirectoryDelegate.modelData
                                                        elide: Text.ElideMiddle
                                                        color: Theme.colors.text.active
                                                        font.family: Theme.uiFont
                                                        font.pixelSize: Theme.uiFontSize
                                                        verticalAlignment: Text.AlignVCenter

                                                        HoverHandler { id: chartDirectoryPathHover }
                                                        Tooltip {
                                                            visible: chartDirectoryPathHover.hovered
                                                            text: chartDirectoryDelegate.modelData
                                                        }
                                                    }

                                                    IconButton {
                                                        compact: true
                                                        iconSource: Qt.resolvedUrl("icons/remove.svg")
                                                        tooltip: qsTrId("qml.remove")
                                                        // Left to IconButton's own resting/hover
                                                        // glyphColor: `active` would be wrong here
                                                        // because it also drives HoverChrome's
                                                        // selected background, so brightening the
                                                        // glyph on row hover would paint the
                                                        // button as if it were toggled on.
                                                        onClicked: if (root.session)
                                                            root.session.removeChartDirectory(chartDirectoryDelegate.index)
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    // Output (single-export range lives on this tab)
                    ColumnLayout {
                        visible: root.session && root.session.settingsTab === "output"
                        spacing: 12
                        Layout.fillWidth: true

                        GridLayout {
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10
                            Layout.fillWidth: true

                        Text {
                            visible: root.session && root.session.activeTab === "export"
                            text: qsTrId("video_export.output")
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
                                text: qsTrId("action.browse")
                                onClicked: if (root.session) root.session.browseOutputPath()
                            }
                        }

                        Text {
                            text: qsTrId("dialog.video_export.resolution")
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
                            text: qsTrId("dialog.video_export.fps")
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
                            text: qsTrId("dialog.video_export.audio_bitrate")
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
                            text: qsTrId("dialog.video_export.preset")
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
                            text: qsTrId("dialog.video_export.size_preset")
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

                        ColumnLayout {
                            visible: root.session && root.session.activeTab === "export"
                            spacing: 10
                            Layout.fillWidth: true

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.topMargin: 2
                            height: 1
                            color: Theme.colors.border.normal
                        }

                        Text {
                            text: qsTrId("video_export.export_range")
                            color: Theme.colors.text.active
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize
                            font.bold: true
                        }

                        ExportRangeSelector {
                            objectName: "exportRangeSelector"
                            Layout.fillWidth: true
                            exportSession: root.session
                            previewSession: root.previewSession
                        }
                        RowLayout {
                            Text {
                                text: qsTrId("dialog.video_export.range.start")
                                color: Theme.colors.text.secondary
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
                                text: qsTrId("dialog.video_export.range.end")
                                color: Theme.colors.text.secondary
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
                                  ? qsTrId("qml.total_duration_1_s").arg(root.session.contentDurationSeconds.toFixed(3))
                                  : ""
                            color: Theme.colors.text.secondary
                        }
                        }
                    }

                    // Video
                    ColumnLayout {
                        visible: root.session && root.session.settingsTab === "video"
                        spacing: 10
                        Layout.fillWidth: true

                        LabeledSlider {
                            objectName: "exportBrightnessOuterSlider"
                            label: qsTrId("qml.outer_brightness")
                            from: 0
                            to: 100
                            value: root.session ? root.session.backgroundBrightnessOuter * 100 : 50
                            onMoved: function(v) { if (root.session) root.session.backgroundBrightnessOuter = v / 100 }
                        }
                        LabeledSlider {
                            objectName: "exportBrightnessInnerSlider"
                            label: qsTrId("qml.inner_brightness")
                            from: 0
                            to: 100
                            value: root.session ? root.session.backgroundBrightnessInner * 100 : 20
                            onMoved: function(v) { if (root.session) root.session.backgroundBrightnessInner = v / 100 }
                        }
                        LabeledSlider {
                            objectName: "exportLayoutSquareScaleSlider"
                            label: qsTrId("video_export.layout_size")
                            from: 50
                            to: 100
                            stepSize: 5
                            value: root.session ? root.session.layoutSquareScale * 100 : 95
                            onMoved: function(v) { if (root.session) root.session.layoutSquareScale = v / 100 }
                        }
                        RowLayout {
                            Text {
                                text: qsTrId("qml.background_scaling")
                                color: Theme.colors.text.secondary
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
                            text: qsTrId("video_export.smooth_brightness")
                            checked: root.session ? root.session.smoothBrightness : false
                            onToggled: if (root.session) root.session.smoothBrightness = checked
                        }
                        AppSwitch {
                            text: qsTrId("video_export.show_bottom_left_timestamp")
                            checked: root.session ? root.session.showTimestamp : true
                            onToggled: if (root.session) root.session.showTimestamp = checked
                        }
                        AppSwitch {
                            text: qsTrId("dialog.video_export.option.show_object_stats")
                            checked: root.session ? root.session.showObjectStatsHud : false
                            onToggled: if (root.session) root.session.showObjectStatsHud = checked
                        }
                        AppSwitch {
                            text: qsTrId("qml.show_chart_information")
                            checked: root.session ? root.session.showChartInfoHud : false
                            onToggled: if (root.session) root.session.showChartInfoHud = checked
                        }
                        AppSwitch {
                            text: qsTrId("qml.fix_hud_text_layout")
                            checked: root.session ? root.session.fixHudTextLayout : false
                            onToggled: if (root.session) root.session.fixHudTextLayout = checked
                        }
                        AppSwitch {
                            text: qsTrId("qml.enable_clock_count")
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
                                text: qsTrId("dialog.render_settings.video.tap_flow_speed")
                                color: Theme.colors.text.secondary
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
                                text: qsTrId("dialog.render_settings.video.touch_flow_speed")
                                color: Theme.colors.text.secondary
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

                    // Global preview skin/HUD font controls are also available
                    // from PreviewSettingsDialog. Both paths use the same v2
                    // owner-live preview state so export reflects the change.
                    ColumnLayout {
                        visible: root.session && root.session.settingsTab === "skin"
                        spacing: 10
                        Layout.fillWidth: true

                        SettingsSection {
                            title: qsTrId("video_export.skin")
                            first: true

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: qsTrId("video_export.skin")
                                    color: Theme.colors.text.secondary
                                    Layout.preferredWidth: 120
                                }
                                AppComboBox {
                                    id: exportSkinCombo
                                    objectName: "exportSkinCombo"
                                    Layout.fillWidth: true
                                    model: root.session ? root.session.skinOptions : []
                                    textRole: "label"
                                    currentIndex: root.session ? root.session.skinIndex : -1
                                    Accessible.name: qsTrId("video_export.skin")
                                    onActivated: if (root.session) root.session.skinIndex = currentIndex
                                }
                                AppButton {
                                    text: qsTrId("dialog.skin_settings.open_directory")
                                    onClicked: if (root.session) root.session.openSkinDirectory()
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
                                    id: exportSkinJudgeEffectCombo
                                    objectName: "exportSkinJudgeEffectCombo"
                                    Layout.fillWidth: true
                                    model: root.session ? root.session.skinJudgeEffectOptions : []
                                    currentIndex: root.session ? root.session.skinJudgeEffectIndex : 0
                                    Accessible.name: qsTrId("dialog.skin_settings.chart_effect")
                                    onActivated: if (root.session) root.session.skinJudgeEffectIndex = currentIndex
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
                                    id: exportOutlineCombo
                                    objectName: "exportOutlineCombo"
                                    Layout.fillWidth: true
                                    model: root.session ? root.session.outlineOptions : []
                                    currentIndex: root.session ? root.session.outlineIndex : 1
                                    Accessible.name: qsTrId("dialog.render_settings.gameplay.judge_line")
                                    onActivated: if (root.session) root.session.outlineIndex = currentIndex
                                }
                                AppButton {
                                    text: qsTrId("dialog.skin_settings.open_directory")
                                    onClicked: if (root.session) root.session.openJudgeLineDirectory()
                                }
                            }
                        }

                        SettingsSection {
                            title: qsTrId("dialog.video_export.option.hud_font")

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: qsTrId("dialog.video_export.option.hud_font_area")
                                    color: Theme.colors.text.secondary
                                    Layout.preferredWidth: 120
                                }
                                AppComboBox {
                                    id: hudFontAreaCombo
                                    objectName: "hudFontAreaCombo"
                                    Layout.fillWidth: true
                                    model: root.session ? root.session.hudFontAreaOptions : []
                                    textRole: "label"
                                    currentIndex: root.session ? root.session.hudFontAreaIndex : 0
                                    Accessible.name: qsTrId("qml.hud_font_area")
                                    onActivated: if (root.session) root.session.hudFontAreaIndex = currentIndex
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
                                    id: hudFontCombo
                                    objectName: "hudFontCombo"
                                    Layout.fillWidth: true
                                    model: root.session ? root.session.fontLibraryOptions : []
                                    textRole: "label"
                                    currentIndex: root.fontIndexForPath(model,
                                                                       root.session ? root.session.hudFontPath : "")
                                    Accessible.name: qsTrId("dialog.video_export.option.hud_font")
                                    onActivated: if (root.session) root.session.hudFontPath = model[currentIndex].path
                                }
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: hudFontSample.implicitHeight + 20
                                radius: Theme.controlRadius
                                color: Theme.overlayColor(Theme.colors.background.surface)
                                Text {
                                    id: hudFontSample
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    text: root.session ? root.session.hudFontSample : ""
                                    color: Theme.colors.text.primary
                                    font.family: root.fontFamilyForPath(
                                                     root.session ? root.session.fontLibraryOptions : [],
                                                     root.session ? root.session.hudFontPath : "") || Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideRight
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                AppButton {
                                    id: hudFontImportButton
                                    objectName: "hudFontImportButton"
                                    text: qsTrId("card_font.import")
                                    Accessible.name: qsTrId("qml.import_hud_font")
                                    onClicked: if (root.session) root.session.importHudFont()
                                }
                                AppButton {
                                    id: hudFontResetButton
                                    objectName: "hudFontResetButton"
                                    text: qsTrId("action.reset")
                                    Accessible.name: qsTrId("qml.reset_hud_font")
                                    onClicked: if (root.session) root.session.resetHudFont()
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }
                    }

                    // Intro
                    ColumnLayout {
                        visible: root.session && root.session.settingsTab === "intro"
                        spacing: 10
                        Layout.fillWidth: true

                        // Kept outside every section: gating this on `introEnabled`
                        // itself would let it disable its own switch.
                        AppSwitch {
                            text: qsTrId("video_export.add_intro")
                            checked: root.session ? root.session.introEnabled : false
                            enabled: root.session
                                     ? root.session.activeTab === "batch" || root.session.fullRangeExport
                                     : false
                            onToggled: if (root.session) root.session.introEnabled = checked
                        }

                        SettingsSection {
                            title: qsTrId("qml.visuals")
                            enabled: root.introSettingsEnabled

                            RowLayout {
                                Text {
                                    text: qsTrId("dialog.preferences.background_group")
                                    color: Theme.colors.text.secondary
                                    Layout.preferredWidth: 120
                                }
                                AppComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTrId("cover.jacket"), qsTrId("qml.custom")]
                                    currentIndex: root.session ? root.session.introBackgroundModeIndex : 0
                                    onActivated: if (root.session) root.session.introBackgroundModeIndex = currentIndex
                                }
                            }
                            RowLayout {
                                visible: root.session && root.session.introBackgroundModeIndex === 1
                                AppTextField {
                                    Layout.fillWidth: true
                                    text: root.session ? root.session.introCustomBackgroundPath : ""
                                    onEditingFinished: if (root.session) root.session.introCustomBackgroundPath = text
                                }
                                AppButton {
                                    text: qsTrId("action.browse")
                                    onClicked: if (root.session) root.session.browseIntroBackground()
                                }
                            }
                            AppSwitch {
                                text: qsTrId("cover.blur_background")
                                checked: root.session ? root.session.introBlurBackground : true
                                onToggled: if (root.session) root.session.introBlurBackground = checked
                            }
                            RowLayout {
                                Text {
                                    text: qsTrId("cover.chart_type")
                                    color: Theme.colors.text.secondary
                                    Layout.preferredWidth: 120
                                }
                                AppComboBox {
                                    Layout.fillWidth: true
                                    model: [qsTrId("qml.automatic"), "DX", "SD"]
                                    currentIndex: root.session ? root.session.introModeIndex : 0
                                    onActivated: if (root.session) root.session.introModeIndex = currentIndex
                                }
                            }
                            AppSwitch {
                                text: qsTrId("cover.card_drop_shadow")
                                checked: root.session ? root.session.introCardShadow : false
                                onToggled: if (root.session) root.session.introCardShadow = checked
                            }
                            AppSwitch {
                                text: qsTrId("qml.render_level_as_text")
                                checked: root.session ? root.session.introLevelTextRender : false
                                onToggled: if (root.session) root.session.introLevelTextRender = checked
                            }
                        }

                        SettingsSection {
                            title: qsTrId("qml.difficulty_card_fonts")
                            enabled: root.introSettingsEnabled

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: qsTrId("card_font.title")
                                    color: Theme.colors.text.secondary
                                    Layout.preferredWidth: 120
                                }
                                AppComboBox {
                                    id: introDisplayFontCombo
                                    objectName: "introDisplayFontCombo"
                                    Layout.fillWidth: true
                                    model: root.session ? root.session.fontLibraryOptions : []
                                    textRole: "label"
                                    currentIndex: root.fontIndexForPath(
                                                      model, root.session ? root.session.introFontDisplayPath : "")
                                    Accessible.name: qsTrId("qml.intro_title_font")
                                    onActivated: if (root.session)
                                        root.session.introFontDisplayPath = model[currentIndex].path
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: qsTrId("card_font.body")
                                    color: Theme.colors.text.secondary
                                    Layout.preferredWidth: 120
                                }
                                AppComboBox {
                                    id: introBodyFontCombo
                                    objectName: "introBodyFontCombo"
                                    Layout.fillWidth: true
                                    model: root.session ? root.session.fontLibraryOptions : []
                                    textRole: "label"
                                    currentIndex: root.fontIndexForPath(
                                                      model, root.session ? root.session.introFontBodyPath : "")
                                    Accessible.name: qsTrId("qml.intro_body_font")
                                    onActivated: if (root.session)
                                        root.session.introFontBodyPath = model[currentIndex].path
                                }
                            }
                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: introFontPreviewColumn.implicitHeight + 20
                                radius: Theme.controlRadius
                                color: Theme.overlayColor(Theme.colors.background.surface)
                                Column {
                                    id: introFontPreviewColumn
                                    anchors.fill: parent
                                    anchors.margins: 10
                                    spacing: 3
                                    Text {
                                        id: introFontSample
                                        width: parent.width
                                        text: qsTrId("qml.title_font_preview")
                                        color: Theme.colors.text.primary
                                        font.family: root.fontFamilyForPath(
                                                         root.session ? root.session.fontLibraryOptions : [],
                                                         root.session ? root.session.introFontDisplayPath : "") || Theme.uiFont
                                        font.pixelSize: Theme.uiFontSize
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        width: parent.width
                                        text: qsTrId("qml.body_font_preview")
                                        color: Theme.colors.text.secondary
                                        font.family: root.fontFamilyForPath(
                                                         root.session ? root.session.fontLibraryOptions : [],
                                                         root.session ? root.session.introFontBodyPath : "") || Theme.uiFont
                                        font.pixelSize: Theme.secondaryFontSize
                                        elide: Text.ElideRight
                                    }
                                }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                AppButton {
                                    id: introFontImportButton
                                    objectName: "introFontImportButton"
                                    text: qsTrId("card_font.import")
                                    Accessible.name: qsTrId("qml.import_intro_difficulty_card_fonts")
                                    onClicked: if (root.session) root.session.importIntroFont()
                                }
                                AppButton {
                                    id: introFontResetButton
                                    objectName: "introFontResetButton"
                                    text: qsTrId("card_font.reset")
                                    Accessible.name: qsTrId("qml.reset_intro_difficulty_card_fonts")
                                    onClicked: if (root.session) root.session.resetIntroFonts()
                                }
                                Item { Layout.fillWidth: true }
                            }
                        }

                        SettingsSection {
                            title: qsTrId("qml.sound_effects")
                            enabled: root.introSettingsEnabled

                            RowLayout {
                                Layout.fillWidth: true
                                Text {
                                    text: root.session ? root.session.introSoundLabel : ""
                                    color: Theme.colors.text.secondary
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
                            LabeledSlider {
                                id: introSoundVolumeSlider
                                objectName: "introSoundVolumeSlider"
                                label: root.session ? root.session.introSoundVolumeLabel : ""
                                from: 0
                                to: 200
                                stepSize: 1
                                suffix: "%"
                                value: root.session ? root.session.introSoundVolume * 100 : 100
                                focusPolicy: Qt.StrongFocus
                                onMoved: function(v) { if (root.session) root.session.introSoundVolume = v / 100 }
                            }
                        }
                    }
                }
            }

            Item { Layout.fillWidth: true; Layout.preferredHeight: 1 }
            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: root.session && root.session.exportRunning ? qsTrId("video_export.cancel_export") : qsTrId("video_export.start_export")
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

    Component.onCompleted: root.normalizeSettingsTab()

    Connections {
        target: root.session

        function onActiveTabChanged() {
            root.normalizeSettingsTab()
        }

        function onRangeChanged() {
            if (!exportRangeStartField.activeFocus)
                exportRangeStartField.text = root.session.exportStartSeconds.toFixed(3)
            if (!exportRangeEndField.activeFocus)
                exportRangeEndField.text = root.session.exportEndSeconds.toFixed(3)
        }
    }
}
