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
    Session::HostUi& ui,
    Session::HostState& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
{}

void miacode::runtime::SettingsHost::showWelcomeDialog()
{
}

void miacode::runtime::SettingsHost::applyConfiguredShortcuts()
{
    applyConfiguredShortcut(
        session_.stopOrPlayPreviewShortcutAction_,
        QStringLiteral("preview.stop_or_play"),
        QKeySequence(QStringLiteral("Ctrl+Shift+C")),
        Qt::ApplicationShortcut);
    applyConfiguredShortcut(
        session_.playPausePreviewShortcutAction_,
        QStringLiteral("preview.play_pause_global"),
        QKeySequence(QStringLiteral("Ctrl+Shift+X")),
        Qt::ApplicationShortcut);
    applyConfiguredShortcut(
        session_.previewSlowerAction_,
        QStringLiteral("preview.speed_down"),
        QKeySequence(QStringLiteral("Ctrl+O")));
    applyConfiguredShortcut(
        session_.previewFasterAction_,
        QStringLiteral("preview.speed_up"),
        QKeySequence(QStringLiteral("Ctrl+P")));
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

void Session::showWelcomeDialog()
{
    settings_->showWelcomeDialog();
}
