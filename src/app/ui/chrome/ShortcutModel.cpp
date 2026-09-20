#include "chrome/ShortcutModel.h"

#include "app/ui/chrome/ShortcutRegistry.h"

#include <QKeySequence>

namespace miacode::ui {
namespace {

QKeySequence resolve(const QString& id, const QString& fallback)
{
    const QKeySequence fallbackSequence =
        fallback.isEmpty() ? QKeySequence() : QKeySequence(fallback, QKeySequence::PortableText);
    return ShortcutRegistry::instance().sequence(id, fallbackSequence);
}

} // namespace

ShortcutModel::ShortcutModel(QObject* parent) : QObject(parent) {}

qulonglong ShortcutModel::revision() const { return revision_; }

QString ShortcutModel::sequence(const QString& id, const QString& fallback) const
{
    return resolve(id, fallback).toString(QKeySequence::PortableText);
}

QString ShortcutModel::displayText(const QString& id, const QString& fallback) const
{
    return resolve(id, fallback).toString(QKeySequence::NativeText);
}

QString ShortcutModel::standardDisplayText(int standardKey) const
{
    return QKeySequence(static_cast<QKeySequence::StandardKey>(standardKey))
        .toString(QKeySequence::NativeText);
}

void ShortcutModel::reload()
{
    ShortcutRegistry::instance().reload();
    ++revision_;
    emit revisionChanged();
}

} // namespace miacode::ui

namespace miacode::ui {

QVariantList ShortcutModel::editableShortcuts() const
{
    QVariantList rows;
    ShortcutRegistry& registry = ShortcutRegistry::instance();
    for (const ShortcutRegistry::ShortcutDefinition& definition : registry.editableShortcuts()) {
        const QString current = registry.shortcutText(definition.id);
        const QString defaults = registry.defaultShortcutText(definition.id);
        QVariantMap row;
        row.insert(QStringLiteral("id"), definition.id);
        // Labels stay raw here. The shell resolves labelKey with qsTrId,
        // falling back to labelEn and then the id.
        row.insert(QStringLiteral("labelKey"), definition.labelKey);
        row.insert(
            QStringLiteral("labelFallback"),
            definition.labelEn.isEmpty() ? definition.id : definition.labelEn);
        const auto nativeText = [](const QString& text) {
            return QKeySequence(text, QKeySequence::PortableText).toString(QKeySequence::NativeText);
        };
        row.insert(QStringLiteral("shortcutText"), nativeText(current));
        row.insert(QStringLiteral("defaultText"), nativeText(defaults));
        row.insert(QStringLiteral("isDefault"), current == defaults);
        rows.append(row);
    }
    return rows;
}

bool ShortcutModel::setShortcutText(const QString& id, const QString& shortcutText)
{
    if (!ShortcutRegistry::instance().setUserShortcutText(id, shortcutText)) {
        return false;
    }
    reload();
    return true;
}

void ShortcutModel::resetShortcut(const QString& id)
{
    if (ShortcutRegistry::instance().resetUserShortcut(id)) {
        reload();
    }
}

QString ShortcutModel::shortcutTextForKeyEvent(int key, int modifiers) const
{
    const auto keyboardModifiers = Qt::KeyboardModifiers(modifiers)
        & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    return QKeySequence(QKeyCombination(keyboardModifiers, Qt::Key(key)))
        .toString(QKeySequence::PortableText);
}

void ShortcutModel::resetAllShortcuts()
{
    if (ShortcutRegistry::instance().resetEditableShortcuts()) {
        reload();
    }
}

}  // namespace miacode::ui
