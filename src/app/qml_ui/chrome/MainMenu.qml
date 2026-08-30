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
    // Re-read each time the menu opens rather than kept live: the list only
    // changes when a document is opened, and a menu nobody is looking at has no
    // reason to hold a copy.
    property var recentDocuments: []
    property var backupDocuments: []
    property real availableWidth: Number.POSITIVE_INFINITY

    readonly property int overflowButtonWidth: 30
    property int visibleCount: 6
    property var _activeMenu: null

    implicitHeight: 34
    height: parent ? parent.height : implicitHeight
    // Shrink-wrap to whole visible controls
    width: barRow.width
    enabled: commandsEnabled

    onAvailableWidthChanged: root.scheduleReflow()
    Component.onCompleted: root.scheduleReflow()

    function scheduleReflow() {
        Qt.callLater(root.reflow)
    }

    function topButtons() {
        return [fileButton, editButton, toolsButton, adjustButton, previewButton, helpButton]
    }

    function topMenus() {
        return [fileMenu, editMenu, toolsMenu, adjustMenu, previewMenu, helpMenu]
    }

    function closeActiveMenu() {
        if (root._activeMenu && root._activeMenu.visible)
            root._activeMenu.close()
        root._activeMenu = null
    }

    function openAnchoredMenu(menu, anchor) {
        if (!menu || !anchor)
            return
        if (root._activeMenu && root._activeMenu !== menu && root._activeMenu.visible)
            root._activeMenu.close()
        root._activeMenu = menu
        menu.popup(anchor, 0, anchor.height)
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
        root.closeActiveMenu()
        root.detachAllOverflowMenus()

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

        height: root.height
        padding: 0
        leftPadding: 8
        rightPadding: 8
        topPadding: 0
        bottomPadding: 0
        visible: root.visibleCount > btn.menuIndex
        focusPolicy: Qt.NoFocus
        tone: "bar"
        selected: btn.menuOpen

        readonly property bool menuOpen: btn.menu && btn.menu.visible

        implicitWidth: Math.ceil(label.implicitWidth) + leftPadding + rightPadding

        contentItem: ControlsImpl.MnemonicLabel {
            id: label
            text: btn.menu ? btn.menu.title : ""
            mnemonicVisible: true
            color: (btn.hovered || btn.menuOpen) ? Theme.colors.text.active
                                                 : Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }


        onClicked: root.openAnchoredMenu(btn.menu, btn)

        // Menubar-style: while a menu is open, hovering another top item switches it.
        onHoveredChanged: {
            if (!btn.hovered || !btn.visible || !btn.menu)
                return
            if (root._activeMenu && root._activeMenu.visible && root._activeMenu !== btn.menu)
                root.openAnchoredMenu(btn.menu, btn)
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
            id: toolsButton
            menu: toolsMenu
            menuIndex: 2
        }
        TopLevelItem {
            id: adjustButton
            menu: adjustMenu
            menuIndex: 3
        }
        TopLevelItem {
            id: previewButton
            menu: previewMenu
            menuIndex: 4
        }
        TopLevelItem {
            id: helpButton
            menu: helpMenu
            menuIndex: 5
        }

        IconButton {
            id: moreButton
            width: root.overflowButtonWidth
            height: root.height
            visible: root.visibleCount < 6
            iconSource: Qt.resolvedUrl("icons/more.svg")
            iconWidth: 16
            iconHeight: 16
            tooltip: qsTr("更多")
            onClicked: root.openAnchoredMenu(overflowMenu, moreButton)
        }
    }

    // Host for top-level menus while their button is visible.
    Item {
        id: menuHost
        width: 0
        height: 0

        AppMenu {
            id: fileMenu
            title: qsTr("文件(&F)")
            AppMenuAction {
                text: qsTr("新建")
                shortcut: StandardKey.New
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.New)
                enabled: root.commandsEnabled
                onTriggered: root.commands.newDocumentRequested()
            }
            AppMenuAction {
                text: qsTr("打开")
                shortcut: StandardKey.Open
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Open)
                enabled: root.commandsEnabled
                onTriggered: root.commands.openRequested()
            }
            AppMenu {
                id: recentMenu
                title: qsTr("打开最近")
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
                           : [{ label: qsTr("暂无最近文档"), path: "" }]
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
                title: qsTr("恢复备份")
                enabled: root.commandsEnabled
                onAboutToShow: root.backupDocuments = root.documentSession.backupDocuments()

                Repeater {
                    model: root.backupDocuments.length > 0
                           ? root.backupDocuments
                           : [{ label: qsTr("暂无备份"), path: "" }]
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
                text: qsTr("关闭文档")
                enabled: root.commandsEnabled
                onTriggered: root.commands.closeDocumentRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: qsTr("保存")
                shortcut: StandardKey.Save
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Save)
                enabled: root.commandsEnabled
                onTriggered: root.commands.saveRequested()
            }
            AppMenuAction {
                text: qsTr("另存为")
                shortcut: StandardKey.SaveAs
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.SaveAs)
                enabled: root.commandsEnabled
                onTriggered: root.commands.saveAsRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: qsTr("退出")
                shortcut: StandardKey.Quit
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Quit)
                enabled: root.commandsEnabled
                onTriggered: root.commands.exitRequested()
            }
        }

        AppMenu {
            id: editMenu
            title: qsTr("编辑(&E)")
            AppMenuAction {
                text: qsTr("撤销")
                shortcut: StandardKey.Undo
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Undo)
                enabled: root.commandsEnabled && root.commands.canUndo
                onTriggered: root.commands.undoRequested()
            }
            AppMenuAction {
                text: qsTr("重做")
                shortcut: StandardKey.Redo
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Redo)
                enabled: root.commandsEnabled && root.commands.canRedo
                onTriggered: root.commands.redoRequested()
            }
            AppMenuAction {
                text: qsTr("查找")
                shortcut: StandardKey.Find
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.Find)
                enabled: root.commandsEnabled
                onTriggered: root.commands.findRequested()
            }
            AppMenuSeparator {}
            AppMenuAction {
                text: qsTr("全选")
                shortcut: StandardKey.SelectAll
                shortcutText: root.shortcuts.standardDisplayText(StandardKey.SelectAll)
                enabled: root.commandsEnabled
                onTriggered: root.commands.selectAllRequested()
            }
            AppMenuAction {
                text: qsTr("选择当前行")
                enabled: root.commandsEnabled
                onTriggered: root.commands.selectCurrentLineRequested()
            }
        }

        AppMenu {
            id: toolsMenu
            title: qsTr("工具(&T)")
            AppMenuAction {
                text: qsTr("元数据")
                enabled: root.commandsEnabled
                onTriggered: root.commands.metadataRequested()
            }
            AppMenuAction {
                text: qsTr("检查谱面")
                enabled: root.commandsEnabled
                onTriggered: root.commands.validateRequested()
            }
        }

        AppMenu {
            id: adjustMenu
            objectName: "adjustMenu"
            title: qsTr("调整(&M)")

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
                text: qsTr("整谱规范化")
                enabled: root.commandsEnabled
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

            AppMenuSeparator {}
            AppMenuAction {
                text: qsTr("切换侧栏")
                shortcut: root.shortcuts.sequence("view.toggle_sidebar", "Ctrl+B")
                shortcutText: root.shortcuts.displayText("view.toggle_sidebar", "Ctrl+B")
                enabled: root.commandsEnabled
                onTriggered: root.commands.toggleSidebarRequested()
            }
            AppMenuAction {
                text: qsTr("切换时间轴")
                enabled: root.commandsEnabled
                onTriggered: root.commands.toggleBottomPanelRequested()
            }
        }

        AppMenu {
            id: previewMenu
            title: qsTr("预览(&P)")
            AppMenuAction {
                text: qsTr("音频设置")
                enabled: root.commandsEnabled
                onTriggered: root.commands.audioSettingsRequested()
            }
            AppMenuAction {
                text: qsTr("预览设置")
                enabled: root.commandsEnabled
                onTriggered: root.commands.previewSettingsRequested()
            }
        }

        AppMenu {
            id: helpMenu
            title: qsTr("帮助(&H)")
            AppMenuAction {
                text: qsTr("关于 MiaCode")
                enabled: root.commandsEnabled
                onTriggered: root.commands.aboutRequested()
            }
        }
    }

    // Overflowed top-level AppMenus are inserted via addMenu() as submenus.
    AppMenu {
        id: overflowMenu
    }
}
