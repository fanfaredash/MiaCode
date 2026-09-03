import QtQuick
import QtQuick.Controls
import MiaCode.UI

// 粘性菜单基类（Popup，点外部/按 Esc 关闭，聚焦但不会因点条目即关）。
// 背景统一走 FloatingCard（无描边 + 微小阴影）。波形亮度、渲染模式菜单等
// 粘性操作弹窗都继承这里。
AppDropdownPanel {
    id: root

    property real minimumWidth: 0
    property bool openAbove: false

    function openAt(anchor) {
        parent = anchor
        open()
    }

    margins: 0
    x: parent ? parent.width - width : 0
    y: root.openAbove ? -height : (parent ? parent.height : 0)
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

    background: FloatingCard { implicitWidth: root.minimumWidth }
}
