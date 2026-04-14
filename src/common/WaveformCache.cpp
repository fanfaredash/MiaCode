#include "common/WaveformCache.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>

#include "common/MiniaudioFileAccess.h"

#include "../../third_party/miniaudio/miniaudio.h"

namespace miacode::waveform {

namespace {

constexpr quint32 kWaveformCacheMagic = 0x4D435746;  // MCWF
constexpr quint32 kWaveformCacheCurrentVersion = kWaveformCacheSchemaVersion;

int nextPowerOfTwoAtLeast(int value)
{
    int result = 1;
    while (result < value) {
        result <<= 1;
    }
    return result;
}

WaveformDataPtr buildWaveformData(
    const QString& normalizedTrackPath,
    qint64 fileSize,
    qint64 lastModifiedMs,
    const QVector<float>& samples,
    double durationSeconds)
{
    auto data = std::make_shared<WaveformData>();
    data->normalizedTrackPath = normalizedTrackPath;
    data->fileSize = fileSize;
    data->lastModifiedMs = lastModifiedMs;
    data->durationSeconds = qMax(0.0, durationSeconds);

    if (samples.isEmpty() || data->durationSeconds <= 0.0) {
        return data;
    }

    const int topLevelColumnCount = recommendedTopLevelColumnCount(data->durationSeconds);
    if (topLevelColumnCount <= 0) {
        return data;
    }

    WaveformLevel topLevel;
    topLevel.secondsPerColumn = data->durationSeconds / static_cast<double>(topLevelColumnCount);
    topLevel.columns.fill(WaveformColumn{1.0f, -1.0f}, topLevelColumnCount);
    QVector<bool> touched(topLevelColumnCount, false);

    const int sampleCount = samples.size();
    for (int sampleIndex = 0; sampleIndex < sampleCount; ++sampleIndex) {
        const float sample = qBound(-1.0f, samples.at(sampleIndex), 1.0f);
        const int columnIndex = qBound(
            0,
            static_cast<int>((static_cast<qint64>(sampleIndex) * topLevelColumnCount) / qMax(1, sampleCount)),
            topLevelColumnCount - 1);
        WaveformColumn& column = topLevel.columns[columnIndex];
        column.min = qMin(column.min, sample);
        column.max = qMax(column.max, sample);
        touched[columnIndex] = true;
    }

    for (int index = 0; index < topLevel.columns.size(); ++index) {
        if (!touched.at(index)) {
            topLevel.columns[index] = WaveformColumn{};
        }
    }

    data->levels.append(topLevel);
    while (data->levels.constLast().columns.size() > 1) {
        const WaveformLevel& previousLevel = data->levels.constLast();
        WaveformLevel nextLevel;
        nextLevel.secondsPerColumn = previousLevel.secondsPerColumn * 2.0;
        nextLevel.columns.reserve((previousLevel.columns.size() + 1) / 2);
        for (int index = 0; index < previousLevel.columns.size(); index += 2) {
            const WaveformColumn& first = previousLevel.columns.at(index);
            const WaveformColumn& second = previousLevel.columns.at(qMin(index + 1, previousLevel.columns.size() - 1));
            nextLevel.columns.append(WaveformColumn{
                qMin(first.min, second.min),
                qMax(first.max, second.max),
            });
        }
        data->levels.append(nextLevel);
    }

    return data;
}

QVector<float> decodeMonoSamples(const QString& trackPath, double* durationSeconds)
{
    QVector<float> samples;
    if (durationSeconds != nullptr) {
        *durationSeconds = 0.0;
    }
    if (trackPath.isEmpty() || !QFileInfo::exists(trackPath)) {
        return samples;
    }

    ma_decoder_config config = ma_decoder_config_init(
        ma_format_f32,
        1,
        static_cast<ma_uint32>(kWaveformDecodeSampleRate));
    ma_decoder decoder;
    if (miacode::audio_io::decoderInitFile(trackPath, &config, &decoder) != MA_SUCCESS) {
        return samples;
    }

    ma_uint64 totalFrames = 0;
    if (ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS && totalFrames > 0) {
        samples.reserve(static_cast<int>(qMin<ma_uint64>(totalFrames, static_cast<ma_uint64>(INT_MAX))));
        if (durationSeconds != nullptr) {
            *durationSeconds = static_cast<double>(totalFrames) / static_cast<double>(kWaveformDecodeSampleRate);
        }
    }

    QVector<float> buffer(4096, 0.0f);
    while (true) {
        ma_uint64 framesRead = 0;
        if (ma_decoder_read_pcm_frames(
                &decoder,
                buffer.data(),
                static_cast<ma_uint64>(buffer.size()),
                &framesRead) != MA_SUCCESS
            || framesRead == 0) {
            break;
        }

        const int oldSize = samples.size();
        samples.resize(oldSize + static_cast<int>(framesRead));
        std::copy_n(buffer.cbegin(), static_cast<int>(framesRead), samples.begin() + oldSize);
    }

    ma_decoder_uninit(&decoder);
    if (durationSeconds != nullptr && *durationSeconds <= 0.0 && !samples.isEmpty()) {
        *durationSeconds = static_cast<double>(samples.size()) / static_cast<double>(kWaveformDecodeSampleRate);
    }
    return samples;
}

}  // namespace

double WaveformLevel::columnsPerSecond() const
{
    return secondsPerColumn > 0.0 ? (1.0 / secondsPerColumn) : 0.0;
}

bool WaveformData::isEmpty() const
{
    return levels.isEmpty() || levels.constFirst().columns.isEmpty();
}

QString normalizeTrackPath(const QString& trackPath)
{
    if (trackPath.isEmpty()) {
        return QString();
    }
    const QFileInfo fileInfo(trackPath);
    const QString absolutePath = fileInfo.exists() ? fileInfo.canonicalFilePath() : fileInfo.absoluteFilePath();
    if (absolutePath.isEmpty()) {
        return QDir::cleanPath(trackPath);
    }
    return QDir::cleanPath(absolutePath);
}

QString projectDataDirectoryPathForFile(const QString& filePath)
{
    const QString normalizedPath = filePath.isEmpty() ? QString() : QDir::cleanPath(filePath);
    if (normalizedPath.isEmpty()) {
        return QString();
    }

    const QFileInfo fileInfo(normalizedPath);
    const QString projectDirectoryPath = fileInfo.absolutePath();
    if (projectDirectoryPath.isEmpty()) {
        return QString();
    }
    return QDir(projectDirectoryPath).filePath(QStringLiteral(".miacode"));
}

QString waveformCacheDirectoryPath(const QString& projectDataDirectoryPath)
{
    if (projectDataDirectoryPath.isEmpty()) {
        return QString();
    }
    return QDir(projectDataDirectoryPath).filePath(QStringLiteral("waveform"));
}

QString waveformCacheFilePath(const QString& normalizedTrackPath, const QString& cacheDirectoryPath)
{
    if (normalizedTrackPath.isEmpty() || cacheDirectoryPath.isEmpty()) {
        return QString();
    }
    const QByteArray hash = QCryptographicHash::hash(normalizedTrackPath.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QDir(cacheDirectoryPath).filePath(QStringLiteral("%1.wvfm").arg(QString::fromLatin1(hash)));
}

int recommendedTopLevelColumnCount(double durationSeconds)
{
    const int minimumTarget = qMax(1, kWaveformMinTopLevelColumns);
    const int scaledTarget = qMax(
        minimumTarget,
        static_cast<int>(std::ceil(qMax(0.0, durationSeconds) * kWaveformTopLevelColumnsPerSecond)));
    return nextPowerOfTwoAtLeast(scaledTarget);
}

WaveformDataPtr makeWaveformPlaceholder(double durationSeconds)
{
    auto data = std::make_shared<WaveformData>();
    data->durationSeconds = qMax(0.0, durationSeconds);
    return data;
}

WaveformDataPtr buildWaveformDataFromSamples(
    const QString& trackPath,
    qint64 fileSize,
    qint64 lastModifiedMs,
    const QVector<float>& samples,
    double durationSeconds)
{
    return buildWaveformData(
        normalizeTrackPath(trackPath),
        fileSize,
        lastModifiedMs,
        samples,
        durationSeconds);
}

WaveformDataPtr buildWaveformDataFromFile(
    const QString& trackPath,
    qint64 fileSize,
    qint64 lastModifiedMs)
{
    double durationSeconds = 0.0;
    const QVector<float> samples = decodeMonoSamples(trackPath, &durationSeconds);
    return buildWaveformData(
        normalizeTrackPath(trackPath),
        fileSize,
        lastModifiedMs,
        samples,
        durationSeconds);
}

WaveformDataPtr readWaveformDataCache(
    const QString& cacheFilePath,
    const QString& normalizedTrackPath,
    qint64 expectedFileSize,
    qint64 expectedLastModifiedMs)
{
    QFile file(cacheFilePath);
    if (!file.exists() || !file.open(QIODevice::ReadOnly)) {
        return {};
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);

    quint32 magic = 0;
    quint32 version = 0;
    qint64 storedFileSize = -1;
    qint64 storedLastModifiedMs = -1;
    double durationSeconds = 0.0;
    quint32 levelCount = 0;
    stream >> magic >> version >> storedFileSize >> storedLastModifiedMs >> durationSeconds >> levelCount;
    if (stream.status() != QDataStream::Ok
        || magic != kWaveformCacheMagic
        || version != kWaveformCacheCurrentVersion
        || storedFileSize != expectedFileSize
        || storedLastModifiedMs != expectedLastModifiedMs) {
        return {};
    }

    auto data = std::make_shared<WaveformData>();
    data->normalizedTrackPath = normalizedTrackPath;
    data->fileSize = storedFileSize;
    data->lastModifiedMs = storedLastModifiedMs;
    data->durationSeconds = qMax(0.0, durationSeconds);
    data->levels.reserve(static_cast<int>(levelCount));

    for (quint32 levelIndex = 0; levelIndex < levelCount; ++levelIndex) {
        qint32 columnCount = 0;
        double secondsPerColumn = 0.0;
        stream >> columnCount >> secondsPerColumn;
        if (stream.status() != QDataStream::Ok
            || columnCount <= 0
            || secondsPerColumn <= 0.0) {
            return {};
        }

        WaveformLevel level;
        level.secondsPerColumn = secondsPerColumn;
        level.columns.reserve(columnCount);
        for (int columnIndex = 0; columnIndex < columnCount; ++columnIndex) {
            float minValue = 0.0f;
            float maxValue = 0.0f;
            stream >> minValue >> maxValue;
            if (stream.status() != QDataStream::Ok) {
                return {};
            }
            level.columns.append(WaveformColumn{
                qBound(-1.0f, minValue, 1.0f),
                qBound(-1.0f, maxValue, 1.0f),
            });
        }
        data->levels.append(level);
    }

    if (data->levels.isEmpty()) {
        return {};
    }
    return data;
}

bool writeWaveformDataCache(const QString& cacheFilePath, const WaveformData& data)
{
    if (cacheFilePath.isEmpty() || data.levels.isEmpty()) {
        return false;
    }

    const QFileInfo cacheInfo(cacheFilePath);
    if (!QDir().mkpath(cacheInfo.absolutePath())) {
        return false;
    }

    QSaveFile file(cacheFilePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }

    QDataStream stream(&file);
    stream.setVersion(QDataStream::Qt_6_0);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << kWaveformCacheMagic
           << kWaveformCacheCurrentVersion
           << data.fileSize
           << data.lastModifiedMs
           << data.durationSeconds
           << static_cast<quint32>(data.levels.size());

    for (const WaveformLevel& level : data.levels) {
        stream << static_cast<qint32>(level.columns.size()) << level.secondsPerColumn;
        for (const WaveformColumn& column : level.columns) {
            stream << column.min << column.max;
        }
    }

    if (stream.status() != QDataStream::Ok) {
        return false;
    }
    return file.commit();
}

const WaveformLevel* selectWaveformLevelForVisibleRange(
    const WaveformData& data,
    double visibleDurationSeconds,
    int targetPixelWidth)
{
    if (data.levels.isEmpty() || visibleDurationSeconds <= 0.0 || targetPixelWidth <= 0) {
        return nullptr;
    }

    const double targetColumnsPerSecond = static_cast<double>(targetPixelWidth) / visibleDurationSeconds;
    const WaveformLevel* selectedLevel = &data.levels.constFirst();
    for (const WaveformLevel& level : data.levels) {
        if (level.columnsPerSecond() + 1e-6 >= targetColumnsPerSecond) {
            selectedLevel = &level;
            continue;
        }
        break;
    }
    return selectedLevel;
}

QPair<int, int> visibleWaveformColumnRange(
    const WaveformLevel& level,
    double visibleStartSecond,
    double visibleEndSecond)
{
    if (level.columns.isEmpty() || level.secondsPerColumn <= 0.0) {
        return {0, 0};
    }

    const double boundedStart = qMin(visibleStartSecond, visibleEndSecond);
    const double boundedEnd = qMax(visibleStartSecond, visibleEndSecond);
    const int begin = qBound(
        0,
        static_cast<int>(std::floor(qMax(0.0, boundedStart) / level.secondsPerColumn)),
        level.columns.size());
    const int end = qBound(
        begin,
        static_cast<int>(std::ceil(qMax(0.0, boundedEnd) / level.secondsPerColumn)) + 1,
        level.columns.size());
    return {begin, end};
}

WaveformCacheService::WaveformCacheService(QObject* parent)
    : QObject(parent)
{}

void WaveformCacheService::setThreadPool(QThreadPool* threadPool)
{
    threadPool_ = threadPool;
}

void WaveformCacheService::requestWaveform(
    const QString& trackPath,
    const QString& cacheDirectoryPath,
    RequestCallback callback)
{
    const QString normalizedTrackPath = normalizeTrackPath(trackPath);
    const QFileInfo trackInfo(trackPath);
    if (normalizedTrackPath.isEmpty() || !trackInfo.exists() || !trackInfo.isFile()) {
        if (callback) {
            callback(makeWaveformPlaceholder(0.0));
        }
        return;
    }

    const qint64 fileSize = trackInfo.size();
    const qint64 lastModifiedMs = trackInfo.lastModified().toMSecsSinceEpoch();
    const QString cacheKey = cacheKeyForTrack(normalizedTrackPath, fileSize, lastModifiedMs);
    if (memoryCache_.contains(cacheKey)) {
        if (callback) {
            callback(memoryCache_.value(cacheKey));
        }
        return;
    }

    if (pendingRequests_.contains(cacheKey)) {
        if (callback) {
            pendingRequests_.value(cacheKey)->callbacks.append(std::move(callback));
        }
        return;
    }

    auto request = std::make_shared<PendingRequest>();
    request->cacheKey = cacheKey;
    request->trackPath = trackPath;
    request->normalizedTrackPath = normalizedTrackPath;
    request->cacheFilePath = waveformCacheFilePath(normalizedTrackPath, cacheDirectoryPath);
    request->fileSize = fileSize;
    request->lastModifiedMs = lastModifiedMs;
    if (callback) {
        request->callbacks.append(std::move(callback));
    }
    pendingRequests_.insert(cacheKey, request);

    QPointer<WaveformCacheService> guard(this);
    QThreadPool* const pool = threadPool_ != nullptr ? threadPool_ : QThreadPool::globalInstance();
    pool->start([guard, request]() {
        WaveformDataPtr data;
        if (!request->cacheFilePath.isEmpty()) {
            data = readWaveformDataCache(
                request->cacheFilePath,
                request->normalizedTrackPath,
                request->fileSize,
                request->lastModifiedMs);
        }
        if (!data) {
            data = buildWaveformDataFromFile(
                request->trackPath,
                request->fileSize,
                request->lastModifiedMs);
            if (data && !data->levels.isEmpty() && !request->cacheFilePath.isEmpty()) {
                writeWaveformDataCache(request->cacheFilePath, *data);
            }
        }
        if (!data) {
            data = makeWaveformPlaceholder(0.0);
        }
        if (guard.isNull()) {
            return;
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, request, data]() {
                if (guard.isNull()) {
                    return;
                }
                guard->finishPendingRequest(request, data);
            },
            Qt::QueuedConnection);
    });
}

void WaveformCacheService::clear()
{
    memoryCache_.clear();
    pendingRequests_.clear();
}

QString WaveformCacheService::cacheKeyForTrack(
    const QString& trackPath,
    qint64 fileSize,
    qint64 lastModifiedMs) const
{
    return QStringLiteral("%1|%2|%3|%4")
        .arg(trackPath)
        .arg(fileSize)
        .arg(lastModifiedMs)
        .arg(kWaveformCacheCurrentVersion);
}

void WaveformCacheService::finishPendingRequest(
    const std::shared_ptr<PendingRequest>& request,
    WaveformDataPtr data)
{
    const auto it = pendingRequests_.find(request->cacheKey);
    if (it == pendingRequests_.end() || it.value() != request) {
        return;
    }

    pendingRequests_.erase(it);
    if (data) {
        memoryCache_.insert(request->cacheKey, data);
    }
    for (const RequestCallback& callback : request->callbacks) {
        if (callback) {
            callback(data);
        }
    }
}

}  // namespace miacode::waveform
