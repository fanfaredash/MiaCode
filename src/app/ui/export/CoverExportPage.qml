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
//   * 右栏按 画板 / 图层 分成 panelTab；图层页只显示选中那一层的设置，难度卡
//     设置跟着「难度卡」这一层走；预设是「布局 ▾」里的二级菜单。
//
// 图层行只读地表达状态。ChromeRow 的高亮铺满整行，交互子项放进 contentItem 会被
// 它从底下穿过去，所以显示/锁定的开关留在右栏「图层」页，行里不放按钮。
Rectangle {
    id: root

    required property var coverSession
    readonly property var session: root.coverSession
    signal closeRequested()

    // 右栏分组。选中任意图层后都回到图层检查器，保证当前选择的属性可见。
    property string inspectorTab: "canvas"
    property real canvasZoom: 1.0

    function setCanvasZoom(value) {
        root.canvasZoom = Math.max(0.5, Math.min(2.0, value))
    }

    function resetCanvasZoom() {
        root.setCanvasZoom(1.0)
    }

    function zoomCanvasIn() {
        root.setCanvasZoom(root.canvasZoom * 1.25)
    }

    function zoomCanvasOut() {
        root.setCanvasZoom(root.canvasZoom / 1.25)
    }

    Shortcut {
        sequence: "Ctrl+0"
        enabled: !!root.session && !root.session.busy
        onActivated: root.resetCanvasZoom()
    }
    Shortcut {
        sequence: "Ctrl++"
        enabled: !!root.session && !root.session.busy
        onActivated: root.zoomCanvasIn()
    }
    Shortcut {
        sequence: "Ctrl+-"
        enabled: !!root.session && !root.session.busy
        onActivated: root.zoomCanvasOut()
    }

    readonly property var activeLayer: root.session ? root.session.activeLayer : null
    // 难度卡是固定的一层（key 为 "card"），它的设置只在选中它时出现。
    readonly property bool cardLayerActive: !!root.session && root.session.activeLayerKey === "card"
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

    // 检查器标签列。标签都是短词，88px 容得下中/英/日三种文案而不折行。
    readonly property int labelWidth: 88

    component FormLabel: Text {
        Layout.preferredWidth: root.labelWidth
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        elide: Text.ElideRight
    }

    // 图层的 label 是模型里写死的英文默认名（"Difficulty card" 等，也原样存进
    // 布局文件），界面上没有改名入口，所以按种类显示本地化名称。
    function layerDisplayName(layer) {
        if (!layer)
            return ""
        switch (layer.kind) {
        case "card": return qsTrId("cover.difficulty_card")
        case "chartFrame": return qsTrId("cover.chart_frame")
        case "image": return qsTrId("cover.image_layer")
        case "text": return qsTrId("cover.text_layer_default")
        }
        return layer.label
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

    color: Theme.surfaceColor(Theme.colors.background.panel)
    clip: true
    implicitWidth: layerPane.SplitView.minimumWidth + canvasPane.SplitView.minimumWidth
                   + inspectorPane.SplitView.minimumWidth + 2 * Theme.splitDividerThickness
                   + 2 * content.anchors.margins

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: 16
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Text {
                text: qsTrId("cover.export_cover")
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
                text: qsTrId("cover.layout")
                selected: layoutMenu.active
                enabled: !!root.session && !root.session.busy
                onClicked: layoutMenu.active ? layoutMenu.close() : layoutMenu.openAt(layoutMenuButton)
            }
            AppButton {
                objectName: "coverExportButton"
                text: qsTrId("cover.export")
                emphasized: true
                enabled: !!root.session && !root.session.busy
                onClicked: root.session.exportCover()
            }
            AppButton {
                objectName: "coverCloseButton"
                text: qsTrId("cover.close")
                enabled: !!root.session && !root.session.busy
                onClicked: root.closeRequested()
            }
        }

        // 布局 ▾ —— v1 同名菜单的四项：重置 / 保存 / 导入 / 最近。
        AppMenu {
            id: layoutMenu
            openRightAligned: true
            hugContent: true

            AppMenuItem {
                objectName: "coverResetLayoutItem"
                text: qsTrId("cover.reset_to_default")
                onTriggered: root.session.resetLayout()
            }
            AppMenuItem {
                text: qsTrId("cover.save_layout_to_file")
                onTriggered: root.session.saveLayout()
            }
            AppMenuItem {
                text: qsTrId("cover.import_layout_file")
                onTriggered: root.session.importLayout()
            }
            AppMenuSeparator {}
            AppMenuItem {
                text: qsTrId("cover.no_recent_files")
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
                text: qsTrId("cover.clear_recent")
                enabled: !!root.session && root.session.recentLayoutFiles.length > 0
                onTriggered: root.session.clearRecentLayouts()
            }
            AppMenuSeparator {}
            // 预设是二级菜单：内置版式和已保存的预设点一下就应用；保存、改名、
            // 删除要输入名字，才进「管理预设」对话框。
            AppMenu {
                id: presetMenu
                objectName: "coverPresetMenu"
                title: qsTrId("cover.presets")

                Repeater {
                    model: root.session ? root.session.builtinPresets : []
                    delegate: AppMenuItem {
                        required property var modelData
                        text: modelData.label
                        enabled: !!root.session
                                 && (!modelData.requiresChartFrame || root.session.chartFrameAvailable)
                        onTriggered: root.session.applyBuiltinPreset(modelData.id)
                    }
                }
                AppMenuSeparator {
                    visible: !!root.session && root.session.presets.length > 0
                    height: visible ? implicitHeight : 0
                }
                Repeater {
                    model: root.session ? root.session.presets : []
                    delegate: AppMenuItem {
                        required property var modelData
                        text: modelData.name
                        onTriggered: root.session.applyPreset(modelData.name)
                    }
                }
                AppMenuSeparator {}
                AppMenuItem {
                    objectName: "coverManagePresetsItem"
                    text: qsTrId("cover.manage_presets")
                    onTriggered: presetDialog.open()
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 4
            Repeater {
                model: root.session ? root.session.difficulties : []
                delegate: AppTab {
                    required property var modelData
                    panelTab: true
                    text: modelData.name
                    difficultyId: modelData.id
                    active: root.session && root.session.selectedDifficultyId === modelData.id
                    onClicked: if (root.session) root.session.selectDifficulty(modelData.id)
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
                color: Theme.colors.background.panel
                clip: true

                PanelHeader {
                    id: layerHeading
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    title: qsTrId("cover.layers")

                    ChromeRow {
                        id: addLayerButton
                        objectName: "coverAddLayerButton"
                        selected: addLayerMenu.active
                        implicitWidth: addLayerLabel.implicitWidth + leftPadding + rightPadding
                        focusPolicy: Qt.TabFocus
                        enabled: !!root.session && !root.session.busy
                        Accessible.name: qsTrId("cover.add_layer")
                        onClicked: addLayerMenu.active ? addLayerMenu.close()
                                                        : addLayerMenu.openAt(addLayerButton)
                        contentItem: Text {
                            id: addLayerLabel
                            text: qsTrId("cover.add_layer")
                            color: !addLayerButton.enabled ? Theme.colors.text.disabled
                                 : (addLayerButton.selected || addLayerButton.hovered || addLayerButton.visualFocus)
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
                        text: qsTrId("cover.add_chart_frame")
                        // 没有可渲染音符的难度加不了谱面帧；session 会解释原因，
                        // 但先在这里禁用，免得让人点了才知道。
                        enabled: !!root.session && root.session.chartFrameAvailable
                        onTriggered: root.session.addChartFrameLayer()
                    }
                    AppMenuItem {
                        text: qsTrId("cover.add_image")
                        onTriggered: root.session.addImageLayer()
                    }
                    AppMenuItem {
                        text: qsTrId("cover.add_text")
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
                    ScrollBar.vertical: AppScrollBar {}

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
                                text: root.layerDisplayName(layerRow.modelData)
                                color: layerRow.labelColor
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.uiFontSize
                                verticalAlignment: Text.AlignVCenter
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: layerRow.modelData.locked
                                text: qsTrId("cover.lock")
                                color: Theme.colors.text.disabled
                                font.family: Theme.uiFont
                                font.pixelSize: Theme.captionFontSize
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
                }

                // 图层操作收成一排图标，悬停看全称。窄栏里两行文字按钮既占高度，
                // 「删除当前图层」这类长文案也放不下。
                RowLayout {
                    id: layerActions
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    anchors.margins: 6
                    spacing: 0

                    readonly property bool cardSelected: !root.session
                                                         || root.session.activeLayerKey === "card"
                    readonly property bool actionable: !!root.session && !root.session.busy
                                                       && !layerActions.cardSelected

                    Item { Layout.fillWidth: true }
                    IconButton {
                        iconSource: Qt.resolvedUrl("icons/arrow-up.svg")
                        tooltip: qsTrId("cover.move_up")
                        enabled: layerActions.actionable
                        onClicked: root.session.raiseActiveLayer()
                    }
                    IconButton {
                        iconSource: Qt.resolvedUrl("icons/arrow-down.svg")
                        tooltip: qsTrId("cover.move_down")
                        enabled: layerActions.actionable
                        onClicked: root.session.lowerActiveLayer()
                    }
                    IconButton {
                        objectName: "coverDuplicateLayerButton"
                        iconSource: Qt.resolvedUrl("icons/copy.svg")
                        tooltip: qsTrId("cover.duplicate_layer")
                        enabled: layerActions.actionable
                        onClicked: root.session.duplicateActiveLayer()
                    }
                    IconButton {
                        iconSource: Qt.resolvedUrl("icons/trash.svg")
                        tooltip: qsTrId("cover.delete_layer")
                        enabled: layerActions.actionable
                        onClicked: root.session.removeActiveLayer()
                    }
                    Item { Layout.fillWidth: true }
                }
            }

            // ---- 画布 ----
            Rectangle {
                id: canvasPane
                SplitView.fillWidth: true
                SplitView.minimumWidth: 280
                color: Theme.colors.background.surface
                clip: true

                Item {
                    id: canvasFrame
                    anchors.fill: parent
                    anchors.margins: 12

                    Loader {
                        id: composer
                        anchors.centerIn: parent
                        scale: root.canvasZoom
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
            // 图层页只放当前选中那一层的设置：通用的不透明度/大小，再加该层
            // 种类自己的一节。难度卡设置属于「难度卡」这一层，不再挂在每一层
            // 下面拖成长滚动。标签列用短词（节标题已经交代了是谁的设置），
            // 这样 280px 的最窄检查器里也不会折行。
            Rectangle {
                id: inspectorPane
                SplitView.minimumWidth: 280
                SplitView.preferredWidth: 330
                SplitView.maximumWidth: 460
                color: Theme.colors.background.panel
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
                            text: qsTrId("cover.canvas")
                            active: root.inspectorTab === "canvas"
                            onClicked: root.inspectorTab = "canvas"
                        }
                        AppTab {
                            objectName: "coverLayerTab"
                            panelTab: true
                            text: qsTrId("cover.layer")
                            active: root.inspectorTab === "layer"
                            onClicked: root.inspectorTab = "layer"
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
                        ScrollBar.vertical: AppScrollBar {}

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
                                    label: qsTrId("cover.size")
                                    labelWidth: root.labelWidth
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
                                    FormLabel { text: qsTrId("cover.output") }
                                    // 显示相对谱面文件夹的路径（covers、.、~/…），完整路径在悬停提示里；
                                    // 输入相对路径同样按谱面文件夹解析。
                                    AppTextField {
                                        id: outputDirectoryField
                                        objectName: "coverOutputDirectoryField"
                                        Layout.fillWidth: true
                                        text: root.session ? root.session.outputDirectoryDisplay : ""
                                        onEditingFinished: {
                                            if (root.session)
                                                root.session.outputDirectory = text
                                            // 手动输入会打断绑定；提交后回到规范写法。
                                            text = Qt.binding(function() {
                                                return root.session ? root.session.outputDirectoryDisplay : ""
                                            })
                                        }

                                        HoverHandler { id: outputDirectoryHover }
                                        Tooltip {
                                            visible: outputDirectoryHover.hovered && !outputDirectoryField.activeFocus
                                                     && !!root.session && root.session.outputDirectory.length > 0
                                            text: root.session ? root.session.outputDirectory : ""
                                        }
                                    }
                                    IconButton {
                                        iconSource: Qt.resolvedUrl("icons/folder-open.svg")
                                        tooltip: qsTrId("cover.browse")
                                        onClicked: if (root.session) root.session.browseOutputDirectory()
                                    }
                                }

                                SettingsSection {
                                    Layout.fillWidth: true
                                    title: qsTrId("cover.background")

                                    LabeledCombo {
                                        objectName: "coverBackgroundModeCombo"
                                        label: qsTrId("cover.background")
                                        labelWidth: root.labelWidth
                                        options: [
                                            { value: 0, label: qsTrId("cover.jacket") },
                                            { value: 1, label: qsTrId("cover.custom_image") },
                                            { value: 2, label: qsTrId("cover.transparent") }
                                        ]
                                        currentValue: root.session ? root.session.backgroundMode : 0
                                        onPicked: function(value) { if (root.session) root.session.backgroundMode = value }

                                        IconButton {
                                            iconSource: Qt.resolvedUrl("icons/folder-open.svg")
                                            tooltip: qsTrId("cover.choose_background_image")
                                            enabled: !!root.session && root.session.backgroundMode === 1
                                            onClicked: root.session.browseBackgroundImage()
                                        }
                                    }
                                    LabeledSlider {
                                        label: qsTrId("cover.brightness")
                                        labelWidth: root.labelWidth
                                        from: 0
                                        to: 1
                                        stepSize: 0.01
                                        decimals: 0
                                        suffix: "%"
                                        readout: Math.round((root.session ? root.session.backgroundBrightness : 0.45) * 100) + "%"
                                        value: root.session ? root.session.backgroundBrightness : 0.45
                                        enabled: !!root.session && root.session.backgroundMode !== 2
                                        onMoved: function(value) { if (root.session) root.session.backgroundBrightness = value }
                                    }
                                    AppSwitch {
                                        text: qsTrId("cover.blur_background")
                                        checked: root.session ? root.session.blurBackground : false
                                        enabled: !!root.session && root.session.backgroundMode !== 2
                                        onToggled: if (root.session) root.session.blurBackground = checked
                                    }
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
                                    text: qsTrId("cover.select_a_chart_frame_layer")
                                    color: Theme.colors.text.secondary
                                    font.family: Theme.uiFont
                                    font.pixelSize: Theme.uiFontSize
                                    wrapMode: Text.WordWrap
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: !!root.activeLayer
                                    spacing: 10

                                    // 层名就是这一页的节标题；显示/锁定跟着它，
                                    // 不再各占一行开关。
                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 2

                                        Text {
                                            Layout.fillWidth: true
                                            text: root.layerDisplayName(root.activeLayer)
                                            color: Theme.colors.text.section
                                            font.family: Theme.uiFont
                                            font.pixelSize: Theme.sectionTitleFontSize
                                            font.bold: true
                                            elide: Text.ElideRight
                                        }
                                        IconButton {
                                            objectName: "coverLayerVisibleButton"
                                            readonly property bool layerVisible: !!root.activeLayer && root.activeLayer.visible
                                            iconSource: Qt.resolvedUrl(layerVisible ? "icons/eye.svg" : "icons/eye-off.svg")
                                            tooltip: layerVisible ? qsTrId("cover.hide") : qsTrId("cover.show")
                                            onClicked: if (root.session) root.session.setActiveLayerVisible(!layerVisible)
                                        }
                                        IconButton {
                                            objectName: "coverLayerLockButton"
                                            readonly property bool layerLocked: !!root.activeLayer && root.activeLayer.locked
                                            iconSource: Qt.resolvedUrl(layerLocked ? "icons/lock.svg" : "icons/lock-open.svg")
                                            tooltip: layerLocked ? qsTrId("cover.unlock") : qsTrId("cover.lock")
                                            onClicked: if (root.session) root.session.setActiveLayerLocked(!layerLocked)
                                        }
                                    }

                                    LabeledSlider {
                                        label: qsTrId("cover.opacity")
                                        labelWidth: root.labelWidth
                                        from: 0
                                        to: 1
                                        stepSize: 0.01
                                        readout: Math.round((root.activeLayer ? root.activeLayer.opacity : 1) * 100) + "%"
                                        value: root.activeLayer ? root.activeLayer.opacity : 1
                                        onMoved: function(value) { if (root.session) root.session.setActiveLayerOpacity(value) }
                                    }
                                    LabeledSlider {
                                        label: qsTrId("cover.size")
                                        labelWidth: root.labelWidth
                                        from: 0.05
                                        to: 1.5
                                        stepSize: 0.01
                                        readout: Math.round((root.activeLayer ? root.activeLayer.sizeFraction : 0.85) * 100) + "%"
                                        value: root.activeLayer ? root.activeLayer.sizeFraction : 0.85
                                        onMoved: function(value) { if (root.session) root.session.setActiveLayerSizeFraction(value) }
                                    }

                                    // ---- 图片 ----
                                    SettingsSection {
                                        Layout.fillWidth: true
                                        visible: root.activeLayerKind === "image"
                                        title: qsTrId("cover.image_options")

                                        RowLayout {
                                            Layout.fillWidth: true
                                            FormLabel { text: qsTrId("cover.image_file") }
                                            AppButton {
                                                text: qsTrId("cover.choose_image")
                                                onClicked: root.session.browseActiveLayerImage()
                                            }
                                            Item { Layout.fillWidth: true }
                                        }
                                    }

                                    // ---- 文字 ----
                                    SettingsSection {
                                        Layout.fillWidth: true
                                        visible: root.activeLayerKind === "text"
                                        title: qsTrId("cover.text_options")

                                        RowLayout {
                                            Layout.fillWidth: true
                                            FormLabel { text: qsTrId("cover.text_content") }
                                            AppTextField {
                                                Layout.fillWidth: true
                                                text: root.activeLayer ? root.activeLayer.text : ""
                                                onEditingFinished: if (root.session) root.session.setActiveLayerText(text)
                                            }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            FormLabel { text: qsTrId("cover.text_color") }
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
                                        RowLayout {
                                            Layout.fillWidth: true
                                            AppSwitch {
                                                text: qsTrId("cover.bold")
                                                checked: root.activeLayer ? root.activeLayer.textBold : false
                                                onToggled: if (root.session) root.session.setActiveLayerTextBold(checked)
                                            }
                                            Item { Layout.fillWidth: true }
                                            AppButton {
                                                text: qsTrId("card_font.import")
                                                onClicked: root.session.importActiveLayerFont()
                                            }
                                        }
                                    }

                                    // ---- 谱面帧 ----
                                    SettingsSection {
                                        Layout.fillWidth: true
                                        visible: root.activeLayerKind === "chartFrame"
                                        title: qsTrId("cover.chart_frame_options")

                                        FocusScope {
                                            id: frameTimeControls
                                            Layout.fillWidth: true
                                            implicitHeight: frameTimeRow.implicitHeight
                                            activeFocusOnTab: true
                                            property bool reservesPlainSpace: true
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
                                                    label: qsTrId("cover.frame_time")
                                                    labelWidth: root.labelWidth
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
                                                    tooltip: qsTrId("cover.play_pause_space")
                                                    onClicked: {
                                                        frameTimeControls.forceActiveFocus(Qt.MouseFocusReason)
                                                        root.session.toggleActiveLayerPlayback()
                                                    }
                                                }
                                            }
                                        }
                                        LabeledCombo {
                                            label: qsTrId("cover.inner")
                                            labelWidth: root.labelWidth
                                            options: [
                                                { value: "image", label: qsTrId("cover.inner_bg") },
                                                { value: "transparent", label: qsTrId("cover.transparent") }
                                            ]
                                            currentValue: root.activeLayer ? root.activeLayer.frameBgMode : "image"
                                            onPicked: function(value) { if (root.session) root.session.setActiveLayerFrameBackgroundMode(value) }
                                        }
                                        LabeledSlider {
                                            label: qsTrId("cover.brightness")
                                            labelWidth: root.labelWidth
                                            from: 0
                                            to: 1
                                            stepSize: 0.01
                                            enabled: root.activeLayer && root.activeLayer.frameBgMode === "image"
                                            readout: Math.round((root.activeLayer ? root.activeLayer.frameBgBrightness : 0.8) * 100) + "%"
                                            value: root.activeLayer ? root.activeLayer.frameBgBrightness : 0.8
                                            onMoved: function(value) { if (root.session) root.session.setActiveLayerFrameBackgroundBrightness(value) }
                                        }
                                        LabeledSlider {
                                            label: qsTrId("cover.transparency")
                                            labelWidth: root.labelWidth
                                            from: 0
                                            to: 1
                                            stepSize: 0.01
                                            enabled: root.activeLayer && root.activeLayer.frameBgMode === "transparent"
                                            readout: Math.round((root.activeLayer ? root.activeLayer.frameBgTransparency : 0.5) * 100) + "%"
                                            value: root.activeLayer ? root.activeLayer.frameBgTransparency : 0.5
                                            onMoved: function(value) { if (root.session) root.session.setActiveLayerFrameBackgroundTransparency(value) }
                                        }
                                    }

                                    // ---- 难度卡 ----
                                    SettingsSection {
                                        id: cardSettings
                                        Layout.fillWidth: true
                                        visible: root.cardLayerActive
                                        title: qsTrId("cover.difficulty_card_options")

                                        LabeledCombo {
                                            objectName: "coverCardModeCombo"
                                            label: qsTrId("cover.chart_type")
                                            labelWidth: root.labelWidth
                                            options: [
                                                { value: "auto", label: qsTrId("qml.automatic") },
                                                { value: "DX", label: "DX" },
                                                { value: "Standard", label: "Standard" }
                                            ]
                                            currentValue: root.session ? root.session.cardMode : "auto"
                                            onPicked: function(value) { if (root.session) root.session.cardMode = value }
                                        }
                                        LabeledCombo {
                                            objectName: "coverLongTextCombo"
                                            label: qsTrId("cover.long_text_label")
                                            labelWidth: root.labelWidth
                                            options: [
                                                { value: "shrink", label: qsTrId("cover.shrink_to_fit") },
                                                { value: "ellipsis", label: qsTrId("cover.keep_size_ellipsis") }
                                            ]
                                            currentValue: root.session ? root.session.longTextMode : "shrink"
                                            onPicked: function(value) { if (root.session) root.session.longTextMode = value }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            AppSwitch {
                                                Layout.fillWidth: true
                                                text: qsTrId("cover.shadow")
                                                checked: root.session ? root.session.cardShadow : false
                                                onToggled: if (root.session) root.session.cardShadow = checked
                                            }
                                            AppSwitch {
                                                Layout.fillWidth: true
                                                text: qsTrId("cover.level_as_text")
                                                checked: root.session ? root.session.levelTextRender : false
                                                onToggled: if (root.session) root.session.levelTextRender = checked
                                            }
                                        }
                                    }

                                    SettingsSection {
                                        Layout.fillWidth: true
                                        visible: root.cardLayerActive
                                        title: qsTrId("cover.font")

                                        LabeledCombo {
                                            objectName: "coverCardDisplayFontCombo"
                                            label: qsTrId("cover.title_font")
                                            labelWidth: root.labelWidth
                                            options: root.fontOptions
                                            currentValue: root.session ? root.session.cardFontDisplayPath : ""
                                            onPicked: function(value) { if (root.session) root.session.cardFontDisplayPath = value }

                                            IconButton {
                                                iconSource: Qt.resolvedUrl("icons/folder-open.svg")
                                                tooltip: qsTrId("card_font.import")
                                                onClicked: if (root.session) root.session.importCardDisplayFont()
                                            }
                                        }
                                        LabeledCombo {
                                            objectName: "coverCardBodyFontCombo"
                                            label: qsTrId("cover.body_font")
                                            labelWidth: root.labelWidth
                                            options: root.fontOptions
                                            currentValue: root.session ? root.session.cardFontBodyPath : ""
                                            onPicked: function(value) { if (root.session) root.session.cardFontBodyPath = value }

                                            IconButton {
                                                iconSource: Qt.resolvedUrl("icons/folder-open.svg")
                                                tooltip: qsTrId("card_font.import")
                                                onClicked: if (root.session) root.session.importCardBodyFont()
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

    // 管理已保存的预设：保存当前、改名、删除都要输入名字，菜单里放不下。
    // 应用预设（含内置版式）走「布局 ▾ → 预设」二级菜单。
    AppDialog {
        id: presetDialog
        objectName: "coverPresetDialog"
        title: qsTrId("cover.presets")
        preferredWidth: 480
        preferredHeight: Theme.dialogHeight
        footer: DialogFooter {
            acceptText: qsTrId("action.close")
            acceptEmphasized: false
            onAccepted: presetDialog.close()
        }

        body: ColumnLayout {
            spacing: 10

            RowLayout {
                Layout.fillWidth: true
                AppTextField {
                    id: presetName
                    objectName: "coverPresetNameField"
                    Layout.fillWidth: true
                    placeholderText: qsTrId("cover.preset_name")
                }
                AppButton {
                    text: qsTrId("cover.save_preset")
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
                text: qsTrId("cover.no_presets")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
            }

            Repeater {
                model: root.session ? root.session.presets : []
                delegate: RowLayout {
                    id: presetRow
                    required property var modelData
                    property bool renaming: false
                    property string editingName: modelData.name
                    Layout.fillWidth: true
                    spacing: 4
                    AppTextField {
                        id: presetNameEdit
                        Layout.fillWidth: true
                        visible: presetRow.renaming
                        text: presetRow.editingName
                        onTextChanged: {
                            if (activeFocus)
                                presetRow.editingName = text
                        }
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: !presetRow.renaming
                        text: presetRow.modelData.name
                        color: Theme.colors.text.secondary
                        font.family: Theme.uiFont
                        font.pixelSize: Theme.uiFontSize
                        elide: Text.ElideRight
                    }
                    AppButton {
                        visible: !presetRow.renaming
                        text: qsTrId("cover.apply_preset")
                        onClicked: root.session.applyPreset(presetRow.modelData.name)
                    }
                    AppButton {
                        visible: !presetRow.renaming
                        text: qsTrId("cover.rename_preset")
                        onClicked: {
                            presetRow.editingName = presetRow.modelData.name
                            presetRow.renaming = true
                        }
                    }
                    AppButton {
                        visible: !presetRow.renaming
                        text: qsTrId("cover.delete_preset")
                        onClicked: root.session.removePreset(presetRow.modelData.name)
                    }
                    AppButton {
                        visible: presetRow.renaming
                        text: qsTrId("cover.rename_preset")
                        enabled: presetNameEdit.text.trim().length > 0
                        onClicked: {
                            root.session.renamePreset(presetRow.modelData.name,
                                                       presetRow.editingName)
                            presetRow.renaming = false
                        }
                    }
                }
            }
        }
    }
}
