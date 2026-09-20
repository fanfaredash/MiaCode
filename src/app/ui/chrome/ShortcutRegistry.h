#pragma once

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QString>
#include <QStringList>

namespace miacode::ui {

class ShortcutRegistry
{
public:
    struct ShortcutDefinition {
        QString id;
        QString labelKey;
        QString labelZh;
        QString labelEn;
        QKeySequence defaultSequence;
        QString defaultShortcutText;
    };

    static ShortcutRegistry& instance();

    QKeySequence sequence(const QString& id, const QKeySequence& fallback = QKeySequence()) const;
    QString shortcutText(const QString& id, const QString& fallback = QString()) const;
    QList<ShortcutDefinition> editableShortcuts() const;
    QKeySequence defaultSequence(const QString& id) const;
    QString defaultShortcutText(const QString& id) const;

    bool registerExtensionShortcut(
        const QString& id,
        const QString& label,
        const QKeySequence& defaultSequence);
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
    QHash<QString, QKeySequence> defaultShortcuts_;
    QHash<QString, QKeySequence> shortcuts_;
    QHash<QString, QString> defaultShortcutTexts_;
    QHash<QString, QString> shortcutTexts_;
    QHash<QString, QString> userOverrides_;
    QStringList editableShortcutIds_;
};
} // namespace miacode::ui
