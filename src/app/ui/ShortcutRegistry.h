#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>

class QAction;
class QShortcut;

class ShortcutRegistry
{
public:
    struct ShortcutDefinition {
        QString id;
        QString labelZh;
        QString labelEn;
        QList<QKeySequence> defaultSequences;
    };

    static ShortcutRegistry& instance();

    QKeySequence sequence(const QString& id, const QKeySequence& fallback = QKeySequence()) const;
    QList<QKeySequence> sequences(const QString& id, const QList<QKeySequence>& fallback = {}) const;
    QList<ShortcutDefinition> editableShortcuts() const;
    QList<QKeySequence> defaultSequences(const QString& id) const;

    void applyShortcut(QAction* action, const QString& id, const QKeySequence& fallback = QKeySequence()) const;
    void applyShortcuts(QAction* action, const QString& id, const QList<QKeySequence>& fallback = {}) const;
    void applyShortcut(QShortcut* shortcut, const QString& id, const QKeySequence& fallback = QKeySequence()) const;
    bool setUserShortcut(const QString& id, const QKeySequence& sequence);
    bool resetUserShortcut(const QString& id);
    bool resetEditableShortcuts();
    void reload();

private:
    ShortcutRegistry();

    void loadDefaults();
    void loadOverrideFile(const QString& path);
    void mergeJsonBytes(const QByteArray& bytes);
    bool saveUserOverrides() const;

    QHash<QString, ShortcutDefinition> definitions_;
    QHash<QString, QList<QKeySequence>> defaultShortcuts_;
    QHash<QString, QList<QKeySequence>> shortcuts_;
    QHash<QString, QList<QKeySequence>> userOverrides_;
};
