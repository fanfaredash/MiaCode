import QtQuick
import QtQuick.Controls
import MiaCode.UI

ApplicationWindow {
    id: window

    required property var applicationContext
    readonly property var shellController: applicationContext.shell
    readonly property var platform: applicationContext.platform

    width: 1280
    height: 720
    minimumWidth: 620
    minimumHeight: 480
    // C++ explicitly shows this only after MainWindow has registered the root
    // and installed the shared chart-audio drop route.
    visible: false
    flags: {
        let value = Qt.Window
        if (window.platform.captionButtons) {
            value |= Qt.CustomizeWindowHint
                    | Qt.WindowTitleHint
                    | Qt.WindowSystemMenuHint
                    | Qt.WindowMinimizeButtonHint
                    | Qt.WindowMaximizeButtonHint
                    | Qt.WindowCloseButtonHint
        }
        if (window.platform.expandClientArea) {
            value |= Qt.ExpandedClientAreaHint
                    | Qt.NoTitleBarBackgroundHint
        }
        return value
    }
    title: Qt.application.name + (mainView.documentTitle.length > 0
                                  ? " — " + mainView.documentTitle
                                  : "")
    color: Theme.colors.background.surface

    palette.window: Theme.colors.background.surface
    palette.windowText: Theme.colors.text.primary
    palette.base: Theme.colors.background.editor
    palette.alternateBase: Theme.colors.background.elevated
    palette.text: Theme.colors.text.primary
    palette.button: Theme.colors.background.surface
    palette.buttonText: Theme.colors.text.primary
    palette.light: Theme.colors.border.control
    palette.midlight: Theme.colors.border.normal
    palette.mid: Theme.colors.border.control
    palette.dark: Theme.colors.background.editor
    palette.shadow: "#000000"
    palette.highlight: Theme.colors.state.selected
    palette.highlightedText: Theme.colors.text.active
    palette.placeholderText: Theme.colors.text.secondary
    palette.toolTipBase: Theme.colors.background.elevated
    palette.toolTipText: Theme.colors.text.primary
    palette.disabled.text: Theme.colors.text.disabled
    palette.disabled.windowText: Theme.colors.text.disabled
    palette.disabled.buttonText: Theme.colors.text.disabled

    Binding {
        target: Theme
        property: "preferences"
        value: window.applicationContext.preferences
    }

    // Same close contract as QuickShell v1: confirm via MainWindow, then notify
    // bootstrap so preparePreviewForShutdown runs before teardown.
    onClosing: function(close) {
        const confirmed = window.shellController.confirmClose()
        if (!confirmed)
            close.accepted = false
        if (close.accepted)
            window.shellController.notifyRootCloseAccepted("qml_ui_root_closing")
    }

    MainView {
        id: mainView
        anchors.fill: parent
        hostWindow: window
        applicationContext: window.applicationContext
    }

    // Mounted on the root window rather than inside MainView: these bindings
    // must survive whichever page or overlay is on screen.
    ShortcutBindings {
        shortcuts: window.applicationContext.shortcuts
        commands: window.applicationContext.commands
        shellController: window.shellController
        chartCommandsEnabled: window.applicationContext.document.currentDifficultyId > 0
    }
}
