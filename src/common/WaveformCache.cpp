#include "common/WaveformCache.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMetaObject>
#include <QPointer>
#include <QSaveFile>
#include <QThreadPool>
#include <QtMath>

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/MiniaudioFileAccess.h"
#include "audio/PreviewBassDeviceLease.h"

#include "../../third_party/miniaudio/miniaudio.h"

#ifdef MIACODE_HAS_BASS_AUDIO
#ifdef Q_OS_WIN
#include <windows.h>
#endif

#include "bass.h"
#endif

namespace miacode::waveform {

namespace {

constexpr quint32 kWaveformCacheMagic = 0x4D435746;  // MCWF
constexpr quint32 kWaveformCacheCurrentVersion = kWaveformCacheSchemaVersion;
constexpr double kWaveformDiagThresholdLow = 0.02;
constexpr double kWaveformDiagThresholdMid = 0.05;
constexpr double kWaveformDiagThresholdHigh = 0.10;

enum class WaveformDecodeBackend {
    None,
    Bass,
    Miniaudio,
};

QString waveformDecodeBackendLabel(WaveformDecodeBackend backend)
{
    switch (backend) {
    case WaveformDecodeBackend::Bass:
        return QStringLiteral("bass");
    case WaveformDecodeBackend::Miniaudio:
        return QStringLiteral("miniaudio");
    case WaveformDecodeBackend::None:
    default:
        return QStringLiteral("none");
    }
}

void appendWaveformDebugLog(const QString& payload)
{
    if (!miacode::debug_options::audioDebugOutputEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QStringLiteral("preview/waveform"),
        payload);
}

void appendWaveformAlignmentDebugLog(const QString& payload)
{
    if (!miacode::debug_options::previewWaveformAlignmentDiagnosticsEnabled()) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Audio,
        QStringLiteral("preview/waveform_align"),
        payload);
}

QString waveformDebugIdFromNormalizedPath(const QString& normalizedTrackPath)
{
    if (normalizedTrackPath.isEmpty()) {
        return QStringLiteral("none");
    }
    const QByteArray hash =
        QCryptographicHash::hash(normalizedTrackPath.toUtf8(), QCryptographicHash::Sha256).toHex();
    return QString::fromLatin1(hash.left(12));
}

double columnEnergy(const WaveformColumn& column)
{
    return qMax(qAbs(static_cast<double>(column.min)), qAbs(static_cast<double>(column.max)));
}

double firstColumnSecondAtThreshold(const WaveformLevel& level, double threshold)
{
    if (level.secondsPerColumn <= 0.0) {
        return -1.0;
    }
    for (int index = 0; index < level.columns.size(); ++index) {
        if (columnEnergy(level.columns.at(index)) >= threshold) {
            return static_cast<double>(index) * level.secondsPerColumn;
        }
    }
    return -1.0;
}

double peakColumnSecond(const WaveformLevel& level, double* peakEnergy)
{
    if (peakEnergy != nullptr) {
        *peakEnergy = 0.0;
    }
    if (level.columns.isEmpty() || level.secondsPerColumn <= 0.0) {
        return -1.0;
    }
    int peakIndex = -1;
    double bestEnergy = 0.0;
    for (int index = 0; index < level.columns.size(); ++index) {
        const double energy = columnEnergy(level.columns.at(index));
        if (peakIndex < 0 || energy > bestEnergy) {
            peakIndex = index;
            bestEnergy = energy;
        }
    }
    if (peakEnergy != nullptr) {
        *peakEnergy = bestEnergy;
    }
    return peakIndex >= 0 ? static_cast<double>(peakIndex) * level.secondsPerColumn : -1.0;
}

#ifdef MIACODE_HAS_BASS_AUDIO
QMutex& bassWaveformDecodeMutex()
{
    static QMutex mutex;
    return mutex;
}

class ScopedBassWaveformDevice
{
public:
    ScopedBassWaveformDevice()
        : lease_(miacode::preview_audio::PreviewBassDeviceLease::acquire({
            [] { return static_cast<miacode::preview_audio::BassDeviceLeaseApi::DeviceId>(BASS_GetDevice()); },
            [] { return BASS_Init(0, kWaveformDecodeSampleRate, BASS_DEVICE_NOSPEAKER, nullptr, nullptr) != FALSE; },
            [] { BASS_Free(); },
        }))
    {
    }

    bool available() const { return lease_.acquired(); }

private:
    miacode::preview_audio::PreviewBassDeviceLease lease_;
};
#endif

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

QVector<float> decodeMonoSamplesWithMiniaudio(const QString& trackPath, double* durationSeconds)
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

#ifdef MIACODE_HAS_BASS_AUDIO
QVector<float> decodeMonoSamplesWithBass(const QString& trackPath, double* durationSeconds)
{
    QVector<float> samples;
    if (durationSeconds != nullptr) {
        *durationSeconds = 0.0;
    }
    if (trackPath.isEmpty() || !QFileInfo::exists(trackPath)) {
        return samples;
    }

    QMutexLocker locker(&bassWaveformDecodeMutex());
    ScopedBassWaveformDevice device;
    if (!device.available()) {
        return samples;
    }

    QFile file(trackPath);
    if (!file.open(QIODevice::ReadOnly)) {
        return samples;
    }
    QByteArray bytes = file.readAll();
    file.close();
    if (bytes.isEmpty()) {
        return samples;
    }

    HSTREAM stream = BASS_StreamCreateFile(
        TRUE,
        bytes.constData(),
        0,
        static_cast<QWORD>(bytes.size()),
        BASS_STREAM_DECODE | BASS_STREAM_PRESCAN | BASS_SAMPLE_FLOAT);
    if (stream == 0) {
        return samples;
    }

    BASS_CHANNELINFO info{};
    if (!BASS_ChannelGetInfo(stream, &info) || info.chans == 0) {
        BASS_StreamFree(stream);
        return samples;
    }
    const int channels = qBound(1, static_cast<int>(info.chans), 8);
    const int sampleRate = qMax(1, static_cast<int>(info.freq));

    const QWORD lengthBytes = BASS_ChannelGetLength(stream, BASS_POS_BYTE);
    if (lengthBytes != static_cast<QWORD>(-1) && lengthBytes > 0) {
        const double seconds = BASS_ChannelBytes2Seconds(stream, lengthBytes);
        if (qIsFinite(seconds) && seconds > 0.0) {
            if (durationSeconds != nullptr) {
                *durationSeconds = seconds;
            }
            samples.reserve(static_cast<int>(qMin<double>(
                static_cast<double>(INT_MAX),
                std::ceil(seconds * static_cast<double>(sampleRate)))));
        }
    }

    constexpr int kBassDecodeFramesPerChunk = 4096;
    QVector<float> interleaved(kBassDecodeFramesPerChunk * channels, 0.0f);
    while (true) {
        const DWORD requestedBytes = static_cast<DWORD>(interleaved.size() * static_cast<int>(sizeof(float)));
        const DWORD bytesRead = BASS_ChannelGetData(
            stream,
            interleaved.data(),
            requestedBytes | BASS_DATA_FLOAT);
        if (bytesRead == static_cast<DWORD>(-1) || bytesRead == 0) {
            break;
        }

        const int floatsRead = static_cast<int>(bytesRead / sizeof(float));
        const int framesRead = floatsRead / channels;
        if (framesRead <= 0) {
            break;
        }

        const int oldSize = samples.size();
        samples.resize(oldSize + framesRead);
        for (int frame = 0; frame < framesRead; ++frame) {
            double mixed = 0.0;
            const int base = frame * channels;
            for (int channel = 0; channel < channels; ++channel) {
                mixed += static_cast<double>(interleaved.at(base + channel));
            }
            samples[oldSize + frame] = static_cast<float>(mixed / static_cast<double>(channels));
        }
    }

    BASS_StreamFree(stream);
    if (durationSeconds != nullptr && *durationSeconds <= 0.0 && !samples.isEmpty()) {
        *durationSeconds =
            static_cast<double>(samples.size()) / static_cast<double>(sampleRate);
    }
    return samples;
}
#endif

QVector<float> decodeMonoSamples(
    const QString& trackPath,
    double* durationSeconds,
    WaveformDecodeBackend* backend)
{
    if (backend != nullptr) {
        *backend = WaveformDecodeBackend::None;
    }
#ifdef MIACODE_HAS_BASS_AUDIO
    const QVector<float> bassSamples = decodeMonoSamplesWithBass(trackPath, durationSeconds);
    if (!bassSamples.isEmpty()) {
        if (backend != nullptr) {
            *backend = WaveformDecodeBackend::Bass;
        }
        return bassSamples;
    }
#endif
    const QVector<float> miniaudioSamples = decodeMonoSamplesWithMiniaudio(trackPath, durationSeconds);
    if (!miniaudioSamples.isEmpty() && backend != nullptr) {
        *backend = WaveformDecodeBackend::Miniaudio;
    }
    return miniaudioSamples;
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

QString waveformTrackDebugId(const QString& normalizedTrackPath)
{
    return waveformDebugIdFromNormalizedPath(normalizedTrackPath);
}

QString waveformDataDebugSummary(const WaveformData& data)
{
    const int levelCount = data.levels.size();
    const WaveformLevel* topLevel = data.levels.isEmpty() ? nullptr : &data.levels.constFirst();
    const int topColumns = topLevel != nullptr ? topLevel->columns.size() : 0;
    const double topSecondsPerColumn = topLevel != nullptr ? topLevel->secondsPerColumn : 0.0;
    double peakEnergy = 0.0;
    const double peakSecond = topLevel != nullptr ? peakColumnSecond(*topLevel, &peakEnergy) : -1.0;
    const double onsetLow = topLevel != nullptr
        ? firstColumnSecondAtThreshold(*topLevel, kWaveformDiagThresholdLow)
        : -1.0;
    const double onsetMid = topLevel != nullptr
        ? firstColumnSecondAtThreshold(*topLevel, kWaveformDiagThresholdMid)
        : -1.0;
    const double onsetHigh = topLevel != nullptr
        ? firstColumnSecondAtThreshold(*topLevel, kWaveformDiagThresholdHigh)
        : -1.0;

    return QStringLiteral("track_id=%1 duration=%2 file_size=%3 mtime_ms=%4 levels=%5 top_cols=%6 top_spc_ms=%7 onset02=%8 onset05=%9 onset10=%10 peak_sec=%11 peak_amp=%12 empty=%13")
        .arg(waveformDebugIdFromNormalizedPath(data.normalizedTrackPath))
        .arg(data.durationSeconds, 0, 'f', 6)
        .arg(data.fileSize)
        .arg(data.lastModifiedMs)
        .arg(levelCount)
        .arg(topColumns)
        .arg(topSecondsPerColumn * 1000.0, 0, 'f', 3)
        .arg(onsetLow, 0, 'f', 6)
        .arg(onsetMid, 0, 'f', 6)
        .arg(onsetHigh, 0, 'f', 6)
        .arg(peakSecond, 0, 'f', 6)
        .arg(peakEnergy, 0, 'f', 6)
        .arg(data.isEmpty() ? 1 : 0);
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
    appendWaveformAlignmentDebugLog(
        QStringLiteral("event=placeholder %1")
            .arg(waveformDataDebugSummary(*data)));
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
    QElapsedTimer timer;
    timer.start();
    double durationSeconds = 0.0;
    WaveformDecodeBackend backend = WaveformDecodeBackend::None;
    const QVector<float> samples = decodeMonoSamples(trackPath, &durationSeconds, &backend);
    WaveformDataPtr data = buildWaveformData(
        normalizeTrackPath(trackPath),
        fileSize,
        lastModifiedMs,
        samples,
        durationSeconds);
    appendWaveformDebugLog(
        QStringLiteral("event=build decoder=%1 samples=%2 elapsed_ms=%3 %4")
            .arg(waveformDecodeBackendLabel(backend))
            .arg(samples.size())
            .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
            .arg(data ? waveformDataDebugSummary(*data) : QStringLiteral("data=0")));
    return data;
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
        appendWaveformDebugLog(
            QStringLiteral("event=request source=placeholder reason=missing_or_invalid track_id=%1 path_empty=%2")
                .arg(waveformDebugIdFromNormalizedPath(normalizedTrackPath))
                .arg(trackPath.isEmpty() ? 1 : 0));
        if (callback) {
            callback(makeWaveformPlaceholder(0.0));
        }
        return;
    }

    const qint64 fileSize = trackInfo.size();
    const qint64 lastModifiedMs = trackInfo.lastModified().toMSecsSinceEpoch();
    const QString cacheKey = cacheKeyForTrack(normalizedTrackPath, fileSize, lastModifiedMs);
    if (memoryCache_.contains(cacheKey)) {
        const WaveformDataPtr cached = memoryCache_.value(cacheKey);
        appendWaveformDebugLog(
            QStringLiteral("event=request source=memory cache_key_hit=1 cache_file=%1 %2")
                .arg(waveformCacheFilePath(normalizedTrackPath, cacheDirectoryPath).isEmpty() ? 0 : 1)
                .arg(cached ? waveformDataDebugSummary(*cached) : QStringLiteral("data=0")));
        if (callback) {
            callback(cached);
        }
        return;
    }

    if (pendingRequests_.contains(cacheKey)) {
        appendWaveformDebugLog(
            QStringLiteral("event=request source=pending track_id=%1 file_size=%2 mtime_ms=%3")
                .arg(waveformDebugIdFromNormalizedPath(normalizedTrackPath))
                .arg(fileSize)
                .arg(lastModifiedMs));
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
    appendWaveformDebugLog(
        QStringLiteral("event=request source=worker track_id=%1 file_size=%2 mtime_ms=%3 cache_file=%4 cache_dir=%5")
            .arg(waveformDebugIdFromNormalizedPath(normalizedTrackPath))
            .arg(fileSize)
            .arg(lastModifiedMs)
            .arg(request->cacheFilePath.isEmpty() ? 0 : 1)
            .arg(cacheDirectoryPath.isEmpty() ? 0 : 1));

    QPointer<WaveformCacheService> guard(this);
    QThreadPool* const pool = threadPool_ != nullptr ? threadPool_ : QThreadPool::globalInstance();
    pool->start([guard, request]() {
        QElapsedTimer timer;
        timer.start();
        WaveformDataPtr data;
        QString source = QStringLiteral("disk_miss");
        if (!request->cacheFilePath.isEmpty()) {
            data = readWaveformDataCache(
                request->cacheFilePath,
                request->normalizedTrackPath,
                request->fileSize,
                request->lastModifiedMs);
            if (data) {
                source = QStringLiteral("disk");
            }
        }
        if (!data) {
            source = QStringLiteral("build");
            data = buildWaveformDataFromFile(
                request->trackPath,
                request->fileSize,
                request->lastModifiedMs);
            if (data && !data->levels.isEmpty() && !request->cacheFilePath.isEmpty()) {
                const bool wrote = writeWaveformDataCache(request->cacheFilePath, *data);
                appendWaveformDebugLog(
                    QStringLiteral("event=cache_write ok=%1 cache_file=1 %2")
                        .arg(wrote ? 1 : 0)
                        .arg(waveformDataDebugSummary(*data)));
            }
        }
        if (!data) {
            source = QStringLiteral("placeholder");
            data = makeWaveformPlaceholder(0.0);
        }
        appendWaveformDebugLog(
            QStringLiteral("event=worker_done source=%1 elapsed_ms=%2 cache_file=%3 %4")
                .arg(source)
                .arg(timer.nsecsElapsed() / 1000000.0, 0, 'f', 3)
                .arg(request->cacheFilePath.isEmpty() ? 0 : 1)
                .arg(data ? waveformDataDebugSummary(*data) : QStringLiteral("data=0")));
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
