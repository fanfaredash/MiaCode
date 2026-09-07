#pragma once

#include <QJsonObject>
#include <QVector>
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

struct LanguageOption {
    QString id;
    QString label;
    bool builtIn = false;
};

// Single key-based lookup for shared UI strings. The resolved language is tried
// first, then English, then the key itself as the final missing-translation
// marker. The product catalog contains only the built-in language set.
QString text(const QString& key);
// Transitional QML-source bridge. UIv2 was authored with Chinese `qsTr()`
// literals while the application already owns the canonical three-language
// key catalog. Resolve an exact Chinese source string back to that catalog so
// QML follows the same locale as the rest of the application. Sources that do
// not yet have a canonical entry intentionally fall back to themselves; callers
// can therefore remain usable while the catalog is completed without a hidden
// Qt translator or a Widgets dialog path.
QString textForQmlSource(const QString& source);
// Whether a QML `UiText.text(...)` literal resolves to a real catalog entry —
// as a key, as a pinned homograph, through the Chinese reverse index, or from
// the QML-only override table. ui_text_locale_spec asserts this over every v2
// QML file, so a mistyped key (the cover page passes keys, not Chinese source)
// cannot silently render itself on screen in all three languages.
bool hasQmlSourceTranslation(const QString& source);
bool hasTranslationKey(const QString& key);
bool isChineseUi();
// Resolved UI language for this session (MIACODE_LANG env > stored preference
// > system locale > English). Constant per session — changes apply on restart.
LanguagePreference resolvedLanguage();
// Key-map drift guard for ui_text_locale_spec: descriptions of keys present
// in one of the built-in maps but missing from another. Empty when in sync.
QStringList translationKeyMismatches();
LanguagePreference preferredLanguage();
void setPreferredLanguage(LanguagePreference preference);
QString preferredLanguageToken();
void setPreferredLanguageToken(const QString& token);
QString resolvedLanguageToken();
QVector<LanguageOption> availableLanguageOptions();
bool isLanguageAvailable(const QString& token);
bool ensurePreferredLanguageAvailable();
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
QJsonObject normalizePreferencesObject(const QJsonObject& root);
QString themeTokenFromPreferencesObject(const QJsonObject& root);
void setThemeTokenInPreferencesObject(QJsonObject* root, const QString& token);

}  // namespace UiText
