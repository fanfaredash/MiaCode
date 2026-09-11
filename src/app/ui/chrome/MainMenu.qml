pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

// Custom menubar
// Overflow removes complete top-level entries (right → left)

Item {
    id: root

    required property var commands
    required property var shortcuts
    // Source of the 调整 menu's operation rows; see chartTransformMenu().
    required property var documentSession
    property bool commandsEnabled: true
    property bool normalizationEnabled: true
    // Re-read each time the menu opens rather than kept live: the list only
    // changes when a document is opened, and a menu nobody is looking at has no
    // reason to hold a copy.
    property var recentDocuments: []
    property var backupDocuments: []
    property real availableWidth: Number.POSITIVE_INFINITY

    readonly property int overflowButtonWidth: 30
    readonly property real fullWidth: fileButton.implicitWidth + editButton.implicitWidth
        + adjustButton.implicitWidth + toolsButton.implicitWidth + previewButton.implicitWidth
    property int visibleCount: 5
    property bool layoutReady: false
    property var _activeMenu: null

    implicitHeight: 34
    implicitWidth: fullWidth
    height: parent ? parent.height : implicitHeight
    // Shrink-wrap to whole visible controls
    width: barRow.width
    enabled: commandsEnabled

    onAvailableWidthChanged: root.reflow()
    Component.onCompleted: {
        root.layoutReady = true
        root.reflow()
    }

    function topButtons() {
        return [fileButton, editButton, adjustButton, toolsButton, previewButton]
    }

    function topMenus() {
        return [fileMenu, editMenu, adjustMenu, toolsMenu, previewMenu]
    }

    function closeActiveMenu() {
        if (root._activeMenu && root._activeMenu.visible)
            root._activeMenu.close()
        root._activeMenu = null
    }

    function toggleAnchoredMenu(menu, anchor) {
        if (root._activeMenu === menu)
            root.closeActiveMenu()
        else
            root.openAnchoredMenu(menu, anchor)
    }

    function hoverAnchoredMenu(menu, anchor) {
        if (root._activeMenu && root._activeMenu !== menu)
            root.openAnchoredMenu(menu, anchor)
    }

    function openAnchoredMenu(menu, anchor) {
        if (!menu || !anchor)
            return
        const previous = root._activeMenu
        if (previous === menu)
            return
        // Adjacent entries switch immediately; opening and closing the menu
        // session use the regular fade transitions.
        if (previous) {
            previous.exit.enabled = false
            previous.close()
            previous.exit.enabled = true
            menu.enter.enabled = false
            menu.opacity = 1
        }
        root._activeMenu = menu
        menu.closePolicy = Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent
        menu.popup(anchor, 0, anchor.height)
        menu.enter.enabled = true
    }

    Connections {
        target: root._activeMenu
        function onAboutToHide() { root._activeMenu = null }
    }

    function plainTitle(text) {
        let out = ""
        for (let i = 0; i < text.length; ++i) {
            if (text[i] === "&" && i + 1 < text.length) {
                ++i
                out += text[i]
                continue
            }
            out += text[i]
        }
        return out
    }

    function detachAllOverflowMenus() {
        // Pull every submenu back out so top-level popup still owns them.
        for (let i = overflowMenu.count - 1; i >= 0; --i) {
            if (overflowMenu.menuAt(i))
                overflowMenu.takeMenu(i)
        }
        const menus = root.topMenus()
        for (let i = 0; i < menus.length; ++i)
            menus[i].parent = menuHost
    }

    function reflow() {
        if (!root.layoutReady)
            return

        const buttons = root.topButtons()
        const menus = root.topMenus()
        const avail = root.availableWidth
        let n = buttons.length

        while (n > 0) {
            let total = 0
            for (let i = 0; i < n; ++i)
                total += buttons[i].implicitWidth
            if (n < buttons.length)
                total += root.overflowButtonWidth
            if (total <= avail + 0.5)
                break
            --n
        }

        // 尺寸变化当下更新折叠状态，原生窗口动画期间也使用当前宽度。
        // 跨过折叠阈值时才迁移菜单，保留同一布局内的活动菜单。
        if (root.visibleCount === n)
            return
        root.closeActiveMenu()
        root.detachAllOverflowMenus()
        root.visibleCount = n

        // Register overflowed entries as real Menu submenus (parent= alone does nothing).
        for (let i = n; i < menus.length; ++i)
            overflowMenu.addMenu(menus[i])
    }

    // Top-level entry: real control width drives overflow math.
    component TopLevelItem: ChromeRow {
        id: btn

        required property var menu
        required property int menuIndex
        stateColors: Theme.colors.activityState

        y: (root.height - height) / 2
        height: Theme.controlMinHeight
        padding: 0
        leftPadding: 8
        rightPadding: 8
        topPadding: 0
        bottomPadding: 0
        visible: root.visibleCount > btn.menuIndex
        focusPolicy: Qt.NoFocus
        selected: btn.menuOpen

        readonly property bool menuOpen: btn.menu.active

        implicitWidth: Math.ceil(label.implicitWidth) + leftPadding + rightPadding
        onImplicitWidthChanged: root.reflow()

        contentItem: ControlsImpl.MnemonicLabel {
            id: label
            text: btn.menu ? btn.menu.title : ""
            mnemonicVisible: true
            color: (btn.hovered || btn.menuOpen) ? Theme.colors.text.active
                                                 : Theme.colors.text.chrome
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }


        onClicked: root.toggleAnchoredMenu(btn.menu, btn)

        // Menubar-style: while a menu is open, hovering another top item switches it.
        onHoveredChanged: {
            if (!btn.hovered || !btn.visible || !btn.menu)
                return
            root.hoverAnchoredMenu(btn.menu, btn)
        }
    }

    Row {
        id: barRow
        spacing: 0
        height: parent.height

        TopLevelItem {
            id: fileButton
            menu: fileMenu
            menuIndex: 0
        }
        TopLevelItem {
            id: editButton
            menu: editMenu
            menuIndex: 1
        }
        TopLevelItem {
            id: adjustButton
            menu: adjustMenu
            menuIndex: 2
        }
        TopLevelItem {
            id: toolsButton
            menu: toolsMenu
            menuIndex: 3
        }
        TopLevelItem {
            id: previewButton
            menu: previewMenu
            menuIndex: 4
        }
        IconButton {
            id: moreButton
            stateColors: Theme.colors.activityState
            width: root.overflowButtonWidth
            y: (root.height - height) / 2
            height: Theme.controlMinHeight
            visible: root.visibleCount < 5
            iconSource: Qt.resolvedUrl("icons/more.svg")
            iconWidth: 16
            iconHeight: 16
            tooltip: qsTrId("qml.more")
            active: overflowMenu.active
            onClicked: root.toggleAnchoredMenu(overflowMenu, moreButton)
            onHoveredChanged: {
                if (hovered)
                    root.hoverAnchoredMenu(overflowMenu, moreButton)
            }
        }
    }

    // Host for top-level menus while their button is visible.
    Item {
        id: menuHost
        width: 0
        height: 0

        AppMenu {
            id: fileMenu
            title: qsTrId("menu.file")
            // 新建 / 打开 keep v1's Ctrl+Shift+N / Ctrl+Shift+O rather than the
            // platform standard keys. StandardKey.New and StandardKey.Open are
            // Ctrl+N and Ctrl+O, which the registry already hands to
            // transform.toggle_ex and preview.speed_down; two window-context
            // shortcuts on one sequence are ambiguous to Qt, which then fires
            // NEITHER. qml_shortcut_binding_spec guards the whole class.
            AppMenuAction {
                text: qsTrId("action.new")
                shortcut: root.shortcuts.revision >= 0
                    ? root.shortcuts.sequence("file.new", "Ctrl+Shift+N")
                    : ""
                shortcutText: root.shortcuts.displayText("file.new", "Ctrl+Shift+N")
                enabled: root.commandsEnabled
                onTriggered: root.commands.newDocumentRequested()
            }
            AppMenuAction {
                text: qsTrId("action.open")
                shortcut: root.shortcuts.revision >= 0
                    ? root.shortcuts.sequence("file.open", "Ctrl+Shift+O")
                    : ""
                shortcutText: root.shortcuts.displayText("file.open", "Ctrl+Shift+O")
                enabled: root.commandsEnabled
                onTriggered: root.commands.openRequested()
            }
            AppMenu {
                id: recentMenu
                title: qsTrId("cover.open_recent")
                enabled: root.commandsEnabled
                onAboutToShow: root.recentDocuments = root.documentSession.recentDocuments()

                // The empty-state row is a model entry, not a hidden sibling.
                // A Menu lays out its statically declared children before a
                // Repeater's, so a placeholder that merely set visible:false
                // still held a row — at the TOP of the list, above the first
                // real chart.
                Repeater {
                    model: root.recentDocuments.length > 0
                           ? root.recentDocuments
                           : [{ label: qsTrId("qml.no_recent_documents"), path: "" }]
                    delegate: AppMenuItem {
                        required property var modelData
                        // The chart's folder name, not its path: every path here
                        // shares a long prefix and ends in the same file name, so
                        // the full one is both unreadable and too wide for a menu.
                        text: modelData.label
                        tooltip: modelData.path
                        enabled: modelData.path.length > 0
                        onTriggered: {
                            if (modelData.path.length > 0)
                                root.commands.openRecentRequested(modelData.path)
                        }
                    }
                }
            }
            AppMenu {
                id: restoreBackupMenu
                title: qsTrId("qml.restore_backup")
                enabled: root.commandsEnabled
                onAboutToShow: root.backupDocuments = root.documentSession.backupDocuments()

                contentItem: ListView {
                    readonly property real averageItemHeight:
                        count > 0 ? contentHeight / count : 0
                    implicitHeight: Math.min(contentHeight, averageItemHeight * 10)
                    model: restoreBackupMenu.contentModel
                    delegate: restoreBackupMenu.delegate
                    clip: true
                    interactive: contentHeight > height
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: AppScrollBar {}
                }

                Repeater {
                    model: root.backupDocuments.length > 0
                           ? root.backupDocuments
                           : [{ label: qsTrId("qml.no_backups"), path: "" }]
                    delegate: AppMenuItem {
                        required property var modelData
                        text: modelData.label
                        tooltip: modelData.path
                        enabled: modelData.path.length > 0
                        onTriggered: {
                            if (modelData.path.length > 0)
                                root.commands.restoreBackupRequested(modelData.path)
                        }
                    }
                }
            }
            AppMenuAction {
                text: qsTrId("qml.close_document")
                enabled: root.commandsEnabled
                onTriggered: root.commands.closeDocumentRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: qsTrId("action.save")
                shortcut: StandardKey.Save
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Save)
                enabled: root.commandsEnabled
                onTriggered: root.commands.saveRequested()
            }
            AppMenuAction {
                text: qsTrId("qml.save_entire_document")
                enabled: root.commandsEnabled
                onTriggered: root.commands.saveWholeDocumentRequested()
            }
            AppMenuAction {
                text: qsTrId("action.save_as")
                shortcut: StandardKey.SaveAs
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.SaveAs)
                enabled: root.commandsEnabled
                onTriggered: root.commands.saveAsRequested()
            }
        }

        AppMenu {
            id: editMenu
            title: qsTrId("metadata.edit_e")
            AppMenuAction {
                text: qsTrId("qml.undo")
                shortcut: StandardKey.Undo
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Undo)
                enabled: root.commandsEnabled && root.commands.canUndo
                onTriggered: root.commands.undoRequested()
            }
            AppMenuAction {
                text: qsTrId("action.redo")
                shortcut: StandardKey.Redo
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Redo)
                enabled: root.commandsEnabled && root.commands.canRedo
                onTriggered: root.commands.redoRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: qsTrId("action.cut")
                shortcut: StandardKey.Cut
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Cut)
                enabled: root.commandsEnabled && root.commands.canCut
                onTriggered: root.commands.cutRequested()
            }
            AppMenuAction {
                text: qsTrId("action.copy")
                shortcut: StandardKey.Copy
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Copy)
                enabled: root.commandsEnabled && root.commands.canCopy
                onTriggered: root.commands.copyRequested()
            }
            AppMenuAction {
                text: qsTrId("action.paste")
                shortcut: StandardKey.Paste
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Paste)
                enabled: root.commandsEnabled && root.commands.canPaste
                onTriggered: root.commands.pasteRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: qsTrId("metadata.find")
                shortcut: StandardKey.Find
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Find)
                enabled: root.commandsEnabled
                onTriggered: root.commands.findRequested()
            }
            AppMenuAction {
                text: qsTrId("net.select_all")
                shortcut: StandardKey.SelectAll
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.SelectAll)
                enabled: root.commandsEnabled
                onTriggered: root.commands.selectAllRequested()
            }
            AppMenuAction {
                text: qsTrId("qml.select_current_line")
                enabled: root.commandsEnabled
                onTriggered: root.commands.selectCurrentLineRequested()
            }
        }

        AppMenu {
            id: toolsMenu
            title: qsTrId("menu.tools")
            AppMenuAction {
                text: qsTrId("dialog.unsaved_field_changes.field.metadata")
                enabled: root.commandsEnabled
                onTriggered: root.commands.metadataRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: qsTrId("qml.latency_calibration")
                enabled: root.commandsEnabled
                onTriggered: root.commands.latencyCalibrationRequested()
            }
            AppMenuAction {
                text: qsTrId("media_tools.audio_video_processing")
                enabled: root.commandsEnabled
                onTriggered: root.commands.mediaToolsRequested()
            }
            AppMenuAction {
                text: qsTrId("qml.normalize_whole_chart")
                enabled: root.commandsEnabled && root.normalizationEnabled
                onTriggered: root.commands.normalizeChartRequested()
            }
        }

        AppMenu {
            id: adjustMenu
            objectName: "adjustMenu"
            title: qsTrId("menu.transform")

            // Rows, labels and grouping come from the shared transform table,
            // so this menu cannot drift from the shortcut editor or the
            // editor's context menu.
            readonly property var transformRows: root.documentSession.chartTransformMenu()

            Repeater {
                model: adjustMenu.transformRows.filter(row => row.section === 0)
                delegate: AppMenuItem {
                    required property var modelData
                    objectName: "adjustTransform_" + modelData.id
                    text: modelData.label
                    shortcutText: root.shortcuts.displayText(modelData.id)
                    enabled: root.commandsEnabled
                    onTriggered: root.commands.chartTransformRequested(modelData.id)
                }
            }
            AppMenuSeparator {}
            Repeater {
                model: adjustMenu.transformRows.filter(row => row.section === 1)
                delegate: AppMenuItem {
                    required property var modelData
                    objectName: "adjustTransform_" + modelData.id
                    text: modelData.label
                    shortcutText: root.shortcuts.displayText(modelData.id)
                    enabled: root.commandsEnabled
                    onTriggered: root.commands.chartTransformRequested(modelData.id)
                }
            }
            AppMenuSeparator {}
            Repeater {
                model: adjustMenu.transformRows.filter(row => row.section === 2)
                delegate: AppMenuItem {
                    required property var modelData
                    objectName: "adjustTransform_" + modelData.id
                    text: modelData.label
                    shortcutText: root.shortcuts.displayText(modelData.id)
                    enabled: root.commandsEnabled
                    onTriggered: root.commands.chartTransformRequested(modelData.id)
                }
            }
            AppMenuAction {
                text: qsTrId("qml.normalize_whole_chart")
                enabled: root.commandsEnabled && root.normalizationEnabled
                onTriggered: root.commands.normalizeChartRequested()
            }

            AppMenu {
                id: adjustMoreMenu
                objectName: "adjustMoreMenu"
                title: root.documentSession.chartTransformMoreLabel()
                Repeater {
                    model: adjustMenu.transformRows.filter(row => row.section === 3)
                    delegate: AppMenuItem {
                        required property var modelData
                        objectName: "adjustTransform_" + modelData.id
                        text: modelData.label
                        shortcutText: root.shortcuts.displayText(modelData.id)
                        enabled: root.commandsEnabled
                        onTriggered: root.commands.chartTransformRequested(modelData.id)
                    }
                }
            }

        }

        AppMenu {
            id: previewMenu
            title: qsTrId("metadata.preview_p")
            // Display-only spellings: ShortcutBindings.qml owns these two
            // bindings, and a `shortcut:` here would be the second claim on the
            // same sequence — the very ambiguity that killed them in the File
            // menu. The rows exist so the binding is discoverable, as in v1.
            AppMenuAction {
                text: qsTrId("action.preview_speed_down")
                shortcutText: root.shortcuts.displayText("preview.speed_down", "Ctrl+O")
                enabled: root.commandsEnabled
                onTriggered: root.commands.previewRateStepRequested(-1)
            }
            AppMenuAction {
                text: qsTrId("action.preview_speed_up")
                shortcutText: root.shortcuts.displayText("preview.speed_up", "Ctrl+P")
                enabled: root.commandsEnabled
                onTriggered: root.commands.previewRateStepRequested(1)
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: qsTrId("action.audio_settings")
                enabled: root.commandsEnabled
                onTriggered: root.commands.audioSettingsRequested()
            }
            AppMenuAction {
                text: qsTrId("action.video_settings")
                enabled: root.commandsEnabled
                onTriggered: root.commands.previewSettingsRequested()
            }
        }

    }

    // Overflowed top-level AppMenus are inserted via addMenu() as submenus.
    AppMenu {
        id: overflowMenu
    }
}
