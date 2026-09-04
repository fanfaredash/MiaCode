#include "runtime/settings/SettingsHost.h"

miacode::runtime::SettingsHost::SettingsHost(
    Session& session,
    RuntimeContext::Ui& ui,
    RuntimeContext::State& state)
    : session_(session)
    , ui_(ui)
    , state_(state)
{}

void Session::onPreferences()
{
    emit preferencesRequested();
}
