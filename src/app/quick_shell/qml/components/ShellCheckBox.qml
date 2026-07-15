import QtQuick
import QtQuick.Controls

CheckBox {
    id: root

    property var paletteMap: ({})
    property var metricsMap: ({})
    property real scale: 1.0
    property color shellTextColor: shellTheme.tone("textPrimary", "#203040")
    property int shellTextPixelSize: Math.max(1, Math.round(12 * scale))
    property int shellTextWeight: Font.DemiBold

    hoverEnabled: true
    spacing: Math.round(4 * scale)

    Theme {
        id: shellTheme
        paletteMap: root.paletteMap
        metricsMap: root.metricsMap
    }

    indicator: Rectangle {
        implicitWidth: Math.max(1, Math.round(14 * root.scale))
        implicitHeight: Math.max(1, Math.round(14 * root.scale))
        x: 0
        y: (root.height - height) / 2
        radius: 3
        color: root.checked
            ? shellTheme.tone("accent", "#60a5fa")
            : shellTheme.tone("cardBg", "#1f2937")
        border.width: 1
        border.color: root.checked
            ? shellTheme.tone("accent", "#60a5fa")
            : (root.hovered
                ? shellTheme.tone("accent", "#60a5fa")
                : shellTheme.tone("border", "#475569"))

        Canvas {
            id: checkMark
            anchors.fill: parent
            visible: root.checked

            onVisibleChanged: requestPaint()

            Connections {
                target: root

                function onPaletteMapChanged() {
                    checkMark.requestPaint()
                }

                function onCheckedChanged() {
                    checkMark.requestPaint()
                }
            }

            onPaint: {
                const ctx = getContext("2d")
                ctx.reset()
                ctx.strokeStyle = shellTheme.tone("accentText", "#ffffff")
                ctx.lineWidth = 1.8
                ctx.lineCap = "round"
                ctx.lineJoin = "round"
                ctx.beginPath()
                ctx.moveTo(width * 0.24, height * 0.55)
                ctx.lineTo(width * 0.44, height * 0.74)
                ctx.lineTo(width * 0.78, height * 0.28)
                ctx.stroke()
            }
        }
    }

    contentItem: Text {
        text: root.text
        color: root.shellTextColor
        font.pixelSize: root.shellTextPixelSize
        font.weight: root.shellTextWeight
        verticalAlignment: Text.AlignVCenter
        leftPadding: root.indicator.width + root.spacing
    }
}
