import QtQuick
import QtQuick.Controls
import MiaCode.UI

ApplicationWindow {
    id: window

    property bool sourceEditorFocused: false

    required property var applicationContext
    readonly property var shellLifecycle: applicationContext.shell
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
    topPadding: 0
    leftPadding: 0
    rightPadding: 0
    bottomPadding: 0

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

    // Two-phase close. The unsaved-changes prompt is a QML dialog, so the first
    // close attempt cannot be answered here — it is refused, the question is
    // asked, and the window closes itself once the answer comes back. Telling
    // bootstrap on the way out is what lets preparePreviewForShutdown run
    // before teardown.
    property bool closeApproved: false

    onClosing: function(close) {
        if (window.closeApproved) {
            window.shellLifecycle.notifyRootCloseAccepted("qml_ui_root_closing")
            return
        }
        close.accepted = false
        window.shellLifecycle.requestClose()
    }

    Connections {
        target: window.shellLifecycle
        function onCloseDecided(accepted) {
            if (!accepted)
                return
            window.closeApproved = true
            window.close()
        }
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
        previewSession: window.applicationContext.preview
        preferencesModel: window.applicationContext.preferencesModel
        sourceEditorFocused: window.sourceEditorFocused
        chartCommandsEnabled: window.applicationContext.document.currentDifficultyId > 0
        onChartTransformRequested: opId => mainView.applyChartTransform(opId)
    }
}
