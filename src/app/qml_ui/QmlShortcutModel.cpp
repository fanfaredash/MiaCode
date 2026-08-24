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
