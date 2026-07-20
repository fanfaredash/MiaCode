#pragma once

#include <QJsonObject>
#include <QList>
#include <QSize>
#include <QStringList>

namespace miacode::cover_export {

class CoverLayoutModel;

struct CoverUserPreset {
    QString name;
    QJsonObject composition;
};

struct CoverCompositionState {
    static constexpr int kCurrentVersion = 3;

    QSize size;
    QJsonObject background;
    QJsonObject card;
    QJsonObject layout;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& root, CoverCompositionState* out, QString* errorMessage = nullptr);
    static QJsonObject migrateToCurrent(const QJsonObject& root);

    static QJsonObject loadPreferences();
    // Saving the last composition must NOT wipe the sibling recentFiles/presets
    // lists stored alongside it under app.cover_export — they are merged back in.
    static bool savePreferences(const QJsonObject& root);

    // Recent .miacover files (most-recent first, capped at 8), stored under
    // app.cover_export.recentFiles. Independent of the composition payload.
    static QStringList loadRecentFiles();
    static void pushRecentFile(const QString& path);
    static void clearRecentFiles();

    // User layout presets, stored under app.cover_export.presets as
    // [{ name, version, composition }]. Preset compositions are normally
    // size-agnostic (no size object), so applying them keeps the current canvas.
    static QList<CoverUserPreset> loadUserPresets();
    static void saveUserPreset(const QString& name, const QJsonObject& composition);
    static void removeUserPreset(const QString& name);
    static void renameUserPreset(const QString& oldName, const QString& newName);
};

}  // namespace miacode::cover_export
