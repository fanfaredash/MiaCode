#include "ShortcutRegistry.h"

#include <QAction>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QShortcut>

namespace {

const QStringList& editableShortcutIds()
{
    static const QStringList ids{
        QStringLiteral("transform.mirror_lr"),
        QStringLiteral("transform.mirror_ud"),
        QStringLiteral("transform.rotate_180"),
        QStringLiteral("transform.rotate_ccw_45"),
        QStringLiteral("transform.rotate_cw_45"),
        QStringLiteral("transform.subdivision_up"),
        QStringLiteral("transform.subdivision_down"),
        QStringLiteral("transform.subdivision_half_up"),
        QStringLiteral("transform.subdivision_half_down"),
        QStringLiteral("transform.toggle_break"),
        QStringLiteral("transform.toggle_ex"),
        QStringLiteral("transform.toggle_firework"),
        QStringLiteral("transform.random_rotate"),
        QStringLiteral("transform.clear_complete_elements"),
        QStringLiteral("preview.stop_or_play"),
        QStringLiteral("preview.play_pause_global"),
        QStringLiteral("preview.speed_down"),
        QStringLiteral("preview.speed_up"),
        QStringLiteral("preview.pause_display_hold"),
        QStringLiteral("editor.font_decrease"),
        QStringLiteral("editor.font_increase"),
        QStringLiteral("editor.overwrite_mode"),
    };
    return ids;
}

QString userOverridePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("shortcuts.json"));
}

QList<QKeySequence> parseSequenceValue(const QJsonValue& value)
{
    QList<QKeySequence> sequences;
    const auto appendSequence = [&sequences](const QString& text) {
        const QString trimmed = text.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        const QKeySequence sequence(trimmed);
        if (!sequence.isEmpty()) {
            sequences.append(sequence);
        }
    };

    if (value.isString()) {
        appendSequence(value.toString());
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue& entry : array) {
            if (entry.isString()) {
                appendSequence(entry.toString());
            }
        }
    }
    return sequences;
}

QList<QKeySequence> parseShortcutObject(const QJsonObject& object)
{
    if (object.contains(QStringLiteral("shortcut"))) {
        return parseSequenceValue(object.value(QStringLiteral("shortcut")));
    }
    return parseSequenceValue(object.value(QStringLiteral("default")));
}

QJsonValue sequenceJsonValue(const QList<QKeySequence>& sequences)
{
    if (sequences.size() == 1) {
        return sequences.constFirst().toString(QKeySequence::PortableText);
    }
    QJsonArray array;
    for (const QKeySequence& sequence : sequences) {
        if (!sequence.isEmpty()) {
            array.append(sequence.toString(QKeySequence::PortableText));
        }
    }
    return array;
}

QList<QKeySequence> runtimeSequencesForAction(const QList<QKeySequence>& sequences)
{
    QList<QKeySequence> expanded;
    const auto appendUnique = [&expanded](const QKeySequence& sequence) {
        if (!sequence.isEmpty() && !expanded.contains(sequence)) {
            expanded.append(sequence);
        }
    };
    for (const QKeySequence& sequence : sequences) {
        appendUnique(sequence);
        const QString portable = sequence.toString(QKeySequence::PortableText);
        if (portable == QStringLiteral("Ctrl+Shift+=")) {
            appendUnique(QKeySequence(QStringLiteral("Ctrl++")));
        } else if (portable == QStringLiteral("Ctrl++")) {
            appendUnique(QKeySequence(QStringLiteral("Ctrl+Shift+=")));
        } else if (portable == QStringLiteral("Ctrl+Shift+-")) {
            appendUnique(QKeySequence(QStringLiteral("Ctrl+_")));
            appendUnique(QKeySequence(QStringLiteral("Ctrl+Shift+_")));
        } else if (portable == QStringLiteral("Ctrl+_") || portable == QStringLiteral("Ctrl+Shift+_")) {
            appendUnique(QKeySequence(QStringLiteral("Ctrl+Shift+-")));
        }
    }
    return expanded;
}

}  // namespace

ShortcutRegistry& ShortcutRegistry::instance()
{
    static ShortcutRegistry registry;
    return registry;
}

ShortcutRegistry::ShortcutRegistry()
{
    reload();
}

void ShortcutRegistry::reload()
{
    definitions_.clear();
    defaultShortcuts_.clear();
    shortcuts_.clear();
    userOverrides_.clear();

    loadDefaults();

    const QString appOverride = userOverridePath();
    loadOverrideFile(appOverride);
    const QString cwdOverride =
        QDir(QDir::currentPath()).filePath(QStringLiteral("shortcuts.json"));
    if (QDir::cleanPath(cwdOverride) != QDir::cleanPath(appOverride)) {
        loadOverrideFile(cwdOverride);
    }
}

QKeySequence ShortcutRegistry::sequence(const QString& id, const QKeySequence& fallback) const
{
    const QList<QKeySequence> matches = sequences(id, fallback.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{fallback});
    return matches.isEmpty() ? QKeySequence() : matches.constFirst();
}

QList<QKeySequence> ShortcutRegistry::sequences(
    const QString& id,
    const QList<QKeySequence>& fallback) const
{
    const QList<QKeySequence> matches = shortcuts_.value(id);
    return matches.isEmpty() ? fallback : matches;
}

QList<ShortcutRegistry::ShortcutDefinition> ShortcutRegistry::editableShortcuts() const
{
    QList<ShortcutDefinition> result;
    for (const QString& id : editableShortcutIds()) {
        const ShortcutDefinition definition = definitions_.value(id);
        if (!definition.id.isEmpty()) {
            result.append(definition);
        }
    }
    return result;
}

QList<QKeySequence> ShortcutRegistry::defaultSequences(const QString& id) const
{
    return defaultShortcuts_.value(id);
}

void ShortcutRegistry::applyShortcut(QAction* action, const QString& id, const QKeySequence& fallback) const
{
    if (action == nullptr) {
        return;
    }
    const QKeySequence matched = sequence(id, fallback);
    action->setShortcuts(runtimeSequencesForAction(matched.isEmpty() ? QList<QKeySequence>{} : QList<QKeySequence>{matched}));
}

void ShortcutRegistry::applyShortcuts(
    QAction* action,
    const QString& id,
    const QList<QKeySequence>& fallback) const
{
    if (action == nullptr) {
        return;
    }
    action->setShortcuts(runtimeSequencesForAction(sequences(id, fallback)));
}

void ShortcutRegistry::applyShortcut(QShortcut* shortcut, const QString& id, const QKeySequence& fallback) const
{
    if (shortcut == nullptr) {
        return;
    }
    shortcut->setKey(sequence(id, fallback));
}

bool ShortcutRegistry::setUserShortcut(const QString& id, const QKeySequence& sequence)
{
    if (!editableShortcutIds().contains(id) || sequence.isEmpty()) {
        return false;
    }
    const QKeySequence normalized =
        sequence.toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+Shift++")
            ? QKeySequence(QStringLiteral("Ctrl+Shift+="))
        : sequence.toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+Shift+_")
            ? QKeySequence(QStringLiteral("Ctrl+Shift+-"))
            : sequence;
    userOverrides_.insert(id, {normalized});
    if (!saveUserOverrides()) {
        return false;
    }
    reload();
    return true;
}

bool ShortcutRegistry::resetUserShortcut(const QString& id)
{
    if (!editableShortcutIds().contains(id)) {
        return false;
    }
    userOverrides_.remove(id);
    if (!saveUserOverrides()) {
        return false;
    }
    reload();
    return true;
}

bool ShortcutRegistry::resetEditableShortcuts()
{
    for (const QString& id : editableShortcutIds()) {
        userOverrides_.remove(id);
    }
    if (!saveUserOverrides()) {
        return false;
    }
    reload();
    return true;
}

void ShortcutRegistry::loadDefaults()
{
    QFile file(QStringLiteral(":/config/shortcuts.json"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    mergeJsonBytes(file.readAll());
}

void ShortcutRegistry::loadOverrideFile(const QString& path)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return;
    }
    mergeJsonBytes(file.readAll());
}

void ShortcutRegistry::mergeJsonBytes(const QByteArray& bytes)
{
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }

    const QJsonObject root = document.object();
    const QJsonObject actions = root.value(QStringLiteral("actions")).toObject();
    for (auto it = actions.constBegin(); it != actions.constEnd(); ++it) {
        const QJsonObject actionObject = it.value().toObject();
        const QList<QKeySequence> parsed = parseShortcutObject(actionObject);
        if (!parsed.isEmpty()) {
            if (!defaultShortcuts_.contains(it.key()) && actionObject.contains(QStringLiteral("default"))) {
                defaultShortcuts_.insert(it.key(), parseSequenceValue(actionObject.value(QStringLiteral("default"))));
            }
            if (editableShortcutIds().contains(it.key()) && !definitions_.contains(it.key())) {
                definitions_.insert(it.key(), {
                    it.key(),
                    actionObject.value(QStringLiteral("label_key")).toString(),
                    actionObject.value(QStringLiteral("label_zh")).toString(),
                    actionObject.value(QStringLiteral("label_en")).toString(),
                    defaultShortcuts_.value(it.key()),
                });
            }
            shortcuts_.insert(it.key(), parsed);
            if (actionObject.contains(QStringLiteral("shortcut"))) {
                userOverrides_.insert(it.key(), parsed);
            }
        }
    }

    const QJsonObject contextual = root.value(QStringLiteral("contextual")).toObject();
    for (auto it = contextual.constBegin(); it != contextual.constEnd(); ++it) {
        const QJsonObject shortcutObject = it.value().toObject();
        const QList<QKeySequence> parsed = parseShortcutObject(shortcutObject);
        if (parsed.isEmpty()) {
            continue;
        }
        if (!defaultShortcuts_.contains(it.key()) && shortcutObject.contains(QStringLiteral("default"))) {
            defaultShortcuts_.insert(it.key(), parseSequenceValue(shortcutObject.value(QStringLiteral("default"))));
        }
        if (editableShortcutIds().contains(it.key()) && !definitions_.contains(it.key())) {
            definitions_.insert(it.key(), {
                it.key(),
                shortcutObject.value(QStringLiteral("label_key")).toString(),
                shortcutObject.value(QStringLiteral("label_zh")).toString(),
                shortcutObject.value(QStringLiteral("label_en")).toString(),
                defaultShortcuts_.value(it.key()),
            });
        }
        shortcuts_.insert(it.key(), parsed);
        if (shortcutObject.contains(QStringLiteral("shortcut"))) {
            userOverrides_.insert(it.key(), parsed);
        }
    }
}

bool ShortcutRegistry::saveUserOverrides() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), 1);
    root.insert(
        QStringLiteral("notes"),
        QJsonArray{QStringLiteral("User shortcut overrides written by MiaCode preferences.")}
    );

    QJsonObject actions;
    for (const QString& id : editableShortcutIds()) {
        const QList<QKeySequence> sequences = userOverrides_.value(id);
        if (sequences.isEmpty()) {
            continue;
        }
        const ShortcutDefinition definition = definitions_.value(id);
        QJsonObject object;
        if (!definition.labelKey.isEmpty()) {
            object.insert(QStringLiteral("label_key"), definition.labelKey);
        }
        if (!definition.labelZh.isEmpty()) {
            object.insert(QStringLiteral("label_zh"), definition.labelZh);
        }
        if (!definition.labelEn.isEmpty()) {
            object.insert(QStringLiteral("label_en"), definition.labelEn);
        }
        object.insert(QStringLiteral("shortcut"), sequenceJsonValue(sequences));
        actions.insert(id, object);
    }
    root.insert(QStringLiteral("actions"), actions);

    QFile file(userOverridePath());
    const QFileInfo info(file);
    if (!info.dir().exists() && !info.dir().mkpath(QStringLiteral("."))) {
        return false;
    }
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return file.error() == QFileDevice::NoError;
}
