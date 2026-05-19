#include "common/ProjectPreferences.h"

#include "common/WaveformCache.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>

namespace miacode::project_preferences {

QString projectPreferencesFilePath(const QString& chartFilePath)
{
    const QString projectDataDir = miacode::waveform::projectDataDirectoryPathForFile(chartFilePath);
    if (projectDataDir.isEmpty()) {
        return QString();
    }
    return QDir(projectDataDir).filePath(QStringLiteral("preferences.json"));
}

QJsonObject load(const QString& chartFilePath)
{
    const QString path = projectPreferencesFilePath(chartFilePath);
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return QJsonObject();
    }
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QJsonObject();
    }
    const QByteArray raw = file.readAll();
    file.close();
    if (raw.isEmpty()) {
        return QJsonObject();
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject();
    }
    return doc.object();
}

bool save(const QString& chartFilePath, const QJsonObject& preferences)
{
    const QString path = projectPreferencesFilePath(chartFilePath);
    if (path.isEmpty()) {
        return false;
    }
    const QFileInfo info(path);
    QDir parentDir = info.dir();
    if (!parentDir.exists()) {
        // .miacode/ is created lazily by other subsystems (logs, autosave,
        // waveform cache). Make it ourselves rather than failing the save
        // — preferences should not depend on those subsystems having run.
        if (!parentDir.mkpath(QStringLiteral("."))) {
            return false;
        }
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    const QJsonDocument doc(preferences);
    const QByteArray payload = doc.toJson(QJsonDocument::Indented);
    const qint64 written = file.write(payload);
    file.close();
    return written == payload.size();
}

}  // namespace miacode::project_preferences
