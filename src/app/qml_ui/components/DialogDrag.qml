import QtQuick

// Makes a Dialog draggable by its own title bar, without changing how it looks.
//
// Settings panels change what the preview shows, and a panel that cannot be
// moved aside hides the thing being judged. Rather than restyle the header,
// this parents a transparent grab area onto the one the style already draws,
// so the dialog is unchanged until someone drags it.
//
// Declare it inside the Dialog: `DialogDrag { dialog: root }`. It is not an
// Item, so it never lands in the dialog's content layout.
QtObject {
    id: root

    required property var dialog

    // Centring is an anchor, so it and a dragged position cannot both hold. The
    // anchor is released the first time the dialog is moved, and the position
    // then survives reopening — having to re-drag the panel on every open would
    // defeat the point of moving it.
    property bool moved: false
    property var handle: null

    function clampIntoOverlay() {
        if (!root.moved || !root.dialog || !root.dialog.parent)
            return
        const overlay = root.dialog.parent
        root.dialog.x = Math.max(0, Math.min(overlay.width - root.dialog.width, root.dialog.x))
        root.dialog.y = Math.max(0, Math.min(overlay.height - root.dialog.height, root.dialog.y))
    }

    // Controls defers header/footer/background creation, so the header can
    // still be absent when this object completes. Opening the dialog has
    // certainly materialised it, which is why the attach is retried here.
    function attachHandle() {
        if (root.handle || !root.dialog || !root.dialog.header)
            return
        root.handle = root.handleComponent.createObject(root.dialog.header)
    }

    property Connections showGuard: Connections {
        target: root.dialog
        function onAboutToShow() {
            root.attachHandle()
            root.clampIntoOverlay()
        }
    }

    property Component handleComponent: Component {
        MouseArea {
            property real grabX: 0
            property real grabY: 0
            anchors.fill: parent
            cursorShape: Qt.SizeAllCursor
            onPressed: function(mouse) {
                if (!root.moved) {
                    // Freeze where the anchor had put it, then take over.
                    const centred = root.dialog.anchors.centerIn
                    root.dialog.anchors.centerIn = null
                    if (centred) {
                        root.dialog.x = Math.round((centred.width - root.dialog.width) / 2)
                        root.dialog.y = Math.round((centred.height - root.dialog.height) / 2)
                    }
                    root.moved = true
                }
                grabX = mouse.x
                grabY = mouse.y
            }
            onPositionChanged: function(mouse) {
                if (!pressed)
                    return
                root.dialog.x += mouse.x - grabX
                root.dialog.y += mouse.y - grabY
                root.clampIntoOverlay()
            }
        }
    }

    Component.onCompleted: root.attachHandle()
}
