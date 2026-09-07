import QtQuick
import QtQuick.Controls
import MiaCode.UI

// 下拉面板基类（Popup 列表弹层）。补全弹层、下拉框弹窗等"自上而下的列表面板"
// 都继承这里，背景统一走 FloatingCard 的磨砂、描边与阴影。
// 各子类自行配置 closePolicy / padding / contentItem。
Popup {
    id: root
    popupType: Popup.Item

    property bool closing: false
    readonly property bool active: root.visible && !root.closing
    enter: FadeTransition {
        id: enterTransition
    }
    exit: FadeTransition {
        appearing: false
        initialOpacity: root.opacity
    }
    onAboutToShow: {
        enterTransition.initialOpacity = root.closing ? root.opacity : 0
        root.closing = false
    }
    onAboutToHide: root.closing = true
    onClosed: root.closing = false

    padding: Theme.menuPadding
    background: FloatingCard { popup: root }
}
