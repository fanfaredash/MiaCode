import QtQuick
import MiaCode.UI

Rectangle {
    id: root

    required property var hostWindow
    property string documentTitle: ""
    signal toggleSidebarRequested()
    signal toggleBottomPanelRequested()
    signal togglePreviewRequested()
    signal exitRequested()
    signal undoRequested()
    signal redoRequested()
    signal selectAllRequested()
    signal validateRequested()
    signal metadataRequested()
    signal openRequested()
    signal saveRequested()
    signal saveAsRequested()

    property bool canUndo: false
    property bool canRedo: false

    implicitHeight: 34
    color: Theme.colors.background.workbench

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
    readonly property real menuAvailableWidth: Math.max(0, titleBandLeft - menuGap - menuLeft)

    onWidthChanged: mainMenu.scheduleReflow()
    onMenuAvailableWidthChanged: mainMenu.scheduleReflow()
    onDocumentTitleChanged: mainMenu.scheduleReflow()

    Row {
        id: brand
        anchors.left: parent.left
        anchors.leftMargin: 10
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
            color: Theme.colors.text.secondary
            font.family: Theme.uiFont
            font.pixelSize: Theme.uiFontSize
            font.bold: true
        }
    }

    MainMenu {
        id: mainMenu
        anchors.left: brand.right
        anchors.leftMargin: 12
        anchors.verticalCenter: parent.verticalCenter
        height: parent.height
        z: 2
        availableWidth: root.menuAvailableWidth
        commandsEnabled: root.visible
        canUndo: root.canUndo
        canRedo: root.canRedo
        onToggleSidebarRequested: root.toggleSidebarRequested()
        onToggleBottomPanelRequested: root.toggleBottomPanelRequested()
        onTogglePreviewRequested: root.togglePreviewRequested()
        onExitRequested: root.exitRequested()
        onUndoRequested: root.undoRequested()
        onRedoRequested: root.redoRequested()
        onSelectAllRequested: root.selectAllRequested()
        onValidateRequested: root.validateRequested()
        onMetadataRequested: root.metadataRequested()
        onOpenRequested: root.openRequested()
        onSaveRequested: root.saveRequested()
        onSaveAsRequested: root.saveAsRequested()
    }

    Item {
        id: dragArea
        anchors.left: mainMenu.right
        anchors.right: captionButtons.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        z: 0

        DragHandler {
            target: null
            acceptedButtons: Qt.LeftButton
            onActiveChanged: {
                if (active)
                    root.hostWindow.startSystemMove()
            }
        }

        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: {
                if (root.hostWindow.visibility === Window.Maximized)
                    root.hostWindow.showNormal()
                else
                    root.hostWindow.showMaximized()
            }
        }
    }

    Text {
        id: titleLabel
        anchors.centerIn: parent
        width: root.titleBandMax
        z: 1
        text: root.documentTitle
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        // Let drag / menu hit-testing win under the label.
        enabled: false

        onImplicitWidthChanged: mainMenu.scheduleReflow()
    }

    WindowCaptionButtons {
        id: captionButtons
        anchors.right: parent.right
        z: 2
        visible: Qt.platform.os === "windows"
        hostWindow: root.hostWindow
    }

    Rectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 1
        color: Theme.colors.border.normal
    }
}
