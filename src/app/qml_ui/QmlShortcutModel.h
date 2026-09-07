#pragma once

#include <QObject>
#include <QVariantList>
#include <QString>

namespace miacode::qml_ui {

// QML's view of ShortcutRegistry. The registry itself is QtCore + QtGui only —
// its QtWidgets-looking helpers take QAction / QShortcut, both of which moved to
// QtGui in Qt 6 — so v2 reuses it rather than growing a second binding table
// that would drift from the user's shortcuts.json and from v1.
//
// Two spellings of the same binding are exposed because they are not
// interchangeable: QML `Shortcut.sequence` parses portable text ("Ctrl+Z"),
// while a menu row must display the platform spelling ("⌘Z" on macOS).
class QmlShortcutModel final : public QObject
{
    Q_OBJECT
    // Bumped when the registry reloads, so every QML binding that resolved a
    // sequence re-evaluates after the user edits their shortcuts.
    Q_PROPERTY(qulonglong revision READ revision NOTIFY revisionChanged)

public:
    explicit QmlShortcutModel(QObject* parent = nullptr);

    qulonglong revision() const;

    // Portable text for `Shortcut { sequence: ... }`. Empty when the id is
    // unknown and no fallback is given, which leaves the Shortcut inert rather
    // than binding something arbitrary.
    Q_INVOKABLE QString sequence(const QString& id, const QString& fallback = QString()) const;
    // Platform spelling for menu rows and tooltips.
    Q_INVOKABLE QString displayText(const QString& id, const QString& fallback = QString()) const;
    // Menu rows whose binding is a Qt standard key (Open/Save/Undo/...) rather
    // than a registry id still need the platform spelling to display.
    Q_INVOKABLE QString standardDisplayText(int standardKey) const;
    Q_INVOKABLE void reload();

    // Shortcut editing. editableShortcuts() returns one row per command:
    // { id, label, shortcutText, defaultText, isDefault }.
    Q_INVOKABLE QVariantList editableShortcuts() const;
    Q_INVOKABLE bool setShortcutText(const QString& id, const QString& shortcutText);
    Q_INVOKABLE void resetShortcut(const QString& id);
    Q_INVOKABLE void resetAllShortcuts();
    // Portable name for one key code, so QML can compose a binding string
    // without knowing Qt's key-name table.
    Q_INVOKABLE QString keyName(int key) const;

signals:
    void revisionChanged();

private:
    qulonglong revision_ = 1;
};

} // namespace miacode::qml_ui
