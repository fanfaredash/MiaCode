import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Sticky extras live in this popup (a Menu would dismiss on item click).
AppStickyPopup {
    id: root

    required property var previewSession
    minimumWidth: 180

    readonly property real rowWidth: Math.max(
        180,
        regularRow.implicitWidth,
        muriRow.implicitWidth,
        smoothSwitch.implicitWidth
    )

    contentItem: Column {
        id: body
        width: root.rowWidth
        spacing: 0

        component ModeRow: ChromeRow {
            id: modeRow
            stateColors: Theme.colors.popupState

            required property string label
            required property bool active

            implicitHeight: 28
            implicitWidth: 12 + 10 + modeLabel.implicitWidth + leftPadding + rightPadding
            leftPadding: 12
            rightPadding: 16
            selected: modeRow.active
            Accessible.name: modeRow.label
            Accessible.checkable: true
            Accessible.checked: modeRow.active

            contentItem: Item {
                Text {
                    id: checkMark
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    width: 12
                    text: modeRow.active ? "✓" : ""
                    color: Theme.colors.text.active
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
                    horizontalAlignment: Text.AlignHCenter
                }

                Text {
                    id: modeLabel
                    anchors.left: checkMark.right
                    anchors.leftMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    text: modeRow.label
                    color: !modeRow.enabled ? Theme.colors.text.disabled
                         : (modeRow.active || modeRow.hovered || modeRow.down) ? Theme.colors.text.active
                         : Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.uiFontSize
                }
            }

        }

        ModeRow {
            id: regularRow
            width: body.width
            label: UiText.text("常规渲染")
            active: !root.previewSession.muriCheckEnabled
            onClicked: root.previewSession.setMuriCheckEnabled(false)
        }

        ModeRow {
            id: muriRow
            width: body.width
            label: UiText.text("无理检测")
            active: root.previewSession.muriCheckEnabled
            onClicked: root.previewSession.setMuriCheckEnabled(true)
        }

        Item {
            width: parent.width
            implicitHeight: 15

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: 6
                anchors.rightMargin: 6
                anchors.verticalCenter: parent.verticalCenter
                height: 1
                color: Theme.colors.border.normal
            }
        }

        AppSwitch {
            id: smoothSwitch
            visible: !root.previewSession.muriCheckEnabled
            width: body.width
            leftPadding: 12
            rightPadding: 16
            text: UiText.text("平滑星星消去动画")
            checked: root.previewSession.smoothStarErase
            onToggled: {
                if (checked === root.previewSession.smoothStarErase)
                    return
                root.previewSession.setSmoothStarErase(checked)
            }

            Connections {
                target: root.previewSession
                function onRenderModeChanged() {
                    if (smoothSwitch.checked !== root.previewSession.smoothStarErase)
                        smoothSwitch.checked = root.previewSession.smoothStarErase
                }
            }
        }

        Column {
            visible: root.previewSession.muriCheckEnabled
            width: body.width
            spacing: 4
            leftPadding: 12
            rightPadding: 16
            topPadding: 4
            bottomPadding: 4

            Text {
                width: parent.width - parent.leftPadding - parent.rightPadding
                text: UiText.text("无理判定半径")
                color: Theme.colors.text.primary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
            }

            Text {
                width: parent.width - parent.leftPadding - parent.rightPadding
                wrapMode: Text.Wrap
                text: UiText.text("A / B / C / D / E 各区半径不同，暂不可调。")
                color: Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.secondaryFontSize
            }
        }
    }
}
