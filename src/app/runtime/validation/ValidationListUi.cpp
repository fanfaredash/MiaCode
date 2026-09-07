#include "runtime/validation/ValidationHost.h"
#include "runtime/Shared.h"

#include "common/ProjectPreferences.h"

#include <QtCore>

using namespace miacode::runtime::shared;

namespace {

constexpr const char* kIgnoreMuriIssuePromptsPrefKey = "ignore_muri_issue_prompts";

}  // namespace

void miacode::runtime::ValidationHost::loadProjectValidationPreferences()
{
    bool ignoreMuriIssuePrompts = false;
    if (!state_.currentFilePath_.isEmpty()) {
        const QJsonObject prefs = miacode::project_preferences::load(state_.currentFilePath_);
        const QJsonValue stored = prefs.value(QLatin1String(kIgnoreMuriIssuePromptsPrefKey));
        if (stored.isBool()) {
            ignoreMuriIssuePrompts = stored.toBool(false);
        }
    }
    applyIgnoreMuriIssuePrompts(ignoreMuriIssuePrompts, false);
}

void miacode::runtime::ValidationHost::saveProjectValidationPreferences(const QString& chartFilePath) const
{
    const QString targetPath = chartFilePath.isEmpty() ? state_.currentFilePath_ : chartFilePath;
    if (targetPath.isEmpty()) {
        return;
    }
    const QString preferencesPath = miacode::project_preferences::projectPreferencesFilePath(targetPath);
    QJsonObject prefs = miacode::project_preferences::load(targetPath);
    if (!state_.ignoreMuriIssuePrompts_
        && !QFileInfo::exists(preferencesPath)
        && !prefs.contains(QLatin1String(kIgnoreMuriIssuePromptsPrefKey))) {
        return;
    }
    prefs[QLatin1String(kIgnoreMuriIssuePromptsPrefKey)] = state_.ignoreMuriIssuePrompts_;
    miacode::project_preferences::save(targetPath, prefs);
}

void miacode::runtime::ValidationHost::applyIgnoreMuriIssuePrompts(bool enabled, bool persistPreference)
{
    state_.ignoreMuriIssuePrompts_ = enabled;
    if (persistPreference) {
        saveProjectValidationPreferences();
    }
    session_.applyAlignedMuriAnalysisReportToViews();
}

void Session::loadProjectValidationPreferences()
{
    validation_->loadProjectValidationPreferences();
}

void Session::saveProjectValidationPreferences(const QString& chartFilePath) const
{
    validation_->saveProjectValidationPreferences(chartFilePath);
}

void Session::applyIgnoreMuriIssuePrompts(bool enabled, bool persistPreference)
{
    validation_->applyIgnoreMuriIssuePrompts(enabled, persistPreference);
    if (persistPreference) emit muriPromptPreferenceChanged();
}
