import QtQuick.Controls

// A menu Action that also carries the platform spelling of its binding.
// MenuItem has no `shortcut` property — only Action does, and its value is a
// QKeySequence/StandardKey that a row cannot render — so the row reads this
// instead. AppMenuItem picks it up through `action.shortcutText`.
Action {
    property string shortcutText: ""
}
