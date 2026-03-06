#pragma once

#include <QJsonObject>
#include <QString>

namespace UiText {

enum class LanguagePreference {
    System,
    English,
    Chinese,
};

QString text(const QString& key);
bool isChineseUi();
LanguagePreference preferredLanguage();
void setPreferredLanguage(LanguagePreference preference);
QString preferencesFilePath();
QJsonObject loadPreferencesObject();
bool savePreferencesObject(const QJsonObject& root);

}  // namespace UiText
