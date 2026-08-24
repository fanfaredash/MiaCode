#pragma once

#include <QString>
#include <QStringList>

namespace miacode::qml_ui {

// The ShortcutRegistry ids v2 binds as window shortcuts and dispatches through
// MainWindow::triggerShortcutCommand. Kept out of QmlCommandService so the
// table can be checked against the registry without standing up a MainWindow —
// a typo here yields a silently inert shortcut, which is exactly the failure
// mode that left v2 without a keyboard route for chart transforms.
inline QStringList qmlShortcutCommandIds()
{
    return {
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
    };
}

} // namespace miacode::qml_ui
