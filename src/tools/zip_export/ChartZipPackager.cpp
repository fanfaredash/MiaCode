#include "ChartZipPackager.h"

#include "common/ChartAssetPaths.h"

#include <miniz.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <cstring>

namespace miacode::zip_export {
namespace {

// One thing to write into the archive. `sourcePath` empty means the
// payload is supplied inline via `inlineBytes` (used for maidata.txt).
struct ZipJob {
    QString archiveName;
    QString sourcePath;
    QByteArray inlineBytes;
    bool fromMemory = false;
};

bool looksLikeBackupName(const QString& fileName)
{
    const QString stem = QFileInfo(fileName).completeBaseName();
    return stem.endsWith(QStringLiteral("_bak"), Qt::CaseInsensitive);
}

// Append a sibling asset if it resolved to an existing file that is not a
// backup. Keeps the original filename so unpacking still satisfies the
// sibling-resolution rules.
void appendSiblingAsset(QVector<ZipJob>& jobs, const QString& resolvedPath)
{
    if (resolvedPath.isEmpty()) {
        return;
    }
    const QString fileName = QFileInfo(resolvedPath).fileName();
    if (looksLikeBackupName(fileName)) {
        return;
    }
    jobs.append(ZipJob{fileName, resolvedPath, QByteArray(), false});
}

}  // namespace

QString sanitizedZipStem(const QString& title)
{
    QString text = title.trimmed();
    QString sanitized;
    sanitized.reserve(text.size());
    for (const QChar ch : text) {
        if (ch.isNull() || ch.isLowSurrogate() || ch.isHighSurrogate()) {
            continue;
        }
        if (ch.unicode() < 0x20 || QStringLiteral("<>:\"/\\|?*").contains(ch)) {
            sanitized.append(QLatin1Char('_'));
            continue;
        }
        sanitized.append(ch);
    }
    sanitized = sanitized.trimmed();
    while (!sanitized.isEmpty()
           && (sanitized.endsWith(QLatin1Char('.')) || sanitized.endsWith(QLatin1Char(' ')))) {
        sanitized.chop(1);
    }
    return sanitized.isEmpty() ? QStringLiteral("chart") : sanitized;
}

ChartZipResult packChartToZip(const ChartZipInput& input, const ChartZipProgressFn& progress)
{
    ChartZipResult result;

    if (input.outputZipPath.trimmed().isEmpty()) {
        result.errorMessage = QStringLiteral("No output path was provided.");
        return result;
    }

    QVector<ZipJob> jobs;

    // 1) maidata.txt — always present; the chart body is the one asset we
    //    cannot do without, so an empty body is the only fatal case.
    if (input.chartText.isEmpty()) {
        result.errorMessage = QStringLiteral("The chart is empty; nothing to package.");
        return result;
    }
    jobs.append(ZipJob{QStringLiteral("maidata.txt"), QString(), input.chartText.toUtf8(), true});

    // 2) Sibling assets resolved through the shared chart-assets rules.
    if (!input.chartPath.isEmpty()) {
        using namespace miacode::chart_assets;
        const QString chartDir = QFileInfo(input.chartPath).absolutePath();

        appendSiblingAsset(jobs, resolveTrackPath(input.chartPath));

        // Background image only (no video candidates here) — exactly one.
        appendSiblingAsset(jobs, resolveBackgroundMediaPath(input.chartPath, /*includeVideoCandidates=*/false));

        // PV video — honor &video=, but only collect it when it is an
        // actual video that lives next to the chart. A &video= target
        // outside the chart folder is intentionally skipped.
        const QString videoPath = resolveChartVideoPath(input.chartPath, input.videoFieldValue);
        if (isVideoBackgroundPath(videoPath)) {
            const QString videoDir = QFileInfo(videoPath).absolutePath();
            if (QDir(videoDir) == QDir(chartDir)) {
                appendSiblingAsset(jobs, videoPath);
            }
        }
    }

    const int total = static_cast<int>(jobs.size());

    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    const QByteArray outUtf8 = QDir::toNativeSeparators(input.outputZipPath).toUtf8();
    if (!mz_zip_writer_init_file(&zip, outUtf8.constData(), 0)) {
        result.errorMessage = QStringLiteral("Could not create the .zip file:\n%1").arg(input.outputZipPath);
        return result;
    }

    const auto abortAndRemove = [&]() {
        mz_zip_writer_end(&zip);
        QFile::remove(input.outputZipPath);
    };

    for (int index = 0; index < jobs.size(); ++index) {
        const ZipJob& job = jobs.at(index);

        if (progress && !progress(index + 1, total, job.archiveName)) {
            abortAndRemove();
            result.canceled = true;
            return result;
        }

        const QByteArray nameUtf8 = job.archiveName.toUtf8();
        bool added = false;
        if (job.fromMemory) {
            // Text compresses well; use a real compression level.
            added = mz_zip_writer_add_mem(
                &zip,
                nameUtf8.constData(),
                job.inlineBytes.constData(),
                static_cast<size_t>(job.inlineBytes.size()),
                MZ_BEST_COMPRESSION);
        } else {
            // Media (mp3/jpg/mp4) is already compressed — store it (level
            // 0) so packing a large PV stays fast and doesn't bloat CPU.
            const QByteArray srcUtf8 = QDir::toNativeSeparators(job.sourcePath).toUtf8();
            added = mz_zip_writer_add_file(
                &zip,
                nameUtf8.constData(),
                srcUtf8.constData(),
                nullptr,
                0,
                MZ_NO_COMPRESSION);
        }

        if (!added) {
            abortAndRemove();
            result.errorMessage = QStringLiteral("Failed to add to the archive:\n%1").arg(job.archiveName);
            return result;
        }
        result.includedEntries.append(job.archiveName);
    }

    if (!mz_zip_writer_finalize_archive(&zip)) {
        abortAndRemove();
        result.errorMessage = QStringLiteral("Failed to finalize the .zip archive.");
        return result;
    }
    mz_zip_writer_end(&zip);

    result.ok = true;
    return result;
}

}  // namespace miacode::zip_export
