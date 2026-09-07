#include "QmlShortcutModel.h"

#include "app/ui/ShortcutRegistry.h"

#include <QKeySequence>

namespace miacode::qml_ui {
namespace {

QKeySequence resolve(const QString& id, const QString& fallback)
{
    const QKeySequence fallbackSequence =
        fallback.isEmpty() ? QKeySequence() : QKeySequence(fallback, QKeySequence::PortableText);
    return ShortcutRegistry::instance().sequence(id, fallbackSequence);
}

} // namespace

QmlShortcutModel::QmlShortcutModel(QObject* parent) : QObject(parent) {}

qulonglong QmlShortcutModel::revision() const { return revision_; }

QString QmlShortcutModel::sequence(const QString& id, const QString& fallback) const
{
    return resolve(id, fallback).toString(QKeySequence::PortableText);
}

QString QmlShortcutModel::displayText(const QString& id, const QString& fallback) const
{
    return resolve(id, fallback).toString(QKeySequence::NativeText);
}

QString QmlShortcutModel::standardDisplayText(int standardKey) const
{
    return QKeySequence(static_cast<QKeySequence::StandardKey>(standardKey))
        .toString(QKeySequence::NativeText);
}

void QmlShortcutModel::reload()
{
    ShortcutRegistry::instance().reload();
    ++revision_;
    emit revisionChanged();
}

} // namespace miacode::qml_ui

namespace miacode::qml_ui {

QVariantList QmlShortcutModel::editableShortcuts() const
{
    QVariantList rows;
    ShortcutRegistry& registry = ShortcutRegistry::instance();
    for (const ShortcutRegistry::ShortcutDefinition& definition : registry.editableShortcuts()) {
        const QStringList current = registry.shortcutTexts(definition.id);
        const QStringList defaults = registry.defaultShortcutTexts(definition.id);
        QVariantMap row;
        row.insert(QStringLiteral("id"), definition.id);
        // Labels stay raw here. Resolving them would pull UiText — and with it
        // the extension manifest loader — into this model and every spec that
        // links it; the shell resolves labelKey through its own preferences
        // object instead, falling back to labelEn and then the id.
        row.insert(QStringLiteral("labelKey"), definition.labelKey);
        row.insert(
            QStringLiteral("labelFallback"),
            definition.labelEn.isEmpty() ? definition.id : definition.labelEn);
        row.insert(QStringLiteral("shortcutText"), current.join(QStringLiteral(", ")));
        row.insert(QStringLiteral("defaultText"), defaults.join(QStringLiteral(", ")));
        row.insert(QStringLiteral("isDefault"), current == defaults);
        rows.append(row);
    }
    return rows;
}

bool QmlShortcutModel::setShortcutText(const QString& id, const QString& shortcutText)
{
    if (!ShortcutRegistry::instance().setUserShortcutText(id, shortcutText)) {
        return false;
    }
    reload();
    return true;
}

void QmlShortcutModel::resetShortcut(const QString& id)
{
    if (ShortcutRegistry::instance().resetUserShortcut(id)) {
        reload();
    }
}

QString QmlShortcutModel::keyName(int key) const
{
    return QKeySequence(key).toString(QKeySequence::PortableText);
}

void QmlShortcutModel::resetAllShortcuts()
{
    if (ShortcutRegistry::instance().resetEditableShortcuts()) {
        reload();
    }
}

}  // namespace miacode::qml_ui
