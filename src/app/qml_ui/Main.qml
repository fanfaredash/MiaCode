import QtQuick
import QtQuick.Controls
import MiaCode.UI

ApplicationWindow {
    id: window

    required property var applicationContext
    readonly property var shellController: applicationContext.shell

    width: 1280
    height: 720
    minimumWidth: 620
    minimumHeight: 480
    visible: true
    flags: Qt.platform.os === "windows"
           ? Qt.Window
             | Qt.CustomizeWindowHint
             | Qt.WindowTitleHint
             | Qt.WindowSystemMenuHint
             | Qt.WindowMinimizeButtonHint
             | Qt.WindowMaximizeButtonHint
             | Qt.WindowCloseButtonHint
           : Qt.Window
    title: Qt.application.name + (workbench.documentTitle.length > 0
                                  ? " — " + workbench.documentTitle
                                  : "")
    color: Theme.colors.background.workbench
    topPadding: 0
    leftPadding: 0
    rightPadding: 0
    bottomPadding: 0

    palette.window: Theme.colors.background.workbench
    palette.windowText: Theme.colors.text.primary
    palette.base: Theme.colors.background.editor
    palette.alternateBase: Theme.colors.background.elevated
    palette.text: Theme.colors.text.primary
    palette.button: Theme.colors.background.workbench
    palette.buttonText: Theme.colors.text.primary
    palette.light: Theme.colors.border.control
    palette.midlight: Theme.colors.border.normal
    palette.mid: Theme.colors.border.control
    palette.dark: Theme.colors.background.editor
    palette.shadow: "#000000"
    palette.highlight: Theme.colors.state.menuSelection
    palette.highlightedText: Theme.colors.text.primary
    palette.placeholderText: Theme.colors.text.secondary
    palette.toolTipBase: Theme.colors.background.elevated
    palette.toolTipText: Theme.colors.text.primary

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

    Workbench {
        id: workbench
        anchors.fill: parent
        hostWindow: window
        applicationContext: window.applicationContext
    }
}
