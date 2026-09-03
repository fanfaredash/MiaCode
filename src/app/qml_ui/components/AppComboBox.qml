import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Shared combo — geometry mirrors v1 dialogComboBoxStyleSheet (QML Popup, no Win11 chrome).
ComboBox {
    id: root

    property bool compact: false

    font.family: Theme.uiFont
    font.pixelSize: root.compact ? Theme.secondaryFontSize : Theme.uiFontSize
    implicitHeight: root.compact ? 27 : Theme.controlMinHeight
    leftPadding: 10
    rightPadding: 28
    hoverEnabled: true

    readonly property FontMetrics textMetrics: FontMetrics { font: root.font }

    contentItem: Text {
        leftPadding: 0
        rightPadding: 0
        text: root.displayText
        font: root.font
        color: root.enabled ? Theme.colors.text.primary : Theme.colors.text.disabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Item {
        x: root.width - width - 8
        y: root.topPadding + (root.availableHeight - height) / 2
        width: 12
        height: 12

        Canvas {
            anchors.fill: parent
            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = root.enabled ? Theme.colors.text.secondary
                                               : Theme.colors.text.disabled
                ctx.lineWidth = 1.5
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.beginPath()
                ctx.moveTo(2.5, 4.5)
                ctx.lineTo(6, 8)
                ctx.lineTo(9.5, 4.5)
                ctx.stroke()
            }
            Component.onCompleted: requestPaint()
        }
    }

    background: Rectangle {
        implicitHeight: root.implicitHeight
        radius: Theme.controlRadius
        color: Theme.overlayColor(root.enabled
               ? Theme.colors.background.control
               : Theme.colors.background.controlDisabled)
        border.width: root.enabled && (root.visualFocus || root.hovered || root.down)
                      ? Theme.controlBorderWidth : 0
        border.color: Theme.colors.accent.primary
    }

    delegate: ChromeRow {
        stateColors: Theme.colors.popupState
        id: itemDelegate
        width: ListView.view ? ListView.view.width : root.width
        height: 28
        highlighted: root.highlightedIndex === index
        text: root.textAt(index)
        labelFont: root.font
    }

    popup: AppDropdownPanel {
        property real optionWidth: 0

        y: root.height + 2
        implicitWidth: Math.max(root.width, 100, optionWidth + leftPadding + rightPadding)
        width: Math.min(implicitWidth, Overlay.overlay ? Overlay.overlay.width : implicitWidth)
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 260)

        onAboutToShow: {
            let widest = 0
            for (let i = 0; i < root.count; ++i)
                widest = Math.max(widest, root.textMetrics.advanceWidth(root.textAt(i)))
            optionWidth = Math.ceil(widest) + 2 * Theme.rowPaddingX
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
