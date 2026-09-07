import QtQuick
import QtQuick.Controls
import MiaCode.UI
import "qrc:/preview/runtime/qml" as Preview

Rectangle {
    id: root

    required property var previewSession
    required property var preferences
    // See PreviewTransport: the canvas menu hides on the export page.
    property bool exportPageActive: false
    // MainSplitView keeps the transport chrome mounted for layout stability, but
    // exactly one PreviewSurface may subscribe to the runtime at a time. The
    // compact and fullscreen owners use the same rule.
    property bool surfaceActive: true
    readonly property real minimumHeight: heading.implicitHeight + transport.implicitHeight
                                          + statistics.implicitHeight + 64
    readonly property real minimumWidth: transport.minimumWidth
    signal fullscreenRequested()

    // Export page still uses the backend ratio. Edit mode defaults to 1:1;
    // free aspect sizes the surface to the live stage geometry.
    readonly property bool freeAspectActive:
        !root.exportPageActive && root.preferences && root.preferences.previewCanvasFreeAspect
    readonly property real canvasAspectRatio: {
        const ratio = root.previewSession && root.previewSession.canvasAspectRatio !== undefined
                      ? root.previewSession.canvasAspectRatio
                      : 1.0
        return Math.max(1.0, (ratio > 0 && isFinite(ratio)) ? ratio : 1.0)
    }

    color: Theme.surfaceColor(Theme.colors.background.panel)
    clip: true

    function fittedFrameWidth(hostWidth, hostHeight) {
        const safeWidth = Math.max(1, hostWidth)
        const safeHeight = Math.max(1, hostHeight)
        return Math.max(1, Math.min(safeWidth, safeHeight * root.canvasAspectRatio))
    }

    function fittedFrameHeight(hostWidth, hostHeight) {
        const frameWidth = fittedFrameWidth(hostWidth, hostHeight)
        return Math.max(1, Math.min(hostHeight, frameWidth / root.canvasAspectRatio))
    }

    PanelHeader {
        id: heading
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        title: UiText.text("预览")
        sidebarTitle: true

        ChromeRow {
            id: renderModeButton
            implicitWidth: renderModeLabelText.implicitWidth + leftPadding + rightPadding
            selected: renderModeMenu.active
            focusPolicy: Qt.TabFocus
            Accessible.name: root.previewSession.renderModeLabel
            Accessible.description: UiText.text("打开预览渲染模式菜单")
            onClicked: {
                if (renderModeMenu.active) {
                    renderModeMenu.close()
                    return
                }
                renderModeMenu.openAt(renderModeButton)
            }

            contentItem: Text {
                id: renderModeLabelText
                text: root.previewSession.renderModeLabel
                color: !renderModeButton.enabled ? Theme.colors.text.disabled
                     : (renderModeButton.selected || renderModeButton.hovered || renderModeButton.visualFocus) ? Theme.colors.text.active
                     : Theme.colors.text.secondary
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }

        }
    }

    PreviewRenderModeMenu {
        id: renderModeMenu
        previewSession: root.previewSession
    }

    Item {
        id: stageArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: heading.bottom
        anchors.bottom: transport.top
        clip: true

        Loader {
            id: previewSurfaceLoader
            anchors.centerIn: parent
            width: root.freeAspectActive
                   ? parent.width
                   : root.fittedFrameWidth(parent.width, parent.height)
            height: root.freeAspectActive
                    ? parent.height
                    : root.fittedFrameHeight(parent.width, parent.height)
            // Do not construct an invisible scene root: it still subscribes to
            // PreviewRuntime::frameStateChanged even when QSG skips painting it.
            active: root.surfaceActive && root.visible && width >= 64 && height >= 64

            sourceComponent: Preview.PreviewSurface {
                anchors.fill: parent
                runtime: root.previewSession.runtime
                mediaHost: root.previewSession.mediaHost
                logger: root.previewSession
                surfaceRole: "workspace"
                backgroundColor: "transparent"
                hudTextColor: Theme.colors.previewHud.text
                hudShadowColor: Theme.colors.previewHud.shadow
            }
        }
    }

    PreviewTransport {
        id: transport
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: statistics.top
        previewSession: root.previewSession
        preferences: root.preferences
        exportPageActive: root.exportPageActive
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
