import QtQuick
import QtQuick.Effects
import MiaCode.UI

// 卡片填满浮层背景，内容留白由浮层 padding 决定。
// 阴影通过效果的自动扩边绘制在卡片外侧。
Item {
    id: root

    Rectangle {
        id: card
        anchors.fill: parent
        radius: Theme.controlRadius
        color: Theme.colors.background.elevated
    }

    layer.enabled: true
    layer.effect: MultiEffect {
        autoPaddingEnabled: true
        shadowEnabled: true
        shadowBlur: 0.6
        shadowColor: "#000000"
        shadowOpacity: 0.28
        shadowVerticalOffset: 2
        shadowHorizontalOffset: 0
    }
}
