import QtQuick
import QtQuick.Templates as T
import QtQuick.Effects
import QtQuick.Window
import MiaCode.UI

// 卡片填满浮层背景，内容留白由浮层 padding 决定。
// 阴影通过效果的自动扩边绘制在卡片外侧。
Item {
    id: root

    property T.Popup popup: null
    property color tintColor: Theme.popupTintColor
    property int blurRadius: Theme.popupBlurRadius
    readonly property Item backdropSource: root.Window.window?.backdropSource ?? null

    Loader {
        id: backdrop
        anchors.fill: parent
        active: root.popup !== null && root.popup.visible
                && root.backdropSource !== null && root.width > 0 && root.height > 0
        sourceComponent: BackdropBlur {
            sourceItem: root.backdropSource
            popup: root.popup
            blurRadius: root.blurRadius
        }
    }

    Rectangle {
        id: card
        anchors.fill: parent
        radius: root.popup ? Theme.popupRadius : Theme.controlRadius
        color: backdrop.active ? root.tintColor
                              : Theme.overlayColor(Theme.colors.background.elevated, Theme.popupOpacity)
    }

    layer.enabled: true
    layer.effect: MultiEffect {
        autoPaddingEnabled: true
        shadowEnabled: true
        shadowBlur: 0.6
        shadowColor: "#000000"
        shadowOpacity: Theme.popupShadowOpacity
        shadowVerticalOffset: 2
        shadowHorizontalOffset: 0
    }
}
