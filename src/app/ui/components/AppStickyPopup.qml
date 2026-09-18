import QtQuick
import QtQuick.Controls
import MiaCode.UI

// 粘性菜单基类（Popup，点外部/按 Esc 关闭，聚焦但不会因点条目即关）。
// 背景统一走 FloatingCard 的磨砂、描边与阴影。波形亮度、渲染模式菜单等
// 粘性操作弹窗都继承这里。
//
// 宽度只取行自己的 implicitWidth。分组 Column（无理参数）再加一层 padding。
// 不钻进按钮/开关/滑条内部，否则 padding 会叠上去把面板撑飞。
// 隐藏行仍计入，切换常规/无理时宽度不变。
AppDropdownPanel {
    id: root

    property real minimumWidth: 0
    property bool openAbove: false
    property Item anchorItem: null

    function openAt(anchor) {
        parent = anchor
        root.anchorItem = anchor
        open()
    }

    function widestChild(item) {
        if (!item)
            return 0
        let widest = 0
        const kids = item.children
        for (let i = 0; i < kids.length; ++i) {
            const child = kids[i]
            if (child instanceof Column) {
                const pad = Number(child.leftPadding || 0) + Number(child.rightPadding || 0)
                widest = Math.max(widest, widestChild(child) + pad)
                continue
            }
            widest = Math.max(widest, Number(child.implicitWidth) || 0)
        }
        return widest
    }

    implicitWidth: Math.ceil(root.leftPadding + root.rightPadding
                             + root.widestChild(root.contentItem))
    width: {
        const hug = Math.max(root.implicitWidth, root.minimumWidth)
        const overlay = Overlay.overlay
        return (overlay && overlay.width > 0) ? Math.min(hug, overlay.width) : hug
    }

    margins: 0
    x: parent ? parent.width - width : 0
    y: root.openAbove ? -height : (parent ? parent.height : 0)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: FloatingCard {
        popup: root
    }
}
