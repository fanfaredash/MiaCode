#pragma once

#include <QJsonObject>
#include <QVector>
#include <QString>
#include <QStringList>

namespace PreferenceDocument {

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

LanguagePreference resolvedLanguage();
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
QString currentPreferencesSchema();
QString storedPreferencesSchema();
QJsonObject loadPreferencesObject();
bool savePreferencesObject(const QJsonObject& root);
QJsonObject normalizePreferencesObject(const QJsonObject& root);
QString themeTokenFromPreferencesObject(const QJsonObject& root);
void setThemeTokenInPreferencesObject(QJsonObject* root, const QString& token);

}  // namespace PreferenceDocument
