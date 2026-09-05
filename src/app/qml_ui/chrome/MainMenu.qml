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
    property int visibleCount: 5
    property bool layoutReady: false
    property var _activeMenu: null

    implicitHeight: 34
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
            tooltip: UiText.text("更多")
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
            title: UiText.text("文件(&F)")
            AppMenuAction {
                text: UiText.text("新建")
                shortcut: StandardKey.New
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.New)
                enabled: root.commandsEnabled
                onTriggered: root.commands.newDocumentRequested()
            }
            AppMenuAction {
                text: UiText.text("打开")
                shortcut: StandardKey.Open
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Open)
                enabled: root.commandsEnabled
                onTriggered: root.commands.openRequested()
            }
            AppMenu {
                id: recentMenu
                title: UiText.text("打开最近")
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
                           : [{ label: UiText.text("暂无最近文档"), path: "" }]
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
                title: UiText.text("恢复备份")
                enabled: root.commandsEnabled
                onAboutToShow: root.backupDocuments = root.documentSession.backupDocuments()

                Repeater {
                    model: root.backupDocuments.length > 0
                           ? root.backupDocuments
                           : [{ label: UiText.text("暂无备份"), path: "" }]
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
                text: UiText.text("关闭文档")
                enabled: root.commandsEnabled
                onTriggered: root.commands.closeDocumentRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: UiText.text("保存")
                shortcut: StandardKey.Save
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Save)
                enabled: root.commandsEnabled
                onTriggered: root.commands.saveRequested()
            }
            AppMenuAction {
                text: UiText.text("另存为")
                shortcut: StandardKey.SaveAs
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.SaveAs)
                enabled: root.commandsEnabled
                onTriggered: root.commands.saveAsRequested()
            }
        }

        AppMenu {
            id: editMenu
            title: UiText.text("编辑(&E)")
            AppMenuAction {
                text: UiText.text("撤销")
                shortcut: StandardKey.Undo
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Undo)
                enabled: root.commandsEnabled && root.commands.canUndo
                onTriggered: root.commands.undoRequested()
            }
            AppMenuAction {
                text: UiText.text("重做")
                shortcut: StandardKey.Redo
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Redo)
                enabled: root.commandsEnabled && root.commands.canRedo
                onTriggered: root.commands.redoRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: UiText.text("剪切")
                shortcut: StandardKey.Cut
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Cut)
                enabled: root.commandsEnabled && root.commands.canCut
                onTriggered: root.commands.cutRequested()
            }
            AppMenuAction {
                text: UiText.text("复制")
                shortcut: StandardKey.Copy
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Copy)
                enabled: root.commandsEnabled && root.commands.canCopy
                onTriggered: root.commands.copyRequested()
            }
            AppMenuAction {
                text: UiText.text("粘贴")
                shortcut: StandardKey.Paste
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Paste)
                enabled: root.commandsEnabled && root.commands.canPaste
                onTriggered: root.commands.pasteRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: UiText.text("查找")
                shortcut: StandardKey.Find
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Find)
                enabled: root.commandsEnabled
                onTriggered: root.commands.findRequested()
            }
            AppMenuAction {
                text: UiText.text("全选")
                shortcut: StandardKey.SelectAll
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.SelectAll)
                enabled: root.commandsEnabled
                onTriggered: root.commands.selectAllRequested()
            }
            AppMenuAction {
                text: UiText.text("选择当前行")
                enabled: root.commandsEnabled
                onTriggered: root.commands.selectCurrentLineRequested()
            }
        }

        AppMenu {
            id: toolsMenu
            title: UiText.text("工具(&T)")
            AppMenuAction {
                text: UiText.text("dialog.unsaved_field_changes.field.metadata")
                enabled: root.commandsEnabled
                onTriggered: root.commands.metadataRequested()
            }
            AppMenuAction {
                text: UiText.text("检查谱面")
                enabled: root.commandsEnabled
                onTriggered: root.commands.validateRequested()
            }
        }

        AppMenu {
            id: adjustMenu
            objectName: "adjustMenu"
            title: UiText.text("调整(&M)")

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
                text: UiText.text("整谱规范化")
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
            title: UiText.text("预览(&P)")
            AppMenuAction {
                text: UiText.text("音频设置")
                enabled: root.commandsEnabled
                onTriggered: root.commands.audioSettingsRequested()
            }
            AppMenuAction {
                text: UiText.text("预览设置")
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
