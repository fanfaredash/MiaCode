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
QJsonObject loadPreferencesObject();
bool savePreferencesObject(const QJsonObject& root);

}  // namespace UiText
