import QtQuick
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
    readonly property real menuLeft: brand.x + brand.width + 12
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

    Row {
        id: brand
        anchors.left: parent.left
        anchors.leftMargin: 10 + root.leadingInset
        anchors.verticalCenter: parent.verticalCenter
        // Font ascent makes glyphs look high; nudge down for optical center.
        anchors.verticalCenterOffset: 1
        spacing: 7
        z: 2

        Image {
            width: 18
            height: 18
            anchors.verticalCenter: parent.verticalCenter
            source: Qt.resolvedUrl("icons/app.png")
            sourceSize: Qt.size(18, 18)
            smooth: true
        }

        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: "MiaCode"
            color: Theme.colors.text.chrome
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            font.bold: true
        }
    }

    Item {
        id: menuHost
        anchors.left: brand.right
        anchors.leftMargin: 12
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
