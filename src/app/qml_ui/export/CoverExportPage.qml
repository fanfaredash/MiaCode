pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// v2 封面合成器。三栏工作区：左图层、中同源 CoverComposer 预览、右检查器。
//
// 版式与 v2 其余表面同构，不再自成一套：
//   * 工作区照抄 MainSplitView —— 面板是平的 surface + PanelHeader，彼此只靠
//     SplitHandle 的 1px 线分隔，没有圆角卡片边框；
//   * 表单行一律走 LabeledCombo / LabeledSlider，标签列因此对齐，滑杆也拿到了
//     读数和双击输入（直接给 AppSlider 绑 value 会在第一次拖动后把绑定打断，
//     LabeledSlider 的 `Binding ... when: !pressed` 就是为这个存在的）；
//   * 右栏按 画板 / 图层 / 预设 分成 panelTab；难度卡设置并入图层页下半，
//     和 导出中心、预览设置一样，而不是一条五段的长滚动。
//
// 图层行只读地表达状态。ChromeRow 的高亮铺满整行，交互子项放进 contentItem 会被
// 它从底下穿过去，所以显示/锁定的开关留在右栏「图层」页，行里不放按钮。
Rectangle {
    id: root

    required property var pages
    required property var coverSession
    readonly property var session: root.coverSession

    // 右栏分组。选中任意图层后都回到图层检查器，保证当前选择的属性可见。
    property string inspectorTab: "canvas"

    readonly property var activeLayer: root.session ? root.session.activeLayer : null
    readonly property string activeLayerKind: root.activeLayer ? root.activeLayer.kind : ""
    readonly property bool chartFrameInteractive:
        !!root.session && root.activeLayerKind === "chartFrame"
        && root.activeLayer.visible
        && root.session.chartFrameDuration > 0
        && root.session.chartFrameAvailable
        && !root.session.busy

    // LabeledCombo 吃 { value, label }；字体库给的是 { path, label, family }。
    readonly property var fontOptions: {
        const source = root.session ? root.session.fontLibraryOptions : []
        const options = []
        for (let index = 0; index < source.length; ++index)
            options.push({ value: source[index].path, label: source[index].label })
        return options
    }

    function baseName(path) {
        const parts = String(path).split(/[\\/]/)
        return parts[parts.length - 1] || path
    }

    function showLayerInspector(key) {
        if (key)
            root.inspectorTab = "layer"
    }

    function selectLayerFromUi(key) {
        if (!key || !root.session)
            return
        root.showLayerInspector(key)
        root.session.selectLayerKey(key)
    }

    color: Theme.surfaceColor("panel", Theme.colors.background.surface)
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: UiText.text("cover.export_cover")
                color: Theme.colors.text.active
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                font.bold: true
                elide: Text.ElideRight
            }

            Item { Layout.fillWidth: true }

            AppButton {
                id: layoutMenuButton
                objectName: "coverLayoutMenuButton"
                text: UiText.text("cover.layout")
                enabled: !!root.session && !root.session.busy
                onClicked: layoutMenu.visible ? layoutMenu.close() : layoutMenu.openAt(layoutMenuButton)
            }
            AppButton {
                objectName: "coverExportButton"
                text: UiText.text("cover.export")
                emphasized: true
                enabled: !!root.session && !root.session.busy
                onClicked: root.session.exportCover()
            }
            AppButton {
                objectName: "coverCloseButton"
                text: UiText.text("cover.close")
                onClicked: root.pages.leaveOverlayPage()
            }
        }

        // 布局 ▾ —— v1 同名菜单的四项：重置 / 保存 / 导入 / 最近。
        AppMenu {
            id: layoutMenu
            openRightAligned: true
            hugContent: true

            AppMenuItem {
                objectName: "coverResetLayoutItem"
                text: UiText.text("cover.reset_to_default")
                onTriggered: root.session.resetLayout()
            }
            AppMenuItem {
                text: UiText.text("cover.save_layout_to_file")
                onTriggered: root.session.saveLayout()
            }
            AppMenuItem {
                text: UiText.text("cover.import_layout_file")
                onTriggered: root.session.importLayout()
            }
            AppMenuSeparator {}
            AppMenuItem {
                text: UiText.text("cover.no_recent_files")
                enabled: false
                height: visible ? implicitHeight : 0
                visible: !root.session || root.session.recentLayoutFiles.length === 0
            }
            Repeater {
                model: root.session ? root.session.recentLayoutFiles : []
                delegate: AppMenuItem {
                    required property string modelData
                    text: root.baseName(modelData)
                    onTriggered: root.session.openRecentLayout(modelData)
                }
            }
            AppMenuSeparator {}
            AppMenuItem {
                text: UiText.text("cover.clear_recent")
                enabled: !!root.session && root.session.recentLayoutFiles.length > 0
                onTriggered: root.session.clearRecentLayouts()
            }
        }

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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colors.border.normal
        }

        SplitView {
            id: workspace
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            handle: SplitHandle {}

            // ---- 图层 ----
            Rectangle {
                id: layerPane
                SplitView.minimumWidth: 190
                SplitView.preferredWidth: 240
                SplitView.maximumWidth: 360
                color: Theme.colors.background.surface
                clip: true

                PanelHeader {
                    id: layerHeading
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    title: UiText.text("cover.layers")

                    ChromeRow {
                        id: addLayerButton
                        objectName: "coverAddLayerButton"
                        implicitWidth: addLayerLabel.implicitWidth + leftPadding + rightPadding
                        tone: "icon"
                        focusPolicy: Qt.TabFocus
                        enabled: !!root.session && !root.session.busy
                        Accessible.name: UiText.text("cover.add_layer")
                        onClicked: addLayerMenu.visible ? addLayerMenu.close()
                                                        : addLayerMenu.openAt(addLayerButton)
                        contentItem: Text {
                            id: addLayerLabel
                            text: UiText.text("cover.add_layer")
                            color: !addLayerButton.enabled ? Theme.colors.text.disabled
                                 : (addLayerButton.hovered || addLayerButton.down)
                                   ? Theme.colors.text.active : Theme.colors.text.secondary
                            font.family: Theme.uiFont
                            font.pixelSize: Theme.uiFontSize
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }

                AppMenu {
                    id: addLayerMenu
                    openRightAligned: true
                    hugContent: true

                    AppMenuItem {
                        objectName: "coverAddChartFrameItem"
                        text: UiText.text("cover.add_chart_frame")
                        // 没有可渲染音符的难度加不了谱面帧；session 会解释原因，
                        // 但先在这里禁用，免得让人点了才知道。
                        enabled: !!root.session && root.session.chartFrameAvailable
                        onTriggered: root.session.addChartFrameLayer()
                    }
                    AppMenuItem {
                        text: UiText.text("cover.add_image")
                        onTriggered: root.session.addImageLayer()
                    }
                    AppMenuItem {
                        text: UiText.text("cover.add_text")
                        onTriggered: root.session.addTextLayer()
                    }
                }

                ListView {
                    id: layers
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: layerHeading.bottom
                    anchors.bottom: layerActions.top
                    anchors.margins: 6
                    clip: true
                    spacing: 2
                    model: root.session ? root.session.layoutModel.layers : []
                    ScrollBar.vertical: ScrollBar {}

                    // 状态写在行里，但不放交互子项：ChromeRow 的高亮铺满整行，
                    // 会从任何按钮底下穿过去（见 ChromeRow 的说明）。显示/锁定的
                    // 开关因此留在右栏「图层」页，这里只读地表达状态 —— 隐藏用
                    // disabled 灰，锁定用一个词，而不是彩色 emoji（emoji 在
                    // Windows 上按彩字渲染，拿不到主题色）。
                    delegate: ChromeRow {
                        id: layerRow
                        required property var modelData
                        width: ListView.view.width
                        implicitHeight: 30
                        selected: root.session && root.session.activeLayerKey === layerRow.modelData.key
                        onClicked: {
                            root.selectLayerFromUi(layerRow.modelData.key)
                        }

                        readonly property color labelColor:
                            !layerRow.modelData.visible ? Theme.colors.text.disabled
                          : layerRow.selected ? Theme.colors.text.active
                          : Theme.colors.text.secondary

                        contentItem: RowLayout {
                            spacing: 6
                            Text {
                                Layout.fillWidth: true
                                text: layerRow.modelData.label
                                color: layerRow.labelColor
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.uiFontSize
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: layerRow.modelData.locked
                                text: UiText.text("cover.lock")
                                color: Theme.colors.text.disabled
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.captionFontSize
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                ColumnLayout {
                    id: layerActions
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    spacing: 4

                    readonly property bool cardSelected: !root.session
                                                         || root.session.activeLayerKey === "card"
                    readonly property bool actionable: !!root.session && !root.session.busy
                                                       && !layerActions.cardSelected

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        AppButton {
                            Layout.fillWidth: true
                            text: UiText.text("cover.move_up")
                            enabled: layerActions.actionable
                            onClicked: root.session.raiseActiveLayer()
                        }
                        AppButton {
                            Layout.fillWidth: true
                            text: UiText.text("cover.move_down")
                            enabled: layerActions.actionable
                            onClicked: root.session.lowerActiveLayer()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        AppButton {
                            Layout.fillWidth: true
                            objectName: "coverDuplicateLayerButton"
                            text: UiText.text("cover.duplicate_layer")
                            enabled: layerActions.actionable
                            onClicked: root.session.duplicateActiveLayer()
                        }
                        AppButton {
                            Layout.fillWidth: true
                            text: UiText.text("cover.delete_the_selected_layer_delete")
                            enabled: layerActions.actionable
                            onClicked: root.session.removeActiveLayer()
                        }
                    }
                }
            }

            // ---- 画布 ----
            Rectangle {
                id: canvasPane
                SplitView.fillWidth: true
                SplitView.minimumWidth: 280
                color: Theme.colors.background.editor
                clip: true

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
                        onLoaded: {
                            if (item)
                                item.layerSelectionCallback = function(key) { root.showLayerInspector(key) }
                        }
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
                    Binding { target: composer.item; property: "activeChartFrameKey"; value: root.session ? root.session.activeLayerKey : ""; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "selectedKey"; value: root.session ? root.session.activeLayerKey : ""; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "selectionBinder"; value: root.session; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "chartSceneBinder"; value: root.session; when: composer.status === Loader.Ready }
                    Binding { target: composer.item; property: "editable"; value: true; when: composer.status === Loader.Ready }

                    BusyIndicator {
                        anchors.centerIn: parent
                        running: root.session && root.session.busy
                        visible: running
                        z: 2
                    }
                }
            }

            // ---- 检查器 ----
            Rectangle {
                id: inspectorPane
                SplitView.minimumWidth: 280
                SplitView.preferredWidth: 330
                SplitView.maximumWidth: 460
                color: Theme.colors.background.surface
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    Row {
                        Layout.fillWidth: true
                        spacing: 4
                        AppTab {
                            panelTab: true
                            text: UiText.text("cover.canvas")
                            active: root.inspectorTab === "canvas"
                            onClicked: root.inspectorTab = "canvas"
                        }
                        AppTab {
                            objectName: "coverLayerTab"
                            panelTab: true
                            text: UiText.text("cover.layer")
                            active: root.inspectorTab === "layer"
                            onClicked: root.inspectorTab = "layer"
                        }
                        AppTab {
                            panelTab: true
                            text: UiText.text("cover.manage_presets")
                            active: root.inspectorTab === "preset"
                            onClicked: root.inspectorTab = "preset"
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 1
                        color: Theme.colors.border.normal
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        contentWidth: width
                        contentHeight: inspector.implicitHeight
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}

                        ColumnLayout {
                            id: inspector
                            width: parent.width
                            spacing: 10

                            // ---- 画板 ----
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: root.inspectorTab === "canvas"
                                spacing: 10

                                LabeledCombo {
                                    objectName: "coverResolutionCombo"
                                    label: UiText.text("cover.size")
                                    labelWidth: 96
                                    options: {
                                        const source = root.session ? root.session.resolutionOptions : []
                                        const options = []
                                        for (let index = 0; index < source.length; ++index)
                                            options.push({ value: index, label: source[index].label })
                                        return options
                                    }
                                    currentValue: root.session ? root.session.resolutionIndex : 0
                                    onPicked: function(value) { if (root.session) root.session.resolutionIndex = value }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    Text {
                                        Layout.preferredWidth: 96
                                        text: UiText.text("输出文件夹")
                                        color: Theme.colors.text.secondary
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.uiFontSize
                                        wrapMode: Text.WordWrap
                                    }
                                    AppTextField {
                                        objectName: "coverOutputDirectoryField"
                                        Layout.fillWidth: true
                                        text: root.session ? root.session.outputDirectory : ""
                                        onEditingFinished: if (root.session) root.session.outputDirectory = text
                                    }
                                    AppButton {
                                        text: UiText.text("cover.browse")
                                        onClicked: if (root.session) root.session.browseOutputDirectory()
                                    }
                                }

                                Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.colors.border.normal }

                                Text {
                                    text: UiText.text("cover.background")
                                    color: Theme.colors.text.active
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize
                                    font.bold: true
                                }

                                LabeledCombo {
                                    objectName: "coverBackgroundModeCombo"
                                    label: UiText.text("cover.background")
                                    labelWidth: 96
                                    options: [
                                        { value: 0, label: UiText.text("cover.jacket") },
                                        { value: 1, label: UiText.text("cover.custom_image") },
                                        { value: 2, label: UiText.text("cover.transparent") }
                                    ]
                                    currentValue: root.session ? root.session.backgroundMode : 0
                                    onPicked: function(value) { if (root.session) root.session.backgroundMode = value }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppButton {
                                        text: UiText.text("cover.choose_background_image")
                                        enabled: root.session && root.session.backgroundMode === 1
                                        onClicked: root.session.browseBackgroundImage()
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                AppSwitch {
                                    text: UiText.text("cover.blur_background")
                                    checked: root.session ? root.session.blurBackground : false
                                    enabled: root.session && root.session.backgroundMode !== 2
                                    onToggled: if (root.session) root.session.blurBackground = checked
                                }
                                LabeledSlider {
                                    label: UiText.text("cover.backdrop_brightness")
                                    labelWidth: 96
                                    from: 0
                                    to: 1
                                    stepSize: 0.01
                                    decimals: 0
                                    suffix: "%"
                                    readout: Math.round((root.session ? root.session.backgroundBrightness : 0.45) * 100) + "%"
                                    value: root.session ? root.session.backgroundBrightness : 0.45
                                    enabled: root.session && root.session.backgroundMode !== 2
                                    onMoved: function(value) { if (root.session) root.session.backgroundBrightness = value }
                                }
                            }

                            // ---- 图层 ----
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: root.inspectorTab === "layer"
                                spacing: 10

                                Text {
                                    Layout.fillWidth: true
                                    visible: !root.activeLayer
                                    text: UiText.text("cover.select_a_chart_frame_layer")
                                    color: Theme.colors.text.secondary
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize
                                    wrapMode: Text.WordWrap
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: !!root.activeLayer
                                    spacing: 10

                                    Text {
                                        text: root.activeLayer ? root.activeLayer.label : ""
                                        color: Theme.colors.text.active
                                        font.family: Theme.uiFont
                                        font.pixelSize: Theme.uiFontSize
                                        font.bold: true
                                        elide: Text.ElideRight
                                    }

                                    RowLayout {
                                        Layout.fillWidth: true
                                        AppSwitch {
                                            Layout.fillWidth: true
                                            text: UiText.text("cover.visible")
                                            checked: root.activeLayer ? root.activeLayer.visible : false
                                            onToggled: if (root.session) root.session.setActiveLayerVisible(checked)
                                        }
                                        AppSwitch {
                                            Layout.fillWidth: true
                                            text: UiText.text("cover.lock")
                                            checked: root.activeLayer ? root.activeLayer.locked : false
                                            onToggled: if (root.session) root.session.setActiveLayerLocked(checked)
                                        }
                                    }
                                    LabeledSlider {
                                        label: UiText.text("cover.opacity")
                                        labelWidth: 96
                                        from: 0
                                        to: 1
                                        stepSize: 0.01
                                        readout: Math.round((root.activeLayer ? root.activeLayer.opacity : 1) * 100) + "%"
                                        value: root.activeLayer ? root.activeLayer.opacity : 1
                                        onMoved: function(value) { if (root.session) root.session.setActiveLayerOpacity(value) }
                                    }
                                    LabeledSlider {
                                        label: UiText.text("cover.layer_size")
                                        labelWidth: 96
                                        from: 0.05
                                        to: 1.5
                                        stepSize: 0.01
                                        readout: Math.round((root.activeLayer ? root.activeLayer.sizeFraction : 0.85) * 100) + "%"
                                        value: root.activeLayer ? root.activeLayer.sizeFraction : 0.85
                                        onMoved: function(value) { if (root.session) root.session.setActiveLayerSizeFraction(value) }
                                    }
                                    RowLayout {
                                        Layout.fillWidth: true
                                        AppButton {
                                            Layout.fillWidth: true
                                            text: UiText.text("cover.send_to_back")
                                            onClicked: root.session.sendActiveLayerToBack()
                                        }
                                        AppButton {
                                            Layout.fillWidth: true
                                            text: UiText.text("cover.bring_to_front")
                                            onClicked: root.session.bringActiveLayerToFront()
                                        }
                                    }

                                    // ---- 图片选项 ----
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        visible: root.activeLayerKind === "image"
                                        spacing: 10
                                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.colors.border.normal }
                                        Text {
                                            text: UiText.text("cover.image_options")
                                            color: Theme.colors.text.active
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.uiFontSize
                                            font.bold: true
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            AppButton {
                                                text: UiText.text("cover.choose_image")
                                                onClicked: root.session.browseActiveLayerImage()
                                            }
                                            Item { Layout.fillWidth: true }
                                        }
                                    }

                                    // ---- 文字选项 ----
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        visible: root.activeLayerKind === "text"
                                        spacing: 10
                                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.colors.border.normal }
                                        Text {
                                            text: UiText.text("cover.text_options")
                                            color: Theme.colors.text.active
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.uiFontSize
                                            font.bold: true
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                Layout.preferredWidth: 96
                                                text: UiText.text("cover.text_content")
                                                color: Theme.colors.text.secondary
                                                font.family: Theme.uiFont
                                                font.pixelSize: Theme.uiFontSize
                                            }
                                            AppTextField {
                                                Layout.fillWidth: true
                                                text: root.activeLayer ? root.activeLayer.text : ""
                                                onEditingFinished: if (root.session) root.session.setActiveLayerText(text)
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Text {
                                                Layout.preferredWidth: 96
                                                text: UiText.text("cover.text_color")
                                                color: Theme.colors.text.secondary
                                                font.family: Theme.uiFont
                                                font.pixelSize: Theme.uiFontSize
                                            }
                                            AppTextField {
                                                Layout.fillWidth: true
                                                text: root.activeLayer ? root.activeLayer.textColor : "#FFFFFF"
                                                onEditingFinished: if (root.session) root.session.setActiveLayerTextColor(text)
                                            }
                                            // 色值是文本输入，一个小色板比读十六进制快。
                                            Rectangle {
                                                implicitWidth: 22
                                                implicitHeight: 22
                                                radius: Theme.controlRadius
                                                color: root.activeLayer ? root.activeLayer.textColor : "#FFFFFF"
                                                border.width: Theme.controlBorderWidth
                                                border.color: Theme.colors.border.control
                                            }
                                        }
                                        AppSwitch {
                                            text: UiText.text("cover.bold")
                                            checked: root.activeLayer ? root.activeLayer.textBold : false
                                            onToggled: if (root.session) root.session.setActiveLayerTextBold(checked)
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            AppButton {
                                                text: UiText.text("card_font.import")
                                                onClicked: root.session.importActiveLayerFont()
                                            }
                                            Item { Layout.fillWidth: true }
                                        }
                                    }

                                    // ---- 谱面帧选项 ----
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        visible: root.activeLayerKind === "chartFrame"
                                        spacing: 10
                                        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.colors.border.normal }
                                        Text {
                                            text: UiText.text("cover.chart_frame_options")
                                            color: Theme.colors.text.active
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.uiFontSize
                                            font.bold: true
                                        }
                                        FocusScope {
                                            id: frameTimeControls
                                            Layout.fillWidth: true
                                            implicitHeight: frameTimeRow.implicitHeight
                                            activeFocusOnTab: true
                                            readonly property bool inputEnabled: root.chartFrameInteractive

                                            function focusTransport() {
                                                if (inputEnabled && visible)
                                                    forceActiveFocus(Qt.OtherFocusReason)
                                            }

                                            onInputEnabledChanged: {
                                                if (inputEnabled)
                                                    Qt.callLater(focusTransport)
                                            }
                                            onVisibleChanged: {
                                                if (visible)
                                                    Qt.callLater(focusTransport)
                                            }
                                            Component.onCompleted: Qt.callLater(focusTransport)

                                            function noModifiers(event) {
                                                return event.modifiers === Qt.NoModifier
                                            }

                                            Keys.priority: Keys.BeforeItem
                                            Keys.onPressed: function(event) {
                                                if (!inputEnabled || !noModifiers(event))
                                                    return
                                                if (frameTimeSlider.valueEditing)
                                                    return
                                                if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                                                    if (!event.isAutoRepeat)
                                                        root.session.beginActiveLayerKeySeek(
                                                            event.key === Qt.Key_Left ? -1 : 1)
                                                    event.accepted = true
                                                } else if (event.key === Qt.Key_Space) {
                                                    if (!event.isAutoRepeat)
                                                        root.session.toggleActiveLayerPlayback()
                                                    event.accepted = true
                                                } else if (event.key === Qt.Key_Home || event.key === Qt.Key_End) {
                                                    if (!event.isAutoRepeat) {
                                                        root.session.cancelActiveLayerInput()
                                                        root.session.previewActiveLayerFrameSeconds(
                                                            event.key === Qt.Key_Home ? 0
                                                                                      : root.session.chartFrameDuration)
                                                        root.session.commitActiveLayerFrameSeconds()
                                                    }
                                                    event.accepted = true
                                                }
                                            }
                                            Keys.onReleased: function(event) {
                                                if (!inputEnabled || !noModifiers(event))
                                                    return
                                                if ((event.key === Qt.Key_Left || event.key === Qt.Key_Right)
                                                        && !event.isAutoRepeat) {
                                                    root.session.endActiveLayerKeySeek()
                                                    event.accepted = true
                                                }
                                            }
                                            onActiveFocusChanged: {
                                                if (!activeFocus && root.session)
                                                    root.session.cancelActiveLayerInput()
                                            }

                                            RowLayout {
                                                id: frameTimeRow
                                                anchors.left: parent.left
                                                anchors.right: parent.right
                                                spacing: 4

                                                LabeledSlider {
                                                    id: frameTimeSlider
                                                    objectName: "coverFrameTimeSlider"
                                                    Layout.fillWidth: true
                                                    label: UiText.text("cover.frame_time_for_the_selected")
                                                    labelWidth: 96
                                                    from: 0
                                                    to: root.session ? root.session.chartFrameDuration : 0
                                                    stepSize: 0.01
                                                    decimals: 2
                                                    suffix: " s"
                                                    enabled: frameTimeControls.inputEnabled
                                                    value: root.session ? root.session.activeChartFrameSeconds : 0
                                                    keyForwardTarget: frameTimeControls
                                                    onPressedChanged: {
                                                        if (pressed) {
                                                            frameTimeControls.forceActiveFocus(Qt.MouseFocusReason)
                                                            if (root.session)
                                                                root.session.cancelActiveLayerInput()
                                                        }
                                                    }
                                                    onMoved: function(value) {
                                                        if (root.session)
                                                            root.session.previewActiveLayerFrameSeconds(value)
                                                    }
                                                    onReleased: {
                                                        if (root.session)
                                                            root.session.commitActiveLayerFrameSeconds()
                                                    }
                                                }

                                                IconButton {
                                                    objectName: "coverFramePlaybackButton"
                                                    Layout.preferredWidth: implicitWidth
                                                    Layout.preferredHeight: implicitHeight
                                                    enabled: frameTimeControls.inputEnabled
                                                    keyForwardTarget: frameTimeControls
                                                    iconSource: Qt.resolvedUrl(
                                                        root.session && root.session.chartFramePlaying
                                                            ? "icons/pause.svg" : "icons/play.svg")
                                                    tooltip: UiText.text("cover.play_pause_space")
                                                    onClicked: {
                                                        frameTimeControls.forceActiveFocus(Qt.MouseFocusReason)
                                                        root.session.toggleActiveLayerPlayback()
                                                    }
                                                }
                                            }
                                        }
                                        LabeledCombo {
                                            label: UiText.text("cover.chart_frame_inner_background")
                                            labelWidth: 96
                                            options: [
                                                { value: "image", label: UiText.text("cover.inner_bg") },
                                                { value: "transparent", label: UiText.text("cover.transparent") }
                                            ]
                                            currentValue: root.activeLayer ? root.activeLayer.frameBgMode : "image"
                                            onPicked: function(value) { if (root.session) root.session.setActiveLayerFrameBackgroundMode(value) }
                                        }
                                        LabeledSlider {
                                            label: UiText.text("cover.chart_frame_background_brightness")
                                            labelWidth: 96
                                            from: 0
                                            to: 1
                                            stepSize: 0.01
                                            enabled: root.activeLayer && root.activeLayer.frameBgMode === "image"
                                            readout: Math.round((root.activeLayer ? root.activeLayer.frameBgBrightness : 0.8) * 100) + "%"
                                            value: root.activeLayer ? root.activeLayer.frameBgBrightness : 0.8
                                            onMoved: function(value) { if (root.session) root.session.setActiveLayerFrameBackgroundBrightness(value) }
                                        }
                                        LabeledSlider {
                                            label: UiText.text("cover.chart_frame_background_transparency")
                                            labelWidth: 96
                                            from: 0
                                            to: 1
                                            stepSize: 0.01
                                            enabled: root.activeLayer && root.activeLayer.frameBgMode === "transparent"
                                            readout: Math.round((root.activeLayer ? root.activeLayer.frameBgTransparency : 0.5) * 100) + "%"
                                            value: root.activeLayer ? root.activeLayer.frameBgTransparency : 0.5
                                            onMoved: function(value) { if (root.session) root.session.setActiveLayerFrameBackgroundTransparency(value) }
                                        }
                                    }
                                }
                            }

                            // ---- 难度卡（图层页下半） ----
                            ColumnLayout {
                                id: cardSettings
                                Layout.fillWidth: true
                                visible: root.inspectorTab === "layer"
                                spacing: 10

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.preferredHeight: 1
                                    color: Theme.colors.border.normal
                                }
                                Text {
                                    text: UiText.text("cover.difficulty_card_options")
                                    color: Theme.colors.text.active
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize
                                    font.bold: true
                                }
                                LabeledCombo {
                                    objectName: "coverCardModeCombo"
                                    label: UiText.text("cover.chart_type")
                                    labelWidth: 96
                                    options: [
                                        { value: "auto", label: UiText.text("自动") },
                                        { value: "DX", label: "DX" },
                                        { value: "Standard", label: "Standard" }
                                    ]
                                    currentValue: root.session ? root.session.cardMode : "auto"
                                    onPicked: function(value) { if (root.session) root.session.cardMode = value }
                                }
                                AppSwitch {
                                    text: UiText.text("cover.card_drop_shadow")
                                    checked: root.session ? root.session.cardShadow : false
                                    onToggled: if (root.session) root.session.cardShadow = checked
                                }
                                AppSwitch {
                                    text: UiText.text("cover.render_level_as_text")
                                    checked: root.session ? root.session.levelTextRender : false
                                    onToggled: if (root.session) root.session.levelTextRender = checked
                                }
                                LabeledCombo {
                                    objectName: "coverLongTextCombo"
                                    label: UiText.text("cover.long_text")
                                    labelWidth: 96
                                    options: [
                                        { value: "shrink", label: UiText.text("cover.shrink_to_fit") },
                                        { value: "ellipsis", label: UiText.text("cover.keep_size_ellipsis") }
                                    ]
                                    currentValue: root.session ? root.session.longTextMode : "shrink"
                                    onPicked: function(value) { if (root.session) root.session.longTextMode = value }
                                }

                                Text {
                                    text: UiText.text("cover.font")
                                    color: Theme.colors.text.active
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize
                                    font.bold: true
                                }
                                LabeledCombo {
                                    objectName: "coverCardDisplayFontCombo"
                                    label: UiText.text("card_font.title")
                                    labelWidth: 96
                                    options: root.fontOptions
                                    currentValue: root.session ? root.session.cardFontDisplayPath : ""
                                    onPicked: function(value) { if (root.session) root.session.cardFontDisplayPath = value }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppButton {
                                        text: UiText.text("card_font.import")
                                        onClicked: if (root.session) root.session.importCardDisplayFont()
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                LabeledCombo {
                                    objectName: "coverCardBodyFontCombo"
                                    label: UiText.text("card_font.body")
                                    labelWidth: 96
                                    options: root.fontOptions
                                    currentValue: root.session ? root.session.cardFontBodyPath : ""
                                    onPicked: function(value) { if (root.session) root.session.cardFontBodyPath = value }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppButton {
                                        text: UiText.text("card_font.import")
                                        onClicked: if (root.session) root.session.importCardBodyFont()
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }

                            // ---- 预设 ----
                            ColumnLayout {
                                Layout.fillWidth: true
                                visible: root.inspectorTab === "preset"
                                spacing: 10

                                RowLayout {
                                    Layout.fillWidth: true
                                    AppTextField {
                                        id: presetName
                                        objectName: "coverPresetNameField"
                                        Layout.fillWidth: true
                                        placeholderText: UiText.text("cover.preset_name")
                                    }
                                    AppButton {
                                        text: UiText.text("cover.save_preset")
                                        enabled: presetName.text.trim().length > 0
                                        onClicked: {
                                            root.session.savePreset(presetName.text)
                                            presetName.clear()
                                        }
                                    }
                                }

                                Text {
                                    Layout.fillWidth: true
                                    visible: !root.session || root.session.presets.length === 0
                                    text: UiText.text("cover.no_presets")
                                    color: Theme.colors.text.secondary
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize
                                }

                                Repeater {
                                    model: root.session ? root.session.presets : []
                                    delegate: RowLayout {
                                        id: presetRow
                                        required property var modelData
                                        Layout.fillWidth: true
                                        spacing: 4
                                        Text {
                                            Layout.fillWidth: true
                                            text: presetRow.modelData.name
                                            color: Theme.colors.text.secondary
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.uiFontSize
                                            elide: Text.ElideRight
                                        }
                                        AppButton {
                                            text: UiText.text("cover.apply_preset")
                                            onClicked: root.session.applyPreset(presetRow.modelData.name)
                                        }
                                        AppButton {
                                            text: UiText.text("cover.delete_preset")
                                            onClicked: root.session.removePreset(presetRow.modelData.name)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
