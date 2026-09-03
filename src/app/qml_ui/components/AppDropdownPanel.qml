import QtQuick
import QtQuick.Controls
import MiaCode.UI

// 下拉面板基类（Popup 列表弹层）。补全弹层、下拉框弹窗等"自上而下的列表面板"
// 都继承这里，背景统一走 FloatingCard 的磨砂、描边与阴影。
// 各子类自行配置 closePolicy / padding / contentItem。
Popup {
    id: root
    popupType: Popup.Item

    readonly property PopupLifecycle lifecycle: PopupLifecycle {
        popup: root
    }
    readonly property bool active: lifecycle.active
    enter: lifecycle.enterTransition
    exit: lifecycle.exitTransition

    padding: Theme.menuPadding
    background: FloatingCard { popup: root }
}
