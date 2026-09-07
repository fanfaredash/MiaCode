import QtQuick
import MiaCode.UI

// The playback-rate HUD v1 drew over the preview: 当前倍速 + the rate as a
// percentage, held briefly and faded out. It is the only feedback a rate change
// made from the keyboard has — the transport's rate button is small, can be
// off-screen in fullscreen, and is not where the user is looking either way.
//
// It centres on whatever it is anchored to. In the workspace that is the
// timeline panel, where the eyes already are while editing and where nothing is
// obscured; fullscreen preview anchors it to the stage, having no timeline.
//
// Overlay only: it declares no input handler, so clicks and drags reach
// whatever is underneath. Mount it AFTER the content it covers — QML stacking
// is declaration order.
Item {
    id: root

    required property var previewSession
    // 900ms hold then a 240ms fade, matching the v1 timings.
    property int holdMilliseconds: 900

    readonly property real rate: root.previewSession ? root.previewSession.rate : 1
    // Coerced once: the theme stores these as strings, which have no channels.
    readonly property color hudText: Theme.colors.previewHud.text
    readonly property color hudPlate: Theme.colors.previewHud.shadow
    property int percent: 100
    property bool showing: false
    // The first evaluation of `rate` is the session's current speed, not a
    // change anyone asked for. Announcing it would flash the HUD every time the
    // pane is built — on page switches, on entering fullscreen.
    property bool armed: false

    onPreviewSessionChanged: root.armed = false

    onRateChanged: {
        const next = Math.round(root.rate * 100)
        if (!root.armed) {
            root.armed = true
            root.percent = next
            return
        }
        if (next === root.percent)
            return
        root.percent = next
        root.showing = true
        hideTimer.restart()
    }

    Timer {
        id: hideTimer
        interval: root.holdMilliseconds
        onTriggered: root.showing = false
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        // Never wider than the stage it floats on: the preview pane can be
        // dragged narrower than the HUD's natural size, and the stage clips.
        width: Math.min(Math.max(1, root.width - 16), Math.max(196, body.implicitWidth + 48))
        height: Math.min(Math.max(1, root.height - 16), Math.max(96, body.implicitHeight + 36))
        radius: 18
        // previewHud is the theme's contrast pair for chrome drawn over the
        // stage: an opaque-enough plate in both themes, with text that reads
        // against it. Ordinary panel colors would vanish into the canvas.
        color: root.hudPlate
        border.width: 1
        border.color: Qt.rgba(root.hudText.r, root.hudText.g, root.hudText.b, 0.22)
        opacity: root.showing ? 1 : 0
        visible: opacity > 0

        Behavior on opacity {
            NumberAnimation { duration: 240; easing.type: Easing.OutCubic }
        }

        Column {
            id: body
            anchors.centerIn: parent
            spacing: 6

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: UiText.text("timeline.playback_speed")
                color: root.hudText
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize + 1
                font.weight: Font.DemiBold
                horizontalAlignment: Text.AlignHCenter
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                text: root.percent + "%"
                color: root.hudText
                font.family: Theme.uiFont
                font.pixelSize: Theme.uiFontSize * 2
                font.weight: Font.Bold
                horizontalAlignment: Text.AlignHCenter
            }
        }
    }
}
