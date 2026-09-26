import QtQuick
import QtQuick.Controls
import MiaCode.UI

MenuBar {
    id: root

    required property var commands
    required property var shortcuts
    required property var documentSession
    required property var pet
    property bool commandsEnabled: true
    property bool saveEnabled: true
    property bool wholeDocumentSaveEnabled: true
    property bool documentAvailable: true
    property bool editorCommandsEnabled: true
    property bool chartCommandsEnabled: true
    property bool toolCommandsEnabled: true
    property bool normalizationEnabled: true

    property MainMenu menuDefinitions: MainMenu {
        visible: false
        commands: root.commands
        shortcuts: root.shortcuts
        documentSession: root.documentSession
        pet: root.pet
        commandsEnabled: root.commandsEnabled
        saveEnabled: root.saveEnabled
        wholeDocumentSaveEnabled: root.wholeDocumentSaveEnabled
        documentAvailable: root.documentAvailable
        editorCommandsEnabled: root.editorCommandsEnabled
        chartCommandsEnabled: root.chartCommandsEnabled
        toolCommandsEnabled: root.toolCommandsEnabled
        normalizationEnabled: root.normalizationEnabled
        nativeMenuMode: true
    }

    property Action aboutApplicationAction: Action {
        text: "About " + Qt.application.name
        enabled: root.commandsEnabled
        onTriggered: root.commands.aboutRequested()
    }

    property Action preferencesApplicationAction: Action {
        text: "Preferences..."
        enabled: root.commandsEnabled
        onTriggered: root.commands.preferencesRequested()
    }

    property Action quitApplicationAction: Action {
        text: "Quit " + Qt.application.name
        shortcut: StandardKey.Quit
        enabled: root.commandsEnabled
        onTriggered: root.commands.exitRequested()
    }

    function nativeMenuTitle(text) {
        let title = text.replace(/\(&.\)$/, "")
        title = title.replace(/&&/g, "\u0000")
        title = title.replace(/&/g, "")
        return title.replace(/\u0000/g, "&")
    }

    Component.onCompleted: {
        const menus = root.menuDefinitions.nativeMenus()
        menus[0].addAction(root.quitApplicationAction)
        menus[1].addAction(root.preferencesApplicationAction)
        menus[5].addAction(root.aboutApplicationAction)
        for (let i = 0; i < menus.length; ++i) {
            menus[i].title = root.nativeMenuTitle(menus[i].title)
            root.addMenu(menus[i])
        }
    }
}
