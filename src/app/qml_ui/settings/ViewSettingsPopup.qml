import QtQuick
import QtQuick.Controls
import MiaCode.UI

Popup {
    id: root

    required property var workbenchState
    required property var preferences

    width: Math.min(270, parent.width - 20)
    padding: 12
    modal: false
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside

    background: Rectangle {
        color: Theme.colors.background.elevated
        border.color: Theme.colors.border.normal
        radius: 7
    }

    contentItem: Column {
        spacing: 8

        Text {
            text: qsTr("视图设置")
            color: Theme.colors.text.primary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            font.weight: Font.DemiBold
        }

        SettingLabel { text: qsTr("时间轴长度  ") + root.preferences.timelineLength + " px" }
        Slider {
            width: parent.width
            from: 1200
            to: 4800
            stepSize: 200
            value: root.preferences.timelineLength
            onMoved: root.preferences.timelineLength = Math.round(value / 200) * 200
        }

        CheckBox {
            text: qsTr("显示字段栏")
            checked: root.workbenchState.sidebarVisible
            onToggled: {
                root.workbenchState.sidebarVisible = checked
                root.preferences.sidebarVisible = checked
            }
        }
        CheckBox {
            text: qsTr("显示底部面板")
            checked: root.workbenchState.bottomPanelVisible
            onToggled: {
                root.workbenchState.bottomPanelVisible = checked
                root.preferences.bottomPanelVisible = checked
            }
        }
        CheckBox {
            text: qsTr("显示实时预览")
            checked: root.workbenchState.previewVisible
            onToggled: {
                root.workbenchState.previewVisible = checked
                root.preferences.previewVisible = checked
            }
        }
        CheckBox {
            text: qsTr("打开时定位播放头")
            checked: root.preferences.timelineAutoCenter
            onToggled: root.preferences.timelineAutoCenter = checked
        }

        Button {
            text: qsTr("恢复默认布局")
            onClicked: {
                root.preferences.resetToDefaults()
                root.workbenchState.sidebarVisible = root.preferences.sidebarVisible
                root.workbenchState.bottomPanelVisible = root.preferences.bottomPanelVisible
                root.workbenchState.previewVisible = root.preferences.previewVisible
                root.workbenchState.compactPanel = ""
            }
        }
    }

    component SettingLabel: Text {
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
    }
}

