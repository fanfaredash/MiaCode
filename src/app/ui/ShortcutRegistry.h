#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>

class ShortcutRegistry
{
public:
    struct ShortcutDefinition {
        QString id;
        QString labelKey;
        QString labelZh;
        QString labelEn;
        QList<QKeySequence> defaultSequences;
        QStringList defaultShortcutTexts;
    };

    static ShortcutRegistry& instance();

    QKeySequence sequence(const QString& id, const QKeySequence& fallback = QKeySequence()) const;
    QList<QKeySequence> sequences(const QString& id, const QList<QKeySequence>& fallback = {}) const;
    QStringList shortcutTexts(const QString& id, const QStringList& fallback = {}) const;
    QList<ShortcutDefinition> editableShortcuts() const;
    QList<QKeySequence> defaultSequences(const QString& id) const;
    QStringList defaultShortcutTexts(const QString& id) const;

    bool registerExtensionShortcut(
        const QString& id,
        const QString& label,
        const QList<QKeySequence>& defaultSequences);
    bool setUserShortcut(const QString& id, const QKeySequence& sequence);
    bool setUserShortcutText(const QString& id, const QString& shortcutText);
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
    QHash<QString, QStringList> defaultShortcutTexts_;
    QHash<QString, QStringList> shortcutTexts_;
    QHash<QString, QStringList> userOverrides_;
};
