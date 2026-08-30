pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

// Window-level shortcuts for v2.
//
// v1 binds these as QActions on MainWindow with Qt::WindowShortcut context. In
// v2 that window is hidden and therefore never active, so none of them ever
// fired — the chart transforms had no keyboard route at all. Binding them here
// puts them on the window the user is actually typing into.
//
// Sequences come from ShortcutRegistry, the same table the Preferences editor
// writes, so a customized binding applies to v1 and v2 alike. The id list comes
// from the command service rather than being retyped here.
Item {
    id: root

    required property var shortcuts
    required property var commands
    required property var shellController
    required property bool sourceEditorFocused
    // Transforms edit the chart, so they are inert without one.
    property bool chartCommandsEnabled: true

    Instantiator {
        model: root.commands.shortcutCommandIds()
        delegate: Shortcut {
            required property string modelData
            // Re-resolved when the registry reloads.
            sequence: root.shortcuts.revision >= 0
                ? root.shortcuts.sequence(modelData)
                : ""
            enabled: root.chartCommandsEnabled && sequence !== ""
            context: Qt.WindowShortcut
            onActivated: root.commands.triggerShortcutCommand(modelData)
        }
    }

    // Preview commands already have a QML-facing surface on the shell
    // controller, so they bind straight to it instead of going through the
    // backend command table.
    Shortcut {
        sequence: root.shortcuts.revision >= 0
            ? root.shortcuts.sequence("editor.font_decrease", "Ctrl+Alt+-")
            : ""
        enabled: sequence !== ""
        context: Qt.WindowShortcut
        onActivated: root.commands.adjustEditorFontSize(-1)
    }

    Shortcut {
        sequence: root.shortcuts.revision >= 0
            ? root.shortcuts.sequence("editor.font_increase", "Ctrl+Alt+=")
            : ""
        enabled: sequence !== ""
        context: Qt.WindowShortcut
        onActivated: root.commands.adjustEditorFontSize(1)
    }

    Shortcut {
        sequence: root.shortcuts.revision >= 0
            ? root.shortcuts.sequence("preview.stop_or_play", "Ctrl+X")
            : ""
        enabled: sequence !== ""
        context: Qt.WindowShortcut
        onActivated: root.shellController.stopPreview()
    }

    Shortcut {
        sequence: root.shortcuts.revision >= 0
            ? root.shortcuts.sequence("preview.play_pause_global", "Ctrl+Shift+X")
            : ""
        enabled: sequence !== "" && !root.sourceEditorFocused
        context: Qt.ApplicationShortcut
        onActivated: root.shellController.togglePreviewPlayback()
    }

    Shortcut {
        sequence: root.shortcuts.revision >= 0
            ? root.shortcuts.sequence("preview.speed_down", "Ctrl+O")
            : ""
        enabled: sequence !== ""
        context: Qt.WindowShortcut
        onActivated: root.shellController.adjustPreviewSpeed(-1)
    }

    Shortcut {
        sequence: root.shortcuts.revision >= 0
            ? root.shortcuts.sequence("preview.speed_up", "Ctrl+P")
            : ""
        enabled: sequence !== ""
        context: Qt.WindowShortcut
        onActivated: root.shellController.adjustPreviewSpeed(1)
    }
}
