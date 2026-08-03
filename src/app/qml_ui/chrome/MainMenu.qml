pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import MiaCode.UI

MenuBar {
    id: root

    signal toggleSidebarRequested()
    signal toggleBottomPanelRequested()
    signal togglePreviewRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()
    signal exitRequested()
    signal undoRequested()
    signal redoRequested()
    signal selectAllRequested()
    signal validateRequested()
    signal metadataRequested()

    property bool canUndo: false
    property bool canRedo: false
    property bool commandsEnabled: true

    enabled: commandsEnabled

    topPadding: 0
    leftPadding: 0
    rightPadding: 0
    bottomPadding: 0

    delegate: MenuBarItem {
        id: menuItem
        height: root.height
        leftPadding: 8
        rightPadding: 8
        topPadding: 0
        bottomPadding: 0
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        palette.highlight: Theme.colors.state.lineHighlight
        palette.highlightedText: Theme.colors.text.primary

        // 一级菜单标题保留 Qt 的 & 助记标记。该标签负责隐藏 &、绘制
        // 下划线；MenuBarItem 同时据此注册对应的 Alt 组合键。
        contentItem: ControlsImpl.MnemonicLabel {
            text: menuItem.text
            mnemonicVisible: true
            color: menuItem.highlighted ? Theme.colors.text.primary : Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    background: Rectangle {
        color: "transparent"
    }

    Menu {
        title: qsTr("文件(&F)")
        Action {
            text: qsTr("打开")
            shortcut: StandardKey.Open
            enabled: root.commandsEnabled
            onTriggered: root.openRequested()
        }
        Action {
            text: qsTr("保存")
            shortcut: StandardKey.Save
            enabled: root.commandsEnabled
            onTriggered: root.saveRequested()
        }
        Action {
            text: qsTr("另存为")
            shortcut: StandardKey.SaveAs
            enabled: root.commandsEnabled
            onTriggered: root.saveAsRequested()
        }
        MenuSeparator {}
        Action {
            text: qsTr("退出")
            shortcut: StandardKey.Quit
            enabled: root.commandsEnabled
            onTriggered: root.exitRequested()
        }
    }

    Menu {
        title: qsTr("编辑(&E)")
        Action {
            text: qsTr("撤销")
            shortcut: StandardKey.Undo
            enabled: root.commandsEnabled && root.canUndo
            onTriggered: root.undoRequested()
        }
        Action {
            text: qsTr("重做")
            shortcut: StandardKey.Redo
            enabled: root.commandsEnabled && root.canRedo
            onTriggered: root.redoRequested()
        }
        Action { text: qsTr("查找"); enabled: false }
        MenuSeparator {}
        Action {
            text: qsTr("全选")
            shortcut: StandardKey.SelectAll
            enabled: root.commandsEnabled
            onTriggered: root.selectAllRequested()
        }
        Action { text: qsTr("选择当前行"); enabled: false }
    }

    Menu {
        title: qsTr("工具(&T)")
        Action {
            text: qsTr("元数据")
            enabled: root.commandsEnabled
            onTriggered: root.metadataRequested()
        }
        Action {
            text: qsTr("检查谱面")
            enabled: root.commandsEnabled
            onTriggered: root.validateRequested()
        }
    }

    Menu {
        title: qsTr("调整(&M)")
        Action {
            text: qsTr("切换侧栏")
            shortcut: "Ctrl+B"
            enabled: root.commandsEnabled
            onTriggered: root.toggleSidebarRequested()
        }
        Action {
            text: qsTr("切换时间轴")
            enabled: root.commandsEnabled
            onTriggered: root.toggleBottomPanelRequested()
        }
    }

    Menu {
        title: qsTr("预览(&P)")
        Action {
            text: qsTr("切换实时预览")
            enabled: root.commandsEnabled
            onTriggered: root.togglePreviewRequested()
        }
    }

    Menu {
        title: qsTr("帮助(&H)")
        Action { text: qsTr("关于 MiaCode"); enabled: false }
    }
}
