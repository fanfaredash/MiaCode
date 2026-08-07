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
    property string tooltip
    property bool active: false
    property bool panelTab: false
    property bool closable: false
    property int count: -1
    property real preferredTabWidth: 160
    readonly property bool hovered: tabButton.hovered || closeButton.hovered

    signal clicked()
    signal closeRequested()

    implicitHeight: panelTab ? 28 : 34
    implicitWidth: panelTab
        ? label.implicitWidth + (count >= 0 ? 22 : 0) + 24
        : preferredTabWidth

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
                anchors.fill: parent
                anchors.leftMargin: root.panelTab ? 12 : 10
                anchors.rightMargin: root.panelTab ? 12 : 5
                spacing: 6

                ControlsImpl.IconImage {
                    Layout.preferredWidth: 15
                    Layout.preferredHeight: 15
                    visible: !root.panelTab && root.iconSource.toString().length > 0
                    source: root.iconSource
                    sourceSize: Qt.size(15, 15)
                    color: root.active ? Theme.colors.text.active : Theme.colors.text.secondary
                }

                Text {
                    id: label

                    Layout.fillWidth: true
                    text: root.text
                    elide: Text.ElideRight
                    color: root.active ? Theme.colors.text.active : Theme.colors.text.secondary
                    font.family: Theme.uiFont
                    font.pixelSize: root.panelTab ? Theme.secondaryFontSize : Theme.uiFontSize
                    horizontalAlignment: root.panelTab ? Text.AlignHCenter : Text.AlignLeft
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
                    Layout.preferredWidth: 16
                    Layout.preferredHeight: 16
                    radius: 8
                    visible: root.count >= 0
                    color: Theme.colors.accent.badge

                    Text {
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
                    Accessible.name: qsTr("关闭 %1").arg(root.text)
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
                        tone: "icon"
                    }
                    Tooltip {
                        visible: closeButton.hovered
                        text: qsTr("关闭 (Ctrl+W)")
                    }
                }
            }
        }

        background: Item {
            // Document tabs: idle/active sheet; hover uses rounded HoverChrome
            // (active tab keeps full-bleed editor fill for adjacent tiling).
            Rectangle {
                anchors.fill: parent
                color: {
                    if (root.panelTab)
                        return Theme.colors.background.surface
                    if (root.active)
                        return Theme.colors.background.editor
                    return Theme.colors.background.surface
                }
            }

            HoverChrome {
                anchors.fill: parent
                margins: root.panelTab ? 2 : -1
                visible: root.hovered && (!root.active || root.panelTab)
                hovered: root.hovered
                tone: "hover"
            }

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                width: 1
                visible: !root.panelTab
                color: Theme.colors.border.normal
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: 1
                visible: root.active && !root.panelTab
                color: Theme.colors.accent.primary
            }

            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                visible: tabButton.activeFocus
                color: Theme.colors.accent.focus
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                width: label.implicitWidth + (root.count >= 0 ? 20 : 0) + 3
                height: 1
                visible: root.active && root.panelTab
                color: Theme.colors.accent.primary
            }
        }
    }

    Tooltip {
        visible: tabButton.hovered && !closeButton.hovered && root.tooltip.length > 0
        text: root.tooltip
    }
}

