#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>

namespace UiText {

enum class LanguagePreference {
    System,
    English,
    Chinese,
    Japanese,
};

enum class ThemePreference {
    System,
    Light,
    Dark,
};

// Single key-based lookup for shared UI strings. The resolved language is tried
// first, then English, then the key itself as the final missing-translation
// marker. ui_text_locale_spec enforces en/zh/ja key-set parity.
QString text(const QString& key);
bool hasTranslationKey(const QString& key);
bool isChineseUi();
// Resolved UI language for this session (MIACODE_LANG env > stored preference
// > system locale > English). Constant per session — changes apply on restart.
LanguagePreference resolvedLanguage();
// Key-map drift guard for ui_text_locale_spec: descriptions of keys present
// in one of enMap/zhMap/jaMap but missing from another. Empty when in sync.
QStringList translationKeyMismatches();
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
