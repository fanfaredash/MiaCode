#include "ShortcutRegistry.h"

#include "common/InputShortcutGesture.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>

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
        QStringLiteral("timeline.zoom_in"),
        QStringLiteral("timeline.zoom_out"),
        QStringLiteral("editor.font_decrease"),
        QStringLiteral("editor.font_increase"),
        QStringLiteral("editor.overwrite_mode"),
    };
    return ids;
}

bool isEditableShortcutId(const QString& id)
{
    return editableShortcutIds().contains(id) || id.startsWith(QStringLiteral("extension."));
}

QStringList editableShortcutIdsFromDefinitions(const QHash<QString, ShortcutRegistry::ShortcutDefinition>& definitions)
{
    QStringList ids = editableShortcutIds();
    for (auto it = definitions.constBegin(); it != definitions.constEnd(); ++it) {
        if (it.key().startsWith(QStringLiteral("extension.")) && !ids.contains(it.key())) {
            ids.append(it.key());
        }
    }
    return ids;
}

QString userOverridePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("shortcuts.json"));
}

QStringList parseShortcutTextValue(const QJsonValue& value)
{
    QStringList shortcuts;
    const auto appendShortcut = [&shortcuts](const QString& text) {
        const QString normalized = miacode::input_shortcut::normalizeGestureText(text);
        if (normalized.isEmpty() || shortcuts.contains(normalized)) {
            return;
        }
        shortcuts.append(normalized);
    };

    if (value.isString()) {
        appendShortcut(value.toString());
    } else if (value.isArray()) {
        const QJsonArray array = value.toArray();
        for (const QJsonValue& entry : array) {
            if (entry.isString()) {
                appendShortcut(entry.toString());
            }
        }
    }
    return shortcuts;
}

QStringList parseShortcutObject(const QJsonObject& object)
{
    if (object.contains(QStringLiteral("shortcut"))) {
        return parseShortcutTextValue(object.value(QStringLiteral("shortcut")));
    }
    return parseShortcutTextValue(object.value(QStringLiteral("default")));
}

QJsonValue sequenceJsonValue(const QStringList& shortcuts)
{
    if (shortcuts.size() == 1) {
        return shortcuts.constFirst();
    }
    QJsonArray array;
    for (const QString& shortcut : shortcuts) {
        if (!shortcut.isEmpty()) {
            array.append(shortcut);
        }
    }
    return array;
}

QStringList shortcutTextsFromKeySequences(const QList<QKeySequence>& sequences)
{
    QStringList texts;
    for (const QKeySequence& sequence : sequences) {
        const QString text = miacode::input_shortcut::normalizeGestureText(
            sequence.toString(QKeySequence::PortableText));
        if (!text.isEmpty() && !texts.contains(text)) {
            texts.append(text);
        }
    }
    return texts;
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
    defaultShortcutTexts_.clear();
    shortcutTexts_.clear();
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

QStringList ShortcutRegistry::shortcutTexts(const QString& id, const QStringList& fallback) const
{
    const QStringList matches = shortcutTexts_.value(id);
    return matches.isEmpty() ? fallback : matches;
}

QList<ShortcutRegistry::ShortcutDefinition> ShortcutRegistry::editableShortcuts() const
{
    QList<ShortcutDefinition> result;
    for (const QString& id : editableShortcutIdsFromDefinitions(definitions_)) {
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

QStringList ShortcutRegistry::defaultShortcutTexts(const QString& id) const
{
    return defaultShortcutTexts_.value(id);
}

bool ShortcutRegistry::registerExtensionShortcut(
    const QString& id,
    const QString& label,
    const QList<QKeySequence>& defaultSequences)
{
    const QString normalizedId = id.trimmed();
    if (!normalizedId.startsWith(QStringLiteral("extension.")) || label.trimmed().isEmpty()) {
        return false;
    }
    const QList<QKeySequence> validDefaults = defaultSequences.isEmpty()
        ? QList<QKeySequence>{}
        : defaultSequences;
    const QStringList defaultTexts = shortcutTextsFromKeySequences(validDefaults);
    definitions_.insert(normalizedId, {
        normalizedId,
        QString(),
        label,
        label,
        validDefaults,
        defaultTexts,
    });
    if (!validDefaults.isEmpty()) {
        defaultShortcuts_.insert(normalizedId, validDefaults);
        defaultShortcutTexts_.insert(normalizedId, defaultTexts);
    }
    if (!userOverrides_.contains(normalizedId)) {
        shortcuts_.insert(normalizedId, validDefaults);
        shortcutTexts_.insert(normalizedId, defaultTexts);
    }
    return true;
}

bool ShortcutRegistry::setUserShortcut(const QString& id, const QKeySequence& sequence)
{
    const QKeySequence normalized =
        sequence.toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+Shift++")
            ? QKeySequence(QStringLiteral("Ctrl+Shift+="))
        : sequence.toString(QKeySequence::PortableText) == QStringLiteral("Ctrl+Shift+_")
            ? QKeySequence(QStringLiteral("Ctrl+Shift+-"))
            : sequence;
    return setUserShortcutText(id, normalized.toString(QKeySequence::PortableText));
}

bool ShortcutRegistry::setUserShortcutText(const QString& id, const QString& shortcutText)
{
    const QString normalized = miacode::input_shortcut::normalizeGestureText(shortcutText);
    if (!isEditableShortcutId(id) || normalized.isEmpty()) {
        return false;
    }
    userOverrides_.insert(id, {normalized});
    if (!saveUserOverrides()) {
        return false;
    }
    reload();
    return true;
}

bool ShortcutRegistry::resetUserShortcut(const QString& id)
{
    if (!isEditableShortcutId(id)) {
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
    for (const QString& id : editableShortcutIdsFromDefinitions(definitions_)) {
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
        const QStringList parsed = parseShortcutObject(actionObject);
        if (!parsed.isEmpty()) {
            if (!defaultShortcuts_.contains(it.key()) && actionObject.contains(QStringLiteral("default"))) {
                const QStringList defaults = parseShortcutTextValue(actionObject.value(QStringLiteral("default")));
                defaultShortcutTexts_.insert(it.key(), defaults);
                defaultShortcuts_.insert(
                    it.key(),
                    miacode::input_shortcut::keyboardSequencesFromGestureTexts(defaults));
            }
            if (isEditableShortcutId(it.key()) && !definitions_.contains(it.key())) {
                definitions_.insert(it.key(), {
                    it.key(),
                    actionObject.value(QStringLiteral("label_key")).toString(),
                    actionObject.value(QStringLiteral("label_zh")).toString(),
                    actionObject.value(QStringLiteral("label_en")).toString(),
                    defaultShortcuts_.value(it.key()),
                    defaultShortcutTexts_.value(it.key()),
                });
            }
            shortcutTexts_.insert(it.key(), parsed);
            shortcuts_.insert(
                it.key(),
                miacode::input_shortcut::keyboardSequencesFromGestureTexts(parsed));
            if (actionObject.contains(QStringLiteral("shortcut"))) {
                userOverrides_.insert(it.key(), parsed);
            }
        }
    }

    const QJsonObject contextual = root.value(QStringLiteral("contextual")).toObject();
    for (auto it = contextual.constBegin(); it != contextual.constEnd(); ++it) {
        const QJsonObject shortcutObject = it.value().toObject();
        const QStringList parsed = parseShortcutObject(shortcutObject);
        if (parsed.isEmpty()) {
            continue;
        }
        if (!defaultShortcuts_.contains(it.key()) && shortcutObject.contains(QStringLiteral("default"))) {
            const QStringList defaults = parseShortcutTextValue(shortcutObject.value(QStringLiteral("default")));
            defaultShortcutTexts_.insert(it.key(), defaults);
            defaultShortcuts_.insert(
                it.key(),
                miacode::input_shortcut::keyboardSequencesFromGestureTexts(defaults));
        }
        if (isEditableShortcutId(it.key()) && !definitions_.contains(it.key())) {
            definitions_.insert(it.key(), {
                it.key(),
                shortcutObject.value(QStringLiteral("label_key")).toString(),
                shortcutObject.value(QStringLiteral("label_zh")).toString(),
                shortcutObject.value(QStringLiteral("label_en")).toString(),
                defaultShortcuts_.value(it.key()),
                defaultShortcutTexts_.value(it.key()),
            });
        }
        shortcutTexts_.insert(it.key(), parsed);
        shortcuts_.insert(
            it.key(),
            miacode::input_shortcut::keyboardSequencesFromGestureTexts(parsed));
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
    for (const QString& id : editableShortcutIdsFromDefinitions(definitions_)) {
        const QStringList shortcuts = userOverrides_.value(id);
        if (shortcuts.isEmpty()) {
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
        object.insert(QStringLiteral("shortcut"), sequenceJsonValue(shortcuts));
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
