import QtQuick
import QtQuick.Controls
import QtQuick.Controls.impl as ControlsImpl
import QtQuick.Layouts
import MiaCode.UI

Item {
    id: root

    property string text
    property string secondaryText
    property url iconSource
    property int difficultyId: 0
    property string tooltip
    property bool active: false
    property bool panelTab: false
    property bool compact: false
    property bool closable: false
    property int count: -1
    property real preferredTabWidth: 160
    readonly property bool hovered: tabButton.hovered || closeButton.hovered

    signal clicked()
    signal closeRequested()

    implicitHeight: compact ? Theme.compactControlHeight : panelTab ? 28 : 34
    implicitWidth: panelTab
        ? contentRow.implicitWidth + contentRow.anchors.leftMargin + contentRow.anchors.rightMargin
        : preferredTabWidth

    FontMetrics {
        id: tabMetrics
        font.family: Theme.uiFont
        font.pixelSize: root.panelTab ? Theme.secondaryFontSize : Theme.uiFontSize
    }

    AbstractButton {
        id: tabButton

        anchors.fill: parent
        hoverEnabled: true
        focusPolicy: Qt.TabFocus
        Accessible.name: root.secondaryText.length > 0
            ? root.text + " " + root.secondaryText
            : root.text
        Accessible.description: root.tooltip
        onClicked: root.clicked()

        TapHandler {
            acceptedButtons: Qt.MiddleButton
            onTapped: {
                if (root.closable)
                    root.closeRequested()
            }
        }

        contentItem: Item {
            RowLayout {
                id: contentRow

                anchors.fill: parent
                anchors.leftMargin: root.compact ? 8 : root.panelTab ? 12 : 10
                anchors.rightMargin: root.compact ? 8 : root.panelTab ? 12 : 5
                spacing: 6

                DifficultySwatch {
                    Layout.preferredWidth: implicitWidth
                    Layout.preferredHeight: implicitHeight
                    Layout.alignment: Qt.AlignVCenter
                    visible: !root.panelTab && root.difficultyId > 0
                    difficultyId: root.difficultyId
                }

                ControlsImpl.IconImage {
                    Layout.preferredWidth: 15
                    Layout.preferredHeight: 15
                    visible: !root.panelTab
                             && root.difficultyId <= 0
                             && root.iconSource.toString().length > 0
                    source: root.iconSource
                    sourceSize: Qt.size(15, 15)
                    color: root.active ? Theme.colors.text.active : Theme.colors.text.secondary
                }

                Text {
                    id: label

                    Layout.fillWidth: !root.panelTab
                    text: root.text
                    elide: Text.ElideRight
                    color: root.active ? Theme.colors.text.active : Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: root.compact ? Theme.compactFontSize : Theme.secondaryFontSize
                    horizontalAlignment: Text.AlignLeft
                    verticalAlignment: Text.AlignVCenter
                }

                Text {
                    Layout.preferredWidth: implicitWidth
                    visible: !root.panelTab && root.secondaryText.length > 0
                    text: root.secondaryText
                    color: Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: Theme.secondaryFontSize
                    verticalAlignment: Text.AlignVCenter
                }

                Rectangle {
                    implicitWidth: Math.max(implicitHeight, countLabel.implicitWidth + 8)
                    implicitHeight: 16
                    radius: height / 2
                    visible: root.count > 0
                    color: Theme.colors.accent.badge

                    Text {
                        id: countLabel

                        anchors.fill: parent
                        text: root.count
                        color: Theme.colors.text.onAccent
                        font.family: Theme.uiFont
                        font.pixelSize: 10
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }

                AbstractButton {
                    id: closeButton

                    Layout.preferredWidth: root.closable ? 24 : 0
                    Layout.preferredHeight: 24
                    visible: root.closable
                    opacity: root.active || root.hovered || activeFocus ? 1 : 0
                    enabled: opacity > 0
                    hoverEnabled: true
                    focusPolicy: Qt.TabFocus
                    Accessible.name: UiText.text("关闭 %1").arg(root.text)
                    onClicked: root.closeRequested()

                    contentItem: ControlsImpl.IconImage {
                        anchors.centerIn: parent
                        width: 14
                        height: 14
                        source: Qt.resolvedUrl("icons/close.svg")
                        sourceSize: Qt.size(14, 14)
                        color: Theme.colors.text.secondary
                    }
                    background: HoverChrome {
                        hovered: closeButton.hovered
                        pressed: closeButton.down
                        focused: closeButton.visualFocus
                    }
                    Tooltip {
                        visible: closeButton.hovered
                        text: UiText.text("关闭 (Ctrl+W)")
                    }
                }
            }
        }

        background: HoverChrome {
            contentHeight: Math.ceil(tabMetrics.height)
            selected: root.active
            hovered: tabButton.hovered && !closeButton.hovered
            pressed: tabButton.down
            focused: tabButton.visualFocus
        }
    }

    Tooltip {
        visible: tabButton.hovered && !closeButton.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
}
