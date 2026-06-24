#pragma once

#include <QJsonObject>
#include <QSize>

namespace miacode::cover_export {

class CoverLayoutModel;

struct CoverCompositionState {
    static constexpr int kCurrentVersion = 2;

    QSize size;
    QJsonObject background;
    QJsonObject card;
    QJsonObject layout;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& root, CoverCompositionState* out, QString* errorMessage = nullptr);
    static QJsonObject migrateToCurrent(const QJsonObject& root);

    static QJsonObject loadPreferences();
    static void savePreferences(const QJsonObject& root);
};

}  // namespace miacode::cover_export
