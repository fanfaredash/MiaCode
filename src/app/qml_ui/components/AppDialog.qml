import QtQuick
import QtQuick.Controls
import MiaCode.UI

// Window-relative geometry and shared chrome for all application dialogs.
Dialog {
    id: root

    property real preferredWidth: 460
    property real preferredHeight: Theme.dialogCompactHeight
    property Item body
    property bool fillBody: false

    parent: Overlay.overlay
    popupType: Popup.Item
    modal: true
    focus: true
    closePolicy: Popup.CloseOnEscape
    margins: Theme.dialogMargin
    leftPadding: Theme.dialogPadding
    rightPadding: Theme.dialogPadding
    topPadding: Theme.panelPadding
    bottomPadding: Theme.panelPadding
    spacing: 0
    font.family: Theme.uiFont
    font.pixelSize: Theme.uiFontSize

    width: Math.min(preferredWidth, Math.max(0, parent.width - 2 * margins))
    height: Math.min(preferredHeight, Math.max(0, parent.height - 2 * margins))
    anchors.centerIn: parent

    property bool closing: false
    enter: FadeTransition {
        id: enterTransition
    }
    exit: FadeTransition {
        appearing: false
        initialOpacity: root.opacity
    }
    Overlay.modal: Rectangle {
        color: Theme.modalScrimColor
        opacity: root.opacity
    }
    onAboutToShow: {
        enterTransition.initialOpacity = root.closing ? root.opacity : 0
        root.closing = false
    }
    onAboutToHide: root.closing = true
    onClosed: root.closing = false

    background: FloatingCard {
        popup: root
        tintColor: Theme.dialogTintColor
        blurRadius: Theme.dialogBlurRadius
        shadowBlur: 0.68
        shadowOpacity: Theme.dialogShadowOpacity
        shadowVerticalOffset: 3
    }

    header: Label {
        text: root.title
        visible: root.title.length > 0
        color: Theme.colors.text.heading
        elide: Text.ElideRight
        font.pixelSize: Theme.headingFontSize
        font.weight: Font.DemiBold
        leftPadding: Theme.dialogPadding
        rightPadding: Theme.dialogPadding
        topPadding: Theme.dialogPadding
        bottomPadding: Theme.panelPadding
    }

    contentItem: ScrollView {
        id: viewport
        clip: true
        implicitHeight: root.body ? root.body.implicitHeight : 0
        contentWidth: availableWidth
        contentHeight: root.body ? root.body.height : 0
        contentData: root.body ? [root.body] : []
        ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
        ScrollBar.vertical: AppScrollBar {}

        readonly property Binding bodyWidth: Binding {
            target: root.body
            property: "width"
            value: viewport.availableWidth
        }
        readonly property Binding bodyHeight: Binding {
            target: root.body
            property: "height"
            value: Math.max(root.fillBody ? viewport.availableHeight : 0,
                            root.body ? root.body.implicitHeight : 0)
        }
    }
}
