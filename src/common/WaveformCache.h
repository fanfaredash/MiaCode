#pragma once

#include <functional>
#include <memory>

#include <QHash>
#include <QObject>
#include <QPair>
#include <QString>
#include <QVector>

class QThreadPool;

namespace miacode::waveform {

inline constexpr quint32 kWaveformCacheSchemaVersion = 2;
inline constexpr int kWaveformMinTopLevelColumns = 2048;
inline constexpr double kWaveformTopLevelColumnsPerSecond = 128.0;
inline constexpr int kWaveformDecodeSampleRate = 24000;

struct WaveformColumn {
    float min = 0.0f;
    float max = 0.0f;
};

struct WaveformLevel {
    double secondsPerColumn = 0.0;
    QVector<WaveformColumn> columns;

    double columnsPerSecond() const;
};

struct WaveformData {
    QString normalizedTrackPath;
    qint64 fileSize = -1;
    qint64 lastModifiedMs = -1;
    double durationSeconds = 0.0;
    QVector<WaveformLevel> levels;

    bool isEmpty() const;
};

using WaveformDataPtr = std::shared_ptr<const WaveformData>;

QString normalizeTrackPath(const QString& trackPath);
QString projectDataDirectoryPathForFile(const QString& filePath);
QString waveformCacheDirectoryPath(const QString& projectDataDirectoryPath);
QString waveformCacheFilePath(const QString& normalizedTrackPath, const QString& cacheDirectoryPath);
int recommendedTopLevelColumnCount(double durationSeconds);
WaveformDataPtr makeWaveformPlaceholder(double durationSeconds);
WaveformDataPtr buildWaveformDataFromSamples(
    const QString& trackPath,
    qint64 fileSize,
    qint64 lastModifiedMs,
    const QVector<float>& samples,
    double durationSeconds);
WaveformDataPtr buildWaveformDataFromFile(
    const QString& trackPath,
    qint64 fileSize,
    qint64 lastModifiedMs);
WaveformDataPtr readWaveformDataCache(
    const QString& cacheFilePath,
    const QString& normalizedTrackPath,
    qint64 expectedFileSize,
    qint64 expectedLastModifiedMs);
bool writeWaveformDataCache(const QString& cacheFilePath, const WaveformData& data);
const WaveformLevel* selectWaveformLevelForVisibleRange(
    const WaveformData& data,
    double visibleDurationSeconds,
    int targetPixelWidth);
QPair<int, int> visibleWaveformColumnRange(
    const WaveformLevel& level,
    double visibleStartSecond,
    double visibleEndSecond);

class WaveformCacheService : public QObject {
public:
    using RequestCallback = std::function<void(WaveformDataPtr)>;

    explicit WaveformCacheService(QObject* parent = nullptr);

    void setThreadPool(QThreadPool* threadPool);
    void requestWaveform(
        const QString& trackPath,
        const QString& cacheDirectoryPath,
        RequestCallback callback);
    void clear();

private:
    struct PendingRequest {
        QString cacheKey;
        QString trackPath;
        QString normalizedTrackPath;
        QString cacheFilePath;
        qint64 fileSize = -1;
        qint64 lastModifiedMs = -1;
        QVector<RequestCallback> callbacks;
    };

    QString cacheKeyForTrack(const QString& trackPath, qint64 fileSize, qint64 lastModifiedMs) const;
    void finishPendingRequest(const std::shared_ptr<PendingRequest>& request, WaveformDataPtr data);

    QThreadPool* threadPool_ = nullptr;
    QHash<QString, WaveformDataPtr> memoryCache_;
    QHash<QString, std::shared_ptr<PendingRequest>> pendingRequests_;
};

}  // namespace miacode::waveform
