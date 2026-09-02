import QtQuick
import QtQuick.Layouts
import MiaCode.UI

// Label + dropdown, the settings-form row every settings page uses. `options`
// is a list of { value, label }; `picked` carries the option's value, not its
// index, so callers never have to keep the two lists in step.
RowLayout {
    id: root

    required property string label
    property var options: []
    property var currentValue
    property int labelWidth: 120
    signal picked(var value)

    function indexOfValue(value) {
        for (let i = 0; i < root.options.length; ++i) {
            if (root.options[i].value === value)
                return i
        }
        return 0
    }

    Layout.fillWidth: true

    Text {
        Layout.preferredWidth: root.labelWidth
        text: root.label
        color: Theme.colors.text.secondary
        wrapMode: Text.WordWrap
    }
    AppComboBox {
        Layout.fillWidth: true
        textRole: "label"
        model: root.options
        currentIndex: root.indexOfValue(root.currentValue)
        onActivated: function(index) { root.picked(root.options[index].value) }
    }
}
