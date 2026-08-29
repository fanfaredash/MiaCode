import QtQuick
import QtQuick.Layouts
import MiaCode.UI

// Label + slider + read-out, the other settings-form row. `moved` fires on user
// drags only, so a page can write straight through without echoing its own
// writes back into the handle.
RowLayout {
    id: root

    required property string label
    property int labelWidth: 120
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property real value: 0
    property string suffix: "%"
    property string readout: Math.round(root.value) + root.suffix
    signal moved(real value)
    // Held handles matter to some pages (auditioning waits for the release), so
    // the state is published rather than kept inside.
    property alias pressed: slider.pressed

    Layout.fillWidth: true

    Text {
        Layout.preferredWidth: root.labelWidth
        text: root.label
        color: Theme.colors.text.secondary
        font.family: Theme.uiFont
        wrapMode: Text.WordWrap
    }
    AppSlider {
        id: slider
        Layout.fillWidth: true
        from: root.from
        to: root.to
        stepSize: root.stepSize
        onMoved: root.moved(value)

        // A drag writes `value` imperatively and would kill a plain binding for
        // good; a Binding re-applies, and stands down while the handle is held
        // so it cannot fight the drag.
        Binding {
            target: slider
            property: "value"
            value: root.value
            when: !slider.pressed
            restoreMode: Binding.RestoreNone
        }
    }
    Text {
        Layout.preferredWidth: 48
        horizontalAlignment: Text.AlignRight
        text: root.readout
        color: Theme.colors.text.active
        font.family: Theme.uiFont
    }
}
