#pragma once

#include "document/ChartTransformCommands.h"

#include <QString>
#include <QStringList>

namespace miacode::ui {

// The ShortcutRegistry ids v2 binds as window shortcuts. Derived from the
// transform table rather than retyped, so an operation cannot be bound without
// a dispatch behind it or dispatched without a binding in front of it — a typo
// used to yield a silently inert shortcut, which is exactly the failure mode
// that left v2 without a keyboard route for chart transforms.
inline QStringList shortcutCommandIds()
{
    QStringList ids;
    for (const ChartTransformSpec& spec : chartTransformSpecs()) {
        ids.append(spec.id);
    }
    return ids;
}

} // namespace miacode::ui
