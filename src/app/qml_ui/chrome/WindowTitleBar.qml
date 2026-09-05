import QtQuick
import QtQuick.Controls
import MiaCode.UI

Rectangle {
    id: root

    required property var hostWindow
    required property var menuCommands
    required property var shortcuts
    required property var documentSession
    required property var platform
    property string documentTitle: ""
    property real leadingInset: 0
    property bool normalizationEnabled: true

    readonly property bool useEmbeddedMenu: root.platform.embeddedMenuInTitleBar
    readonly property bool useCaptionButtons: root.platform.captionButtons
    readonly property real brandContentPadding: Theme.chromePadding
    readonly property real brandLeadingMargin:
        (root.leadingInset > 0 ? root.leadingInset : 10) - brandContentPadding

    implicitHeight: 32
    color: Theme.surfaceColor(Theme.colors.background.titleBar)

    // Title stays window-centered. Menu only yields to the painted glyph width
    // (capped by the max title band), not the whole empty center band.
    readonly property real titleBandMax: Math.min(320, width * 0.3)
    readonly property real titleGlyphWidth: {
        const glyph = titleLabel.implicitWidth
        if (glyph <= 1)
            return 0
        return Math.min(glyph, root.titleBandMax)
    }
    readonly property real titleBandLeft: (width - titleGlyphWidth) / 2
    readonly property real menuGap: 16
    readonly property real menuLeft: brand.x + brand.width
    readonly property real menuAvailableWidth: root.useEmbeddedMenu
        ? Math.max(0, titleBandLeft - menuGap - menuLeft)
        : 0

    function scheduleMainMenuReflow() {
        if (mainMenuLoader.item)
            mainMenuLoader.item.scheduleReflow()
    }

    onWidthChanged: root.scheduleMainMenuReflow()
    onMenuAvailableWidthChanged: root.scheduleMainMenuReflow()
    onDocumentTitleChanged: root.scheduleMainMenuReflow()

    WindowGestureArea {
        anchors.fill: parent
        hostWindow: root.hostWindow
        z: 0
    }

    ChromeRow {
        id: brand
        anchors.left: parent.left
        anchors.leftMargin: root.brandLeadingMargin
        anchors.verticalCenter: parent.verticalCenter
        // Font ascent makes glyphs look high; nudge down for optical center.
        anchors.verticalCenterOffset: 1
        height: Theme.controlMinHeight
        leftPadding: root.brandContentPadding
        rightPadding: root.brandContentPadding
        topPadding: 0
        bottomPadding: 0
        stateColors: Theme.colors.activityState
        selected: brandMenu.active
        Accessible.name: "MiaCode"
        z: 2

        implicitWidth: brandContent.implicitWidth + leftPadding + rightPadding

        contentItem: Row {
            id: brandContent
            spacing: 7

            Image {
                width: Theme.titleBarBrandIconSize
                height: Theme.titleBarBrandIconSize
                anchors.verticalCenter: parent.verticalCenter
                source: Qt.resolvedUrl("icons/app-titlebar.png")
                smooth: true
            }

            Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "MiaCode"
                color: brand.hovered || brandMenu.active
                       ? Theme.colors.text.active : Theme.colors.text.chrome
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                font.bold: true
            }
        }

        onClicked: {
            if (brandMenu.active)
                brandMenu.close()
            else
                brandMenu.popup(brand, 0, brand.height)
        }
    }

    AppMenu {
        id: brandMenu

        AppMenuAction {
            text: UiText.text("关于 MiaCode")
            enabled: root.visible
            onTriggered: root.menuCommands.aboutRequested()
        }
        AppMenuAction {
            text: UiText.text("dialog.preferences.title")
            enabled: root.visible
            onTriggered: root.menuCommands.preferencesRequested()
        }
        AppMenuSeparator {}
        AppMenuAction {
            text: UiText.text("退出")
            shortcut: StandardKey.Quit
            shortcutText: root.shortcuts.standardDisplayText(StandardKey.Quit)
            enabled: root.visible
            onTriggered: root.menuCommands.exitRequested()
        }
    }

    Item {
        id: menuHost
        anchors.left: brand.right
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height
        width: root.useEmbeddedMenu && mainMenuLoader.item ? mainMenuLoader.item.width : 0
        clip: true
        z: 2

        Loader {
            id: mainMenuLoader
            active: root.useEmbeddedMenu
            anchors.left: parent.left
            anchors.verticalCenter: parent.verticalCenter
            height: parent.height
            sourceComponent: MainMenu {
                height: menuHost.height
                availableWidth: root.menuAvailableWidth
                commands: root.menuCommands
                shortcuts: root.shortcuts
                documentSession: root.documentSession
                commandsEnabled: root.visible
                normalizationEnabled: root.normalizationEnabled
            }
            onLoaded: root.scheduleMainMenuReflow()
        }
    }

    Text {
        id: titleLabel
        anchors.centerIn: parent
        width: root.titleBandMax
        z: 1
        text: root.documentTitle
        color: Theme.colors.text.chrome
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        // Let drag / menu hit-testing win under the label.
        enabled: false

        onImplicitWidthChanged: root.scheduleMainMenuReflow()
    }

    WindowCaptionButtons {
        id: captionButtons
        anchors.right: parent.right
        z: 2
        visible: root.useCaptionButtons
        width: root.useCaptionButtons ? implicitWidth : 0
        hostWindow: root.hostWindow
    }
}
