#pragma once

#include <QJsonObject>

#include "UiText.h"

namespace miacode::video_export {

inline QJsonObject loadDialogPreferences()
{
    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject app = root.value(QStringLiteral("app")).toObject();
    return app.value(QStringLiteral("video_export")).toObject();
}

inline bool saveDialogPreferences(const QJsonObject& preferences)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QStringLiteral("app")).toObject();
    app.insert(QStringLiteral("video_export"), preferences);
    root.insert(QStringLiteral("app"), app);
    return UiText::savePreferencesObject(root);
}

}  // namespace miacode::video_export
