pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// Full v2 cover compositor. The only scene it embeds is CoverComposer.qml; all
// controls, file requests, persistence and output stay in the QML surface.
Rectangle {
    id: root

    required property var pages
    required property var coverSession
    readonly property var session: root.coverSession

    function fontIndex(path) {
        if (!session || !session.fontLibraryOptions)
            return 0
        for (let index = 0; index < session.fontLibraryOptions.length; ++index) {
            if (session.fontLibraryOptions[index].path === path)
                return index
        }
        return 0
    }

    color: Theme.colors.background.surface
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            AppButton {
                text: UiText.text("cover.close")
                onClicked: root.pages.leaveOverlayPage()
            }

            Text {
                Layout.fillWidth: true
                text: UiText.text("cover.export_cover")
                color: Theme.colors.text.active
                font.family: Theme.uiFont
                font.pixelSize: Theme.headerFontSize
                font.bold: true
                elide: Text.ElideRight
            }

            AppButton {
                text: UiText.text("cover.import_layout")
                enabled: !!root.session && !root.session.busy
                onClicked: root.session.importLayout()
            }
            AppButton {
                text: UiText.text("cover.save_layout")
                enabled: !!root.session && !root.session.busy
                onClicked: root.session.saveLayout()
            }
            AppButton {
                text: UiText.text("cover.export")
                emphasized: true
                enabled: !!root.session && !root.session.busy
                onClicked: root.session.exportCover()
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: root.session ? root.session.difficulties : []
                delegate: ChromeRow {
                    required property var modelData
                    implicitHeight: 28
                    implicitWidth: label.implicitWidth + leftPadding + rightPadding
                    checkable: true
                    checked: root.session && root.session.selectedDifficultyId === modelData.id
                    selected: checked
                    onClicked: if (root.session) root.session.selectDifficulty(modelData.id)
                    contentItem: Text {
                        id: label
                        text: modelData.name
                        color: parent.checked ? Theme.colors.text.active : Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.secondaryFontSize
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }
        }

        SplitView {
            id: workspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal

            Rectangle {
                id: layerPane
                SplitView.minimumWidth: 160
                SplitView.preferredWidth: 220
                SplitView.maximumWidth: 340
                color: Theme.colors.background.elevated
                radius: Theme.controlRadius
                border.width: Theme.controlBorderWidth
                border.color: Theme.colors.border.normal

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 8

                    Text {
                        Layout.fillWidth: true
                        text: UiText.text("cover.layers")
                        color: Theme.colors.text.active
                        font.family: Theme.uiFont
                        font.bold: true
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            Layout.fillWidth: true
                            text: UiText.text("cover.add_chart_frame")
                            enabled: root.session && root.session.chartFrameAvailable && !root.session.busy
                            onClicked: root.session.addChartFrameLayer()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            Layout.fillWidth: true
                            text: UiText.text("cover.add_image")
                            enabled: root.session && !root.session.busy
                            onClicked: root.session.addImageLayer()
                        }
                        AppButton {
                            Layout.fillWidth: true
                            text: UiText.text("cover.add_text")
                            enabled: root.session && !root.session.busy
                            onClicked: root.session.addTextLayer()
                        }
                    }

                    ListView {
                        id: layers
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: 3
                        model: root.session ? root.session.layoutModel.layers : []
                        ScrollBar.vertical: ScrollBar {}
                        delegate: ChromeRow {
                            required property var modelData
                            width: ListView.view.width
                            selected: root.session && root.session.activeLayerKey === modelData.key
                            enabled: modelData.visible
                            text: modelData.label + (modelData.locked ? " · 🔒" : "")
                            onClicked: if (root.session) root.session.selectLayerKey(modelData.key)
                        }
                    }

                    AppButton {
                        Layout.fillWidth: true
                        text: UiText.text("cover.delete_the_selected_layer_delete")
                        enabled: root.session && root.session.activeLayerKey !== "card" && !root.session.busy
                        onClicked: root.session.removeActiveLayer()
                    }
                }
            }

            Rectangle {
                id: canvasPane
                SplitView.fillWidth: true
                SplitView.minimumWidth: 280
                color: Theme.colors.background.editor
                radius: Theme.controlRadius
                border.width: Theme.controlBorderWidth
                border.color: Theme.colors.border.normal

                Item {
                    id: canvasFrame
                    anchors.fill: parent
                    anchors.margins: 12

                    Loader {
                        id: composer
                        anchors.centerIn: parent
                        width: Math.max(1, Math.min(canvasFrame.width,
                                                    canvasFrame.height * ((root.session && root.session.outputWidth)
                                                                          ? root.session.outputWidth / root.session.outputHeight : 1)))
                        height: Math.max(1, Math.min(canvasFrame.height,
                                                     canvasFrame.width / ((root.session && root.session.outputWidth)
                                                                         ? root.session.outputWidth / root.session.outputHeight : 1)))
                        source: "qrc:/intro/qml/CoverComposer.qml"
                    }

                    Binding { target: composer.item; property: "coverLayout"; value: root.session ? root.session.layoutModel : null; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "coverTemplate"; value: root.session ? root.session.templateMap : ({}); when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "trackOverrides"; value: root.session ? root.session.trackOverrides : ({}); when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "jacketImage"; value: root.session ? root.session.jacketImage : ""; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "backgroundImage"; value: root.session ? root.session.backgroundImage : ""; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "backgroundMode"; value: root.session ? root.session.backgroundMode : 0; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "blurEnabled"; value: root.session ? root.session.blurBackground : true; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "coverBgBrightness"; value: root.session ? root.session.backgroundBrightness : 0.45; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "cardShadowEnabled"; value: root.session ? root.session.cardShadow : false; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "chartFrameDiskDiameter"; value: root.session ? root.session.chartFrameDiskDiameter : 0; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "selectedKey"; value: root.session ? root.session.activeLayerKey : ""; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "selectionBinder"; value: root.session; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "editable"; value: true; when: composer.status === Loader.Ready }

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: root.session && root.session.busy
                        visible: running
                        z: 2
                    }
                }
            }

            Rectangle {
                id: inspectorPane
                SplitView.minimumWidth: 240
                SplitView.preferredWidth: 310
                SplitView.maximumWidth: 430
                color: Theme.colors.background.elevated
                radius: Theme.controlRadius
                border.width: Theme.controlBorderWidth
                border.color: Theme.colors.border.normal

                Flickable {
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    contentWidth: width
                    contentHeight: inspector.implicitHeight
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}

                    ColumnLayout {
                        id: inspector
                        width: parent.width
                        spacing: 10

                        Text {
                            Layout.fillWidth: true
                            text: UiText.text("cover.canvas")
                            color: Theme.colors.text.active
                            font.family: Theme.uiFont
                            font.bold: true
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Text { text: UiText.text("cover.size"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: root.session ? root.session.resolutionOptions : []
                                textRole: "label"
                                currentIndex: root.session ? root.session.resolutionIndex : 0
                                onActivated: if (root.session) root.session.resolutionIndex = currentIndex
                            }
                            Text { text: UiText.text("输出文件夹"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                            RowLayout {
                                Layout.fillWidth: true
                                AppTextField { Layout.fillWidth: true; text: root.session ? root.session.outputDirectory : ""; onEditingFinished: if (root.session) root.session.outputDirectory = text }
                                AppButton { text: UiText.text("cover.browse"); onClicked: if (root.session) root.session.browseOutputDirectory() }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colors.border.normal }

                        Text { Layout.fillWidth: true; text: UiText.text("cover.background"); color: Theme.colors.text.active; font.family: Theme.uiFont; font.bold: true }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Text { text: UiText.text("cover.background"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                            AppComboBox {
                                Layout.fillWidth: true
                                model: [UiText.text("cover.jacket"), UiText.text("cover.custom_image"), UiText.text("cover.transparent")]
                                currentIndex: root.session ? root.session.backgroundMode : 0
                                onActivated: if (root.session) root.session.backgroundMode = currentIndex
                            }
                            Item { Layout.columnSpan: 2; Layout.fillWidth: true; implicitHeight: backgroundControls.implicitHeight
                                ColumnLayout { id: backgroundControls; width: parent.width
                                    AppButton { text: UiText.text("cover.choose_background_image"); enabled: root.session && root.session.backgroundMode === 1; onClicked: root.session.browseBackgroundImage() }
                                    AppSwitch { text: UiText.text("cover.blur_background"); checked: root.session ? root.session.blurBackground : false; enabled: root.session && root.session.backgroundMode !== 2; onToggled: if (root.session) root.session.blurBackground = checked }
                                    RowLayout { Layout.fillWidth: true
                                        Text { text: UiText.text("cover.backdrop_brightness"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                                        AppSlider { Layout.fillWidth: true; from: 0; to: 1; value: root.session ? root.session.backgroundBrightness : 0.45; enabled: root.session && root.session.backgroundMode !== 2; onMoved: if (root.session) root.session.backgroundBrightness = value }
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colors.border.normal }

                        Text { Layout.fillWidth: true; text: UiText.text("cover.difficulty_card_options"); color: Theme.colors.text.active; font.family: Theme.uiFont; font.bold: true }
                        GridLayout {
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 8
                            rowSpacing: 8
                            Text { text: UiText.text("cover.chart_type"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                            AppComboBox { Layout.fillWidth: true; model: ["auto", "DX", "Standard"]; currentIndex: root.session && root.session.cardMode === "DX" ? 1 : root.session && root.session.cardMode === "Standard" ? 2 : 0; onActivated: if (root.session) root.session.cardMode = model[currentIndex] }
                            Item { Layout.columnSpan: 2; Layout.fillWidth: true; implicitHeight: cardControls.implicitHeight
                                ColumnLayout { id: cardControls; width: parent.width
                                    AppSwitch { text: UiText.text("cover.card_drop_shadow"); checked: root.session ? root.session.cardShadow : false; onToggled: if (root.session) root.session.cardShadow = checked }
                                    AppSwitch { text: UiText.text("cover.render_level_as_text"); checked: root.session ? root.session.levelTextRender : false; onToggled: if (root.session) root.session.levelTextRender = checked }
                                    Text { text: UiText.text("card_font.title"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                                    RowLayout { Layout.fillWidth: true
                                        AppComboBox { Layout.fillWidth: true; model: root.session ? root.session.fontLibraryOptions : []; textRole: "label"; currentIndex: root.fontIndex(root.session ? root.session.cardFontDisplayPath : ""); onActivated: if (root.session) root.session.cardFontDisplayPath = model[currentIndex].path }
                                        AppButton { text: UiText.text("card_font.import"); onClicked: if (root.session) root.session.importCardDisplayFont() }
                                    }
                                    Text { text: UiText.text("card_font.body"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                                    RowLayout { Layout.fillWidth: true
                                        AppComboBox { Layout.fillWidth: true; model: root.session ? root.session.fontLibraryOptions : []; textRole: "label"; currentIndex: root.fontIndex(root.session ? root.session.cardFontBodyPath : ""); onActivated: if (root.session) root.session.cardFontBodyPath = model[currentIndex].path }
                                        AppButton { text: UiText.text("card_font.import"); onClicked: if (root.session) root.session.importCardBodyFont() }
                                    }
                                }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colors.border.normal }

                        Text { Layout.fillWidth: true; text: UiText.text("cover.layer") + (root.session && root.session.activeLayer ? " · " + root.session.activeLayer.label : ""); color: Theme.colors.text.active; font.family: Theme.uiFont; font.bold: true }
                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: root.session && root.session.activeLayer
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                AppSwitch { Layout.fillWidth: true; text: UiText.text("cover.visible"); checked: root.session && root.session.activeLayer ? root.session.activeLayer.visible : false; onToggled: if (root.session) root.session.setActiveLayerVisible(checked) }
                                AppSwitch { Layout.fillWidth: true; text: UiText.text("cover.lock"); checked: root.session && root.session.activeLayer ? root.session.activeLayer.locked : false; onToggled: if (root.session) root.session.setActiveLayerLocked(checked) }
                            }
                            RowLayout { Layout.fillWidth: true
                                Text { text: UiText.text("cover.opacity"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                                AppSlider { Layout.fillWidth: true; from: 0; to: 1; value: root.session && root.session.activeLayer ? root.session.activeLayer.opacity : 1; onMoved: if (root.session) root.session.setActiveLayerOpacity(value) }
                            }
                            RowLayout { Layout.fillWidth: true
                                Text { text: UiText.text("cover.layer_size"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                                AppSlider { Layout.fillWidth: true; from: 0.05; to: 1.5; value: root.session && root.session.activeLayer ? root.session.activeLayer.sizeFraction : 0.85; onMoved: if (root.session) root.session.setActiveLayerSizeFraction(value) }
                            }
                            RowLayout { Layout.fillWidth: true
                                AppButton { Layout.fillWidth: true; text: UiText.text("cover.send_to_back"); onClicked: root.session.sendActiveLayerToBack() }
                                AppButton { Layout.fillWidth: true; text: UiText.text("cover.bring_to_front"); onClicked: root.session.bringActiveLayerToFront() }
                            }
                            ColumnLayout { Layout.fillWidth: true; visible: root.session && root.session.activeLayer && root.session.activeLayer.kind === "image"
                                AppButton { text: UiText.text("cover.choose_image"); onClicked: root.session.browseActiveLayerImage() }
                            }
                            ColumnLayout { Layout.fillWidth: true; visible: root.session && root.session.activeLayer && root.session.activeLayer.kind === "text"
                                Text { text: UiText.text("cover.text_content"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                                AppTextField { Layout.fillWidth: true; text: root.session && root.session.activeLayer ? root.session.activeLayer.text : ""; onEditingFinished: if (root.session) root.session.setActiveLayerText(text) }
                                Text { text: UiText.text("cover.text_color"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                                AppTextField { Layout.fillWidth: true; text: root.session && root.session.activeLayer ? root.session.activeLayer.textColor : "#FFFFFF"; onEditingFinished: if (root.session) root.session.setActiveLayerTextColor(text) }
                                AppSwitch { text: UiText.text("cover.bold"); checked: root.session && root.session.activeLayer ? root.session.activeLayer.textBold : false; onToggled: if (root.session) root.session.setActiveLayerTextBold(checked) }
                                AppButton { text: UiText.text("card_font.import"); onClicked: root.session.importActiveLayerFont() }
                            }
                            ColumnLayout { Layout.fillWidth: true; visible: root.session && root.session.activeLayer && root.session.activeLayer.kind === "chartFrame"
                                Text { text: UiText.text("cover.frame_time"); color: Theme.colors.text.secondary; font.family: Theme.uiFont }
                                AppSlider { Layout.fillWidth: true; from: 0; to: root.session ? root.session.chartFrameDuration : 0; value: root.session && root.session.activeLayer ? root.session.activeLayer.frameSeconds : 0; onMoved: if (root.session) root.session.setActiveLayerFrameSeconds(value) }
                                AppComboBox { Layout.fillWidth: true; model: [UiText.text("cover.inner_bg"), UiText.text("cover.transparent")]; currentIndex: root.session && root.session.activeLayer && root.session.activeLayer.frameBgMode === "transparent" ? 1 : 0; onActivated: if (root.session) root.session.setActiveLayerFrameBackgroundMode(currentIndex === 1 ? "transparent" : "image") }
                            }
                        }

                        Rectangle { Layout.fillWidth: true; height: 1; color: Theme.colors.border.normal }

                        Text { Layout.fillWidth: true; text: UiText.text("cover.manage_presets_2"); color: Theme.colors.text.active; font.family: Theme.uiFont; font.bold: true }
                        RowLayout { Layout.fillWidth: true
                            AppTextField { id: presetName; Layout.fillWidth: true; placeholderText: UiText.text("cover.preset_name") }
                            AppButton { text: UiText.text("cover.save_preset"); onClicked: if (root.session) { root.session.savePreset(presetName.text); presetName.clear() } }
                        }
                        Repeater {
                            model: root.session ? root.session.presets : []
                            delegate: RowLayout {
                                required property var modelData
                                Layout.fillWidth: true
                                Text { Layout.fillWidth: true; text: modelData.name; color: Theme.colors.text.secondary; font.family: Theme.uiFont; elide: Text.ElideRight }
                                AppButton { text: UiText.text("cover.apply_preset"); onClicked: root.session.applyPreset(modelData.name) }
                                AppButton { text: UiText.text("cover.delete_preset"); onClicked: root.session.removePreset(modelData.name) }
                            }
                        }
                    }
                }
            }
        }
    }
}
