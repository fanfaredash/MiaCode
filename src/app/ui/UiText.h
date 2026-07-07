#pragma once

#include <QHash>
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

QString text(const QString& key);
bool isChineseUi();
// Resolved UI language for this session (MIACODE_LANG env > stored preference
// > system locale > English). Constant per session — changes apply on restart.
LanguagePreference resolvedLanguage();
// Single entry point for inline-localized strings; replaces the scattered
// `isChineseUi() ? zh : en` ternaries and the per-file l10n/trText/
// localizedText helpers. Chinese returns `zh`; Japanese returns `ja` when
// provided, else the central zh-keyed dictionary (UiTextJaDictionary.cpp),
// else `en`; every other language returns `en`. Simplified Chinese is the
// reference language: author new UI strings as (en, zh) pairs and add the
// Japanese translation to the dictionary — ui_text_locale_spec fails when a
// pair is missing there, and when a zh string is edited without re-keying its
// dictionary entry.
QString localized(const QString& en, const QString& zh, const QString& ja = QString());
// zh-text → Japanese dictionary backing localized() (UiTextJaDictionary.cpp).
const QHash<QString, QString>& japaneseByChineseText();
// Key-map drift guard for ui_text_locale_spec: descriptions of keys present
// in one of zhMap/jaMap but missing from the other. Empty when in sync.
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
