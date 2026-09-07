import QtQuick
import QtQuick.Controls
import MiaCode.UI

ApplicationWindow {
    id: window

    required property var controller
    required property var coverSession
    required property var preferences
    readonly property Item backdropSource: page

    title: UiText.text("cover.export_cover")
    width: 1280
    height: 720
    minimumWidth: page.implicitWidth
    minimumHeight: 480
    visible: false
    color: Theme.colors.background.surface
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize

    palette.window: Theme.colors.background.surface
    palette.windowText: Theme.colors.text.primary
    palette.base: Theme.colors.background.surface
    palette.text: Theme.colors.text.primary
    palette.button: Theme.colors.background.panel
    palette.buttonText: Theme.colors.text.primary
    palette.highlight: Theme.colors.state.textSelection
    palette.highlightedText: Theme.colors.text.active
    palette.placeholderText: Theme.colors.text.secondary
    palette.disabled.text: Theme.colors.text.disabled
    palette.disabled.buttonText: Theme.colors.text.disabled

    Binding {
        target: Theme
        property: "preferences"
        value: window.preferences
    }
    Binding {
        target: UiText
        property: "provider"
        value: window.preferences
    }

    onClosing: function(event) {
        event.accepted = !window.coverSession.busy
        if (event.accepted)
            window.controller.close()
    }

    CoverExportPage {
        id: page
        anchors.fill: parent
        coverSession: window.coverSession
        onCloseRequested: window.close()
    }

    UiRequestHost {
        requests: window.coverSession.uiRequests
    }

    Shortcut {
        sequence: StandardKey.Close
        onActivated: window.close()
    }
}
