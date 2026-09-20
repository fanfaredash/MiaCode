#include "chrome/ShortcutRegistry.h"

#include "common/InputShortcutGesture.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>


namespace miacode::ui {
namespace {

QString userOverridePath()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("shortcuts.json"));
}

QString parseShortcutTextValue(const QJsonValue& value)
{
    return value.isString()
        ? miacode::input_shortcut::normalizeGestureText(value.toString())
        : QString();
}

QStringList parseStringList(const QJsonValue& value)
{
    QStringList values;
    const auto appendValue = [&values](const QString& value) {
        const QString trimmed = value.trimmed();
        if (!trimmed.isEmpty() && !values.contains(trimmed)) {
            values.append(trimmed);
        }
    };
    if (value.isString()) {
        appendValue(value.toString());
    } else if (value.isArray()) {
        for (const QJsonValue& entry : value.toArray()) {
            if (entry.isString()) {
                appendValue(entry.toString());
            }
        }
    }
    return values;
}

QString parseShortcutObject(const QJsonObject& object)
{
    if (object.contains(QStringLiteral("shortcut"))) {
        return parseShortcutTextValue(object.value(QStringLiteral("shortcut")));
    }
    return parseShortcutTextValue(object.value(QStringLiteral("default")));
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
    editableShortcutIds_.clear();

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
    return shortcuts_.value(id, fallback);
}

QString ShortcutRegistry::shortcutText(const QString& id, const QString& fallback) const
{
    return shortcutTexts_.value(id, fallback);
}

QList<ShortcutRegistry::ShortcutDefinition> ShortcutRegistry::editableShortcuts() const
{
    QList<ShortcutDefinition> result;
    for (const QString& id : editableShortcutIds_) {
        const ShortcutDefinition definition = definitions_.value(id);
        if (!definition.id.isEmpty()) {
            result.append(definition);
        }
    }
    return result;
}

QKeySequence ShortcutRegistry::defaultSequence(const QString& id) const
{
    return defaultShortcuts_.value(id);
}

QString ShortcutRegistry::defaultShortcutText(const QString& id) const
{
    return defaultShortcutTexts_.value(id);
}

bool ShortcutRegistry::registerExtensionShortcut(
    const QString& id,
    const QString& label,
    const QKeySequence& defaultSequence)
{
    const QString normalizedId = id.trimmed();
    if (!normalizedId.startsWith(QStringLiteral("extension.")) || label.trimmed().isEmpty()) {
        return false;
    }
    const QKeySequence validDefault = defaultSequence;
    const QString defaultText = validDefault.toString(QKeySequence::PortableText);
    definitions_.insert(normalizedId, {
        normalizedId,
        QString(),
        label,
        label,
        validDefault,
        defaultText,
    });
    if (!validDefault.isEmpty()) {
        defaultShortcuts_.insert(normalizedId, validDefault);
        defaultShortcutTexts_.insert(normalizedId, defaultText);
    }
    if (!editableShortcutIds_.contains(normalizedId)) {
        editableShortcutIds_.append(normalizedId);
    }
    if (!userOverrides_.contains(normalizedId)) {
        shortcuts_.insert(normalizedId, validDefault);
        shortcutTexts_.insert(normalizedId, defaultText);
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
    if (!editableShortcutIds_.contains(id) || normalized.isEmpty()) {
        return false;
    }
    userOverrides_.insert(id, normalized);
    if (!saveUserOverrides()) {
        return false;
    }
    reload();
    return true;
}

bool ShortcutRegistry::resetUserShortcut(const QString& id)
{
    if (!editableShortcutIds_.contains(id)) {
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
    for (const QString& id : editableShortcutIds_) {
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
    if (root.contains(QStringLiteral("editable"))) {
        editableShortcutIds_ = parseStringList(root.value(QStringLiteral("editable")));
    }
    const QJsonObject actions = root.value(QStringLiteral("actions")).toObject();
    for (auto it = actions.constBegin(); it != actions.constEnd(); ++it) {
        const QJsonObject actionObject = it.value().toObject();
        const QString parsed = parseShortcutObject(actionObject);
        if (!defaultShortcuts_.contains(it.key()) && actionObject.contains(QStringLiteral("default"))) {
            const QString defaultText = parseShortcutTextValue(actionObject.value(QStringLiteral("default")));
            if (!defaultText.isEmpty()) {
                defaultShortcutTexts_.insert(it.key(), defaultText);
                defaultShortcuts_.insert(it.key(), QKeySequence(defaultText, QKeySequence::PortableText));
            }
        }
        if (!definitions_.contains(it.key())) {
            definitions_.insert(it.key(), {
                it.key(),
                actionObject.value(QStringLiteral("label_key")).toString(),
                actionObject.value(QStringLiteral("label_zh")).toString(),
                actionObject.value(QStringLiteral("label_en")).toString(),
                defaultShortcuts_.value(it.key()),
                defaultShortcutTexts_.value(it.key()),
            });
        }
        if (!parsed.isEmpty()) {
            shortcutTexts_.insert(it.key(), parsed);
            shortcuts_.insert(it.key(), QKeySequence(parsed, QKeySequence::PortableText));
            if (actionObject.contains(QStringLiteral("shortcut"))) {
                userOverrides_.insert(it.key(), parsed);
            }
        }
    }

    const QJsonObject contextual = root.value(QStringLiteral("contextual")).toObject();
    for (auto it = contextual.constBegin(); it != contextual.constEnd(); ++it) {
        const QJsonObject shortcutObject = it.value().toObject();
        const QString parsed = parseShortcutObject(shortcutObject);
        if (parsed.isEmpty()) {
            continue;
        }
        if (!defaultShortcuts_.contains(it.key()) && shortcutObject.contains(QStringLiteral("default"))) {
            const QString defaultText = parseShortcutTextValue(shortcutObject.value(QStringLiteral("default")));
            if (!defaultText.isEmpty()) {
                defaultShortcutTexts_.insert(it.key(), defaultText);
                defaultShortcuts_.insert(it.key(), QKeySequence(defaultText, QKeySequence::PortableText));
            }
        }
        if (editableShortcutIds_.contains(it.key()) && !definitions_.contains(it.key())) {
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
        shortcuts_.insert(it.key(), QKeySequence(parsed, QKeySequence::PortableText));
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
    for (const QString& id : editableShortcutIds_) {
        const QString shortcut = userOverrides_.value(id);
        if (shortcut.isEmpty()) {
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
        object.insert(QStringLiteral("shortcut"), shortcut);
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

} // namespace miacode::ui
