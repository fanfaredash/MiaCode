import QtQuick
import MiaCode.UI
import "qrc:/quick_shell/qml" as Shell

Rectangle {
    id: root

    required property var previewSession
    required property var shellController
    signal fullscreenRequested()

    color: Theme.colors.background.workbench
    clip: true

    PanelHeader {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: root.previewSession.muriMode ? qsTr("MURI模式") : qsTr("实时预览")
    }

    Item {
        id: stageArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: transport.top
        clip: true

        Shell.QuickShellPreviewSurface {
            anchors.centerIn: parent
            width: Math.max(0, Math.min(parent.width, parent.height) * 0.97)
            height: width
            runtime: root.previewSession.runtime
            mediaHost: root.previewSession.mediaHost
            logger: root.shellController
            surfaceRole: "workspace"
            dcompFallbackActive: true
        }
    }

    PreviewTransport {
        id: transport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statistics.top
        previewSession: root.previewSession
        onFullscreenRequested: root.fullscreenRequested()
    }

    NoteStatistics {
        id: statistics
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        statistics: root.previewSession.statistics
    }
}
