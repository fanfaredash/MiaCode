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
        color: root.enabled ? Theme.colors.background.editor : Theme.colors.background.elevated
        border.width: Theme.controlBorderWidth
        border.color: !root.enabled ? Theme.colors.border.normal
                     : (root.activeFocus || root.hovered || root.down) ? Theme.colors.accent.primary
                     : Theme.colors.border.control
    }

    delegate: ItemDelegate {
        id: itemDelegate
        width: ListView.view ? ListView.view.width : root.width
        height: 28
        highlighted: root.highlightedIndex === index
        hoverEnabled: true

        contentItem: Text {
            text: root.textAt(index)
            font: root.font
            color: itemDelegate.highlighted || itemDelegate.hovered
                   ? Theme.colors.text.active
                   : Theme.colors.text.secondary
            elide: Text.ElideRight
            verticalAlignment: Text.AlignVCenter
            leftPadding: 10
            rightPadding: 10
        }

        background: HoverChrome {
            selected: itemDelegate.highlighted || itemDelegate.hovered
            tone: "nav"
        }
    }

    popup: Popup {
        y: root.height + 2
        width: Math.max(root.width, 100)
        padding: 6
        implicitHeight: Math.min(contentItem.implicitHeight + topPadding + bottomPadding, 260)

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: root.delegateModel
            currentIndex: root.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }

        background: Rectangle {
            radius: Theme.controlRadius
            color: Theme.colors.background.elevated
            border.width: Theme.controlBorderWidth
            border.color: Theme.colors.border.normal
        }
    }
}
