#include "runtime/settings/SettingsHost.h"

#include "ShortcutRegistry.h"
#include "timeline/quick/TimelineQuickStateBridge.h"

#include <QAction>
#include <QKeySequence>

namespace {

void applyConfiguredShortcut(
    QAction* action,
    const QString& id,
    const QKeySequence& fallback,
    Qt::ShortcutContext context = Qt::WindowShortcut)
{
    ShortcutRegistry::instance().applyShortcut(action, id, fallback);
    if (action != nullptr) {
        action->setShortcutContext(context);
    }
}

}  // namespace

miacode::runtime::SettingsHost::SettingsHost(
    Session& session,
    RuntimeContext::Ui& ui,
    RuntimeContext::State& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
{}

void miacode::runtime::SettingsHost::applyConfiguredShortcuts()
{
    if (session_.timelineQuickStateBridge_ != nullptr) {
        session_.timelineQuickStateBridge_->setZoomWheelShortcuts(
            ShortcutRegistry::instance().shortcutTexts(
                QStringLiteral("timeline.zoom_in"),
                {QStringLiteral("Ctrl+WheelUp")}),
            ShortcutRegistry::instance().shortcutTexts(
                QStringLiteral("timeline.zoom_out"),
                {QStringLiteral("Ctrl+WheelDown")}));
    }
    applyConfiguredShortcut(
        session_.fontDecreaseAction_,
        QStringLiteral("editor.font_decrease"),
        QKeySequence(QStringLiteral("Ctrl+Alt+-")),
        Qt::WindowShortcut);
    applyConfiguredShortcut(
        session_.fontIncreaseAction_,
        QStringLiteral("editor.font_increase"),
        QKeySequence(QStringLiteral("Ctrl+Alt+=")),
        Qt::WindowShortcut);
}

void Session::onPreferences()
{
    emit preferencesRequested();
}

void Session::applyConfiguredShortcuts()
{
    settings_->applyConfiguredShortcuts();
}
