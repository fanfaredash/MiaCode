#pragma once

#include <QJsonObject>
#include <QString>

namespace UiText {

enum class LanguagePreference {
    System,
    English,
    Chinese,
};

enum class ThemePreference {
    System,
    Light,
    Dark,
};

QString text(const QString& key);
bool isChineseUi();
LanguagePreference preferredLanguage();
void setPreferredLanguage(LanguagePreference preference);
ThemePreference preferredTheme();
void setPreferredTheme(ThemePreference preference);
QString preferencesFilePath();
// The schema token baked into the current build. Bumping kPreferencesSchema
// (UiText.cpp) re-runs first-run onboarding for users whose stored
// preferences predate the bump — see main()'s welcome-dialog gate.
QString currentPreferencesSchema();
// The RAW schema token stored on disk (primary file, then the legacy file),
// or an empty string when no readable preferences file exists. Deliberately
// un-normalized: loadPreferencesObject() would inject the current token and
// mask an outdated file.
QString storedPreferencesSchema();
QJsonObject loadPreferencesObject();
bool savePreferencesObject(const QJsonObject& root);

}  // namespace UiText
