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
    required property var pet
    property string documentTitle: ""
    property real leadingInset: 0
    property bool saveEnabled: true
    property bool wholeDocumentSaveEnabled: true
    property bool documentAvailable: true
    property bool editorCommandsEnabled: true
    property bool chartCommandsEnabled: true
    property bool toolCommandsEnabled: true
    property bool normalizationEnabled: true
    property real nativeHeight: 0
    property real leadingToolAreaWidth: 0
    property real trailingToolAreaWidth: 0

    readonly property bool useEmbeddedMenu: root.platform.embeddedMenuInTitleBar
    readonly property bool useNativeMenu: root.platform.nativeMenuBar
    readonly property bool useCaptionButtons: root.platform.captionButtons
    readonly property real brandContentPadding: Theme.chromePadding
    readonly property real brandLeadingMargin:
        (root.leadingInset > 0 ? root.leadingInset : 10) - brandContentPadding

    implicitHeight: root.useNativeMenu && root.nativeHeight > 0
                    ? root.nativeHeight
                    : 32
    color: Theme.surfaceColor(Theme.colors.background.titleBar)

    // 标题以窗口中心为轴，左右留白取菜单与窗口按钮所需空间的较大值。
    readonly property real menuGap: 16
    readonly property real minimumMenuWidth: root.useEmbeddedMenu
        ? (mainMenuLoader.item ? mainMenuLoader.item.overflowButtonWidth : 30)
        : 0
    readonly property real mainMenuFullWidth: root.useEmbeddedMenu && mainMenuLoader.item
        ? mainMenuLoader.item.fullWidth : 0
    readonly property real brandCollapsedWidth: Theme.titleBarBrandIconSize + 2 * root.brandContentPadding
    readonly property real brandFullWidth: Theme.titleBarBrandIconSize + 7
        + (brandText ? Math.ceil(brandText.implicitWidth) : 0) + 2 * root.brandContentPadding
    readonly property real minimumLeftMargin: root.useNativeMenu
        ? root.leadingToolAreaWidth + menuGap
        : root.brandLeadingMargin + brandCollapsedWidth + minimumMenuWidth + menuGap
    readonly property real minimumRightMargin: root.useNativeMenu
        ? root.trailingToolAreaWidth + menuGap
        : captionButtons.width + menuGap
    // 按标题文字宽度预留居中区域，两侧至少预留最小边距保证居中对称。
    readonly property real preferredTitleWidth: Math.min(titleLabel.implicitWidth, Math.max(0,
        width - 2 * Math.max(minimumLeftMargin, minimumRightMargin)))
    // 居中标题左侧提供给菜单与图标的可用空间。空间紧缩时优先折叠品牌文字，再折叠后续菜单项。
    readonly property real availableMenuAreaWidth: Math.max(0,
        (width - preferredTitleWidth) / 2 - menuGap - root.brandLeadingMargin)
    readonly property bool brandTextVisible: !root.useEmbeddedMenu
        || (availableMenuAreaWidth >= brandFullWidth + mainMenuFullWidth)
    readonly property real menuLeft: brand.x + brand.width
    readonly property real menuAvailableWidth: root.useEmbeddedMenu
        ? Math.max(minimumMenuWidth, (width - preferredTitleWidth) / 2 - menuGap - menuLeft)
        : 0
    readonly property real titleAreaLeft: root.useNativeMenu
        ? root.leadingToolAreaWidth + menuGap
        : menuHost.x + menuHost.width + menuGap
    readonly property real titleAreaRight: root.useNativeMenu
        ? width - root.trailingToolAreaWidth - menuGap
        : width - captionButtons.width - menuGap
    readonly property real titleAvailableWidth: Math.max(0,
        2 * Math.min(width / 2 - titleAreaLeft, titleAreaRight - width / 2))

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
        visible: !root.useNativeMenu

        implicitWidth: visible
            ? (root.brandTextVisible ? root.brandFullWidth : root.brandCollapsedWidth)
            : 0

        contentItem: Row {
            id: brandContent
            spacing: brandText.visible ? 7 : 0

            Image {
                width: Theme.titleBarBrandIconSize
                height: Theme.titleBarBrandIconSize
                anchors.verticalCenter: parent.verticalCenter
                source: Qt.resolvedUrl("icons/app-titlebar.png")
                smooth: true
            }

            Text {
                id: brandText
                anchors.verticalCenter: parent.verticalCenter
                visible: root.brandTextVisible
                text: "MiaCode"
                color: brand.hovered || brandMenu.active
                       ? Theme.colors.text.active : Theme.colors.text.chrome
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize
                font.bold: true
            }
        }

        Tooltip {
            visible: brand.hovered && !root.brandTextVisible && !brandMenu.active
            text: "MiaCode"
        }

        onClicked: {
            if (mainMenuLoader.item)
                mainMenuLoader.item.toggleAnchoredMenu(brandMenu, brand)
            else if (brandMenu.active)
                brandMenu.close()
            else
                brandMenu.popup(brand, 0, brand.height)
        }
        onHoveredChanged: {
            if (hovered && mainMenuLoader.item)
                mainMenuLoader.item.hoverAnchoredMenu(brandMenu, brand)
        }
    }

    AppMenu {
        id: brandMenu
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutsideParent

        AppMenuAction {
            text: qsTrId("qml.about_miacode")
            enabled: root.visible && !root.useNativeMenu
            onTriggered: root.menuCommands.aboutRequested()
        }
        AppMenuAction {
            text: qsTrId("dialog.preferences.title")
            enabled: root.visible && !root.useNativeMenu
            onTriggered: root.menuCommands.preferencesRequested()
        }
        AppMenuSeparator {}
        AppMenuAction {
            text: qsTrId("shortcut.file.quit")
            shortcut: StandardKey.Quit
            shortcutText: root.shortcuts.standardDisplayText(StandardKey.Quit)
            enabled: root.visible && !root.useNativeMenu
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
                pet: root.pet
                commandsEnabled: root.visible
                saveEnabled: root.saveEnabled
                wholeDocumentSaveEnabled: root.wholeDocumentSaveEnabled
                documentAvailable: root.documentAvailable
                editorCommandsEnabled: root.editorCommandsEnabled
                chartCommandsEnabled: root.chartCommandsEnabled
                toolCommandsEnabled: root.toolCommandsEnabled
                normalizationEnabled: root.normalizationEnabled
            }
        }
    }

    Text {
        id: titleLabel
        anchors.centerIn: parent
        width: Math.min(implicitWidth, root.titleAvailableWidth)
        z: 1
        text: (root.documentSession.dirty ? "* " : "") + root.documentTitle
        color: Theme.colors.text.chrome
        font.family: Theme.uiFont
        font.pixelSize: Theme.uiFontSize
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        HoverHandler { id: titleHover }
        Tooltip {
            visible: titleHover.hovered && titleLabel.truncated
            text: titleLabel.text
        }
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
