import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import MiaCode.UI

// 预览设置. Like 音频设置 and 偏好设置, every control writes straight through and
// persists on the spot — there is no OK/Apply.
//
// 视频、玩法和皮肤 all describe the running preview. 性能只含预览刷新率，仍放在
// 偏好设置 → 性能，避免同一设置有两个入口。
AppDialog {
    id: root

    required property var previewSettings

    property int activePage: 0

    title: qsTrId("action.video_settings")
    preferredWidth: 640
    preferredHeight: Theme.dialogHeight
    footer: DialogFooter {
        cancelText: qsTrId("action.close")
        onRejected: root.reject()
    }

    body: ColumnLayout {
        spacing: 10

        Row {
            spacing: 4
            Repeater {
                model: root.previewSettings
                       ? [root.previewSettings.videoGroupLabel,
                          root.previewSettings.gameplayGroupLabel,
                          root.previewSettings.skinGroupLabel]
                       : []
                delegate: AppTab {
                    required property int index
                    required property string modelData
                    panelTab: true
                    text: modelData
                    active: root.activePage === index
                    onClicked: root.activePage = index
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.colors.border.normal
        }

        PreviewAppearancePages {
            Layout.fillWidth: true
            previewSettings: root.previewSettings
            pageIndex: root.activePage
        }
    }

    onOpened: if (root.previewSettings) root.previewSettings.refresh()
}
