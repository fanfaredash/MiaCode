#pragma once

#include "DebugOptions.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QImage>
#include <QString>
#include <QtGlobal>

namespace miacode::debug_image_compare {

struct ImageIntensityStats {
    double meanLuma = -1.0;
    double nearBlackRatio = -1.0;
};

enum class DumpStatus {
    Disabled,
    LimitSkipped,
    Failed,
    Dumped,
};

struct DumpOutcome {
    DumpStatus status = DumpStatus::Disabled;
    QString sampleDirPath;
    QString error;
};

inline QImage asRgba8888(const QImage& image)
{
    if (image.format() == QImage::Format_RGBA8888) {
        return image;
    }
    return image.convertToFormat(QImage::Format_RGBA8888);
}

inline double meanAbsDiffNormalized(const QImage& lhs, const QImage& rhs)
{
    const QImage a = asRgba8888(lhs);
    const QImage b = asRgba8888(rhs);
    if (a.size() != b.size() || a.width() <= 0 || a.height() <= 0) {
        return -1.0;
    }

    qint64 totalAbs = 0;
    const int width = a.width();
    const int height = a.height();
    const int rowBytes = width * 4;
    for (int y = 0; y < height; ++y) {
        const uchar* rowA = a.constScanLine(y);
        const uchar* rowB = b.constScanLine(y);
        for (int x = 0; x < rowBytes; ++x) {
            totalAbs += qAbs(static_cast<int>(rowA[x]) - static_cast<int>(rowB[x]));
        }
    }

    const double denom = static_cast<double>(width) * height * 4.0 * 255.0;
    return denom > 0.0 ? static_cast<double>(totalAbs) / denom : -1.0;
}

inline quint64 sampledFrameSignature(const QImage& frame)
{
    const QImage rgba = asRgba8888(frame);
    const int width = rgba.width();
    const int height = rgba.height();
    if (width <= 0 || height <= 0) {
        return 0;
    }

    const int stepX = qMax(1, width / 16);
    const int stepY = qMax(1, height / 16);
    quint64 hash = 1469598103934665603ULL;
    for (int y = 0; y < height; y += stepY) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < width; x += stepX) {
            const uchar* px = row + (x * 4);
            hash ^= static_cast<quint64>(px[0]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(px[1]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(px[2]);
            hash *= 1099511628211ULL;
            hash ^= static_cast<quint64>(px[3]);
            hash *= 1099511628211ULL;
        }
    }
    hash ^= static_cast<quint64>(width);
    hash *= 1099511628211ULL;
    hash ^= static_cast<quint64>(height);
    hash *= 1099511628211ULL;
    return hash;
}

inline ImageIntensityStats analyzeImageIntensity(const QImage& frame)
{
    const QImage rgba = asRgba8888(frame);
    const int width = rgba.width();
    const int height = rgba.height();
    if (width <= 0 || height <= 0) {
        return {};
    }

    qint64 lumaSum = 0;
    qint64 nearBlackCount = 0;
    const qint64 pixelCount = static_cast<qint64>(width) * height;
    for (int y = 0; y < height; ++y) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < width; ++x) {
            const uchar* px = row + (x * 4);
            const int r = px[0];
            const int g = px[1];
            const int b = px[2];
            const int maxRgb = qMax(r, qMax(g, b));
            const int luma = (r * 77 + g * 150 + b * 29) >> 8;
            lumaSum += luma;
            if (maxRgb <= 16) {
                ++nearBlackCount;
            }
        }
    }

    ImageIntensityStats stats;
    stats.meanLuma = pixelCount > 0
        ? static_cast<double>(lumaSum) / (static_cast<double>(pixelCount) * 255.0)
        : -1.0;
    stats.nearBlackRatio = pixelCount > 0
        ? static_cast<double>(nearBlackCount) / static_cast<double>(pixelCount)
        : -1.0;
    return stats;
}

inline QImage absoluteDiffImage(const QImage& lhs, const QImage& rhs, int gain = 4)
{
    const QImage a = asRgba8888(lhs);
    const QImage b = asRgba8888(rhs);
    if (a.size() != b.size() || a.width() <= 0 || a.height() <= 0) {
        return QImage();
    }

    QImage diff(a.size(), QImage::Format_RGBA8888);
    if (diff.isNull()) {
        return QImage();
    }

    const int safeGain = qMax(1, gain);
    for (int y = 0; y < a.height(); ++y) {
        const uchar* rowA = a.constScanLine(y);
        const uchar* rowB = b.constScanLine(y);
        uchar* rowOut = diff.scanLine(y);
        for (int x = 0; x < a.width(); ++x) {
            const uchar* pxA = rowA + (x * 4);
            const uchar* pxB = rowB + (x * 4);
            uchar* pxOut = rowOut + (x * 4);
            pxOut[0] = static_cast<uchar>(qBound(0, qAbs(static_cast<int>(pxA[0]) - static_cast<int>(pxB[0])) * safeGain, 255));
            pxOut[1] = static_cast<uchar>(qBound(0, qAbs(static_cast<int>(pxA[1]) - static_cast<int>(pxB[1])) * safeGain, 255));
            pxOut[2] = static_cast<uchar>(qBound(0, qAbs(static_cast<int>(pxA[2]) - static_cast<int>(pxB[2])) * safeGain, 255));
            pxOut[3] = 255;
        }
    }
    return diff;
}

inline QString safePathToken(const QString& value)
{
    QString token = value.trimmed().toLower();
    if (token.isEmpty()) {
        return QStringLiteral("compare");
    }
    for (QChar& ch : token) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_') && ch != QLatin1Char('-')) {
            ch = QLatin1Char('_');
        }
    }
    return token;
}

inline QString compareDumpRootDirectory()
{
    const QString overrideDir = debug_options::previewCompareDumpDirectoryOverride();
    if (!overrideDir.isEmpty()) {
        return QDir::cleanPath(overrideDir);
    }

    static const QString defaultRoot = []() {
        const QString logDir = qEnvironmentVariable("MIACODE_LOG_DIR").trimmed();
        const QString baseDir = logDir.isEmpty() ? QDir::tempPath() : QDir::cleanPath(logDir);
        const QString sessionToken = QStringLiteral("preview_compare_%1_pid%2")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss_zzz")))
            .arg(QCoreApplication::applicationPid());
        return QDir(baseDir).filePath(sessionToken);
    }();
    return defaultRoot;
}

inline QString dumpStatusLabel(DumpStatus status)
{
    switch (status) {
    case DumpStatus::Disabled:
        return QStringLiteral("disabled");
    case DumpStatus::LimitSkipped:
        return QStringLiteral("limit");
    case DumpStatus::Failed:
        return QStringLiteral("failed");
    case DumpStatus::Dumped:
        return QStringLiteral("saved");
    }
    return QStringLiteral("unknown");
}

inline DumpOutcome dumpComparisonFrames(
    const QString& compareName,
    quint64 sampleIndex,
    const QImage& gpuFrame,
    const QImage& cpuFrame)
{
    DumpOutcome outcome;
    if (!debug_options::previewCompareDumpFramesEnabled()) {
        return outcome;
    }

    const int maxSamples = debug_options::previewCompareDumpMaxSamples();
    if (maxSamples > 0 && sampleIndex > static_cast<quint64>(maxSamples)) {
        outcome.status = DumpStatus::LimitSkipped;
        return outcome;
    }
    if (gpuFrame.isNull() || cpuFrame.isNull()) {
        outcome.status = DumpStatus::Failed;
        outcome.error = QStringLiteral("gpu/cpu frame missing");
        return outcome;
    }

    const QImage diffFrame = absoluteDiffImage(gpuFrame, cpuFrame);
    if (diffFrame.isNull()) {
        outcome.status = DumpStatus::Failed;
        outcome.error = QStringLiteral("failed to build diff frame");
        return outcome;
    }

    const QString compareDir = QDir(compareDumpRootDirectory()).filePath(safePathToken(compareName));
    const QString sampleDir = QDir(compareDir).filePath(
        QStringLiteral("sample_%1").arg(sampleIndex, 4, 10, QChar('0'))
    );
    if (!QDir().mkpath(sampleDir)) {
        outcome.status = DumpStatus::Failed;
        outcome.error = QStringLiteral("failed to create dump directory");
        return outcome;
    }

    const QString gpuPath = QDir(sampleDir).filePath(QStringLiteral("gpu.png"));
    const QString cpuPath = QDir(sampleDir).filePath(QStringLiteral("cpu.png"));
    const QString diffPath = QDir(sampleDir).filePath(QStringLiteral("abs_diff.png"));
    if (!gpuFrame.save(gpuPath, "PNG") || !cpuFrame.save(cpuPath, "PNG") || !diffFrame.save(diffPath, "PNG")) {
        outcome.status = DumpStatus::Failed;
        outcome.error = QStringLiteral("failed to save png files");
        return outcome;
    }

    outcome.status = DumpStatus::Dumped;
    outcome.sampleDirPath = sampleDir;
    return outcome;
}

}  // namespace miacode::debug_image_compare
