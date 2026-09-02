import QtQuick
import QtQuick.Controls
import MiaCode.UI

// 下拉面板基类（Popup 列表弹层）。补全弹层、下拉框弹窗等"自上而下的列表面板"
// 都继承这里，背景统一走 FloatingCard（无描边 + 微小阴影）。
// 各子类自行配置 closePolicy / padding / contentItem。
Popup {
    id: root

    enter: FadeTransition { id: fadeIn }
    exit: FadeTransition {
        appearing: false
        initialOpacity: root.opacity
    }

    property bool closing: false
    readonly property bool active: visible && !closing

    Connections {
        target: root
        function onAboutToShow() {
            fadeIn.initialOpacity = root.closing ? root.opacity : 0
            root.closing = false
        }
        function onAboutToHide() { root.closing = true }
        function onClosed() { root.closing = false }
    }

    background: FloatingCard {}
}
