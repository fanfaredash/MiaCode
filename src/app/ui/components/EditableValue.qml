import QtQuick
import QtQuick.Controls
import MiaCode.UI

// A value read-out ("50%", "3.25", "14 pt") that a double-click turns into an
// inline editor, so a value can be typed instead of dragged. v1's
// EditableValueLabel did the same beside its sliders; typing is the only way to
// hit an exact number on a short handle.
//
// The editor occupies exactly the read-out's rectangle, so nothing around it
// moves. Committing reports a value already clamped to [from, to] and snapped
// to the step grid — callers apply it the same way they apply a drag.
Item {
    id: root

    // What the resting state shows. Carries the unit; the editor strips it.
    property string text: ""
    // The number the editor starts from, and what the grid is measured against.
    property real value: 0
    property real from: 0
    property real to: 100
    property real stepSize: 1
    property int decimals: 0
    property int horizontalAlignment: Text.AlignRight
    property color color: Theme.colors.text.active

    signal committed(real value)

    readonly property bool editing: editor.visible

    implicitWidth: Math.max(label.implicitWidth, 40)
    implicitHeight: Math.max(label.implicitHeight, 20)

    function beginEdit() {
        editor.text = root.value.toFixed(root.decimals)
        editor.visible = true
        editor.forceActiveFocus()
        editor.selectAll()
    }

    function commitEdit() {
        if (!editor.visible)
            return
        const typed = parseFloat(editor.text)
        editor.visible = false
        if (isNaN(typed))
            return
        const clamped = Math.min(root.to, Math.max(root.from, typed))
        const steps = Math.round((clamped - root.from) / root.stepSize)
        root.committed(Math.min(root.to, root.from + steps * root.stepSize))
    }

    Text {
        id: label
        anchors.fill: parent
        visible: !editor.visible
        text: root.text
        color: root.color
        horizontalAlignment: root.horizontalAlignment
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    MouseArea {
        anchors.fill: parent
        enabled: !editor.visible
        cursorShape: Qt.IBeamCursor
        onDoubleClicked: root.beginEdit()
    }

    AppTextField {
        id: editor
        anchors.fill: parent
        visible: false
        // The read-out's slot is narrow; the form-field paddings would leave no
        // room for the digits.
        leftPadding: 4
        rightPadding: 4
        topPadding: 0
        bottomPadding: 0
        implicitHeight: root.height
        horizontalAlignment: root.horizontalAlignment
        validator: DoubleValidator {
            bottom: root.from
            top: root.to
            decimals: root.decimals
            notation: DoubleValidator.StandardNotation
        }
        onAccepted: root.commitEdit()
        // Clicking away is a commit, not a cancel — the same as leaving v1's
        // inline editor. Escape is the cancel, and hides the field first so
        // losing focus afterwards has nothing left to commit.
        onActiveFocusChanged: {
            if (!activeFocus)
                root.commitEdit()
        }
        Keys.onEscapePressed: function(event) {
            editor.visible = false
            event.accepted = true
        }
    }
}
