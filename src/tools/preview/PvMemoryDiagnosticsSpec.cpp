// Contract spec for scalar-only PV-memory accounting. The production host owns
// video-frame conversion, timers, and logging; this helper receives only facts
// already computed by that host.

#include "preview/runtime/PvMemoryDiagnostics.h"

#include <QFile>
#include <QTextStream>

namespace {

using namespace miacode::preview::pv_memory;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

Observation observation(qint64 elapsedMs, qint64 residentBytes, qint64 footprintBytes = -1)
{
    Observation result;
    result.elapsedMs = elapsedMs;
    result.media.mediaVisible = true;
    result.media.playbackState = QStringLiteral("playing");
    result.media.mediaStatus = QStringLiteral("loaded");
    result.media.positionMs = elapsedMs;
    result.process.residentBytes = residentBytes;
    result.process.footprintBytes = footprintBytes;
    result.process.internalBytes = residentBytes < 0 ? -1 : residentBytes / 2;
    result.process.compressedBytes = residentBytes < 0 ? -1 : residentBytes / 4;
    return result;
}

const Record* findRecord(const QVector<Record>& records, RecordAction action, const QString& reason)
{
    for (const Record& record : records) {
        if (record.action == action && record.reason == reason) {
            return &record;
        }
    }
    return nullptr;
}

bool helperInterfaceIsScalarOnly(QTextStream& err)
{
    const QStringList helperFiles {
        QStringLiteral(MIACODE_SOURCE_ROOT "/src/preview/runtime/PvMemoryDiagnostics.h"),
        QStringLiteral(MIACODE_SOURCE_ROOT "/src/preview/runtime/PvMemoryDiagnostics.cpp"),
    };
    for (const QString& path : helperFiles) {
        QFile helperFile(path);
        if (!require(helperFile.open(QIODevice::ReadOnly), QStringLiteral("open scalar-only helper source"), err)) {
            return false;
        }
        const QByteArray source = helperFile.readAll();
        if (!require(!source.contains("QVideoFrame") && !source.contains("QImage")
                         && !source.contains("QVideoSink") && !source.contains("QMediaPlayer")
                         && !source.contains(".toImage("),
                     QStringLiteral("helper sources cannot receive media objects or invoke conversion"), err)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    ok &= helperInterfaceIsScalarOnly(err);

    // A source identifier is assigned only to active video loads and never reused.
    Diagnostics diagnostics;
    ok &= require(diagnostics.beginSource(MediaKind::Image, observation(0, 10)).isEmpty(),
                  QStringLiteral("image loads create no PV-memory source"), err);
    ok &= require(!diagnostics.boundary(BoundaryReason::Play, observation(1, 11)).has_value(),
                  QStringLiteral("inactive play creates no record"), err);
    const QVector<Record> firstLoad = diagnostics.beginSource(MediaKind::Video, observation(0, 10));
    ok &= require(firstLoad.size() == 1 && firstLoad.front().sourceId == 1
                      && firstLoad.front().reason == QStringLiteral("source_load"),
                  QStringLiteral("first video load receives source id 1"), err);
    const QVector<Record> replacement = diagnostics.beginSource(MediaKind::Video, observation(1, 11));
    const Record* replacedSummary = findRecord(replacement, RecordAction::Summary, QStringLiteral("replace"));
    const Record* secondLoad = findRecord(replacement, RecordAction::Boundary, QStringLiteral("source_load"));
    ok &= require(replacedSummary && secondLoad && replacedSummary->sourceId == 1
                      && secondLoad->sourceId == 2 && replacedSummary->media.positionMs == 0
                      && replacedSummary->process.residentBytes == 10,
                  QStringLiteral("replacement finalizes old source without applying new-source facts"), err);

    // Frame facts and precomputed image-conversion facts aggregate without touching a frame object.
    ImageConversionFact success;
    success.attempted = true;
    success.succeeded = true;
    success.elapsedMs = 4;
    success.resultBytes = 64;
    FrameMetadata frame;
    frame.width = 1920;
    frame.height = 1080;
    frame.pixelFormat = QStringLiteral("rgba8888");
    const QVector<Record> firstFrame = diagnostics.observeFrame(observation(100, 12), frame, success);
    ok &= require(firstFrame.size() == 1 && firstFrame.front().reason == QStringLiteral("first_frame"),
                  QStringLiteral("first supplied frame emits one first-frame boundary"), err);
    ImageConversionFact nullResult;
    nullResult.attempted = true;
    nullResult.succeeded = false;
    nullResult.elapsedMs = 6;
    nullResult.resultBytes = 0;
    diagnostics.observeFrame(observation(200, 9), frame, nullResult);
    const auto aggregate = diagnostics.boundary(BoundaryReason::OutputAttach, observation(300, 20, 30));
    ok &= require(aggregate && aggregate->frameCount == 2 && aggregate->frame.width == 1920
                      && aggregate->frame.height == 1080 && aggregate->frame.pixelFormat == QStringLiteral("rgba8888"),
                  QStringLiteral("frame metadata is retained as scalar facts"), err);
    ok &= require(aggregate && aggregate->toImageAttempts == 2 && aggregate->toImageSuccesses == 1
                      && aggregate->toImageNulls == 1 && aggregate->toImageTotalMs == 10
                      && aggregate->toImageMaxMs == 6 && aggregate->toImageLastBytes == 0
                      && aggregate->toImagePeakBytes == 64 && aggregate->toImageOutputBytesEstimate == 64,
                  QStringLiteral("supplied image-conversion counters aggregate exactly"), err);
    ok &= require(aggregate && aggregate->firstProcess.residentBytes == 11
                      && aggregate->lastProcess.residentBytes == 20
                      && aggregate->peakProcess.residentBytes == 20
                      && aggregate->peakProcess.footprintBytes == 30,
                  QStringLiteral("first last and peak process samples are tracked"), err);

    // Clear emits exactly one summary and preserves an immutable old-source snapshot.
    const QVector<Record> cleared = diagnostics.clear(observation(400, 40));
    const Record* clearBefore = findRecord(cleared, RecordAction::Boundary, QStringLiteral("clear_before"));
    const Record* clearSummary = findRecord(cleared, RecordAction::Summary, QStringLiteral("clear"));
    const Record* clearAfter = findRecord(cleared, RecordAction::Boundary, QStringLiteral("clear_after"));
    ok &= require(cleared.size() == 2 && clearBefore && !clearSummary && clearAfter
                      && clearAfter->snapshot && clearAfter->clearEpoch == 1,
                  QStringLiteral("clear leaves its one summary pending with an old-source snapshot"), err);
    ok &= require(diagnostics.clear(observation(401, 41)).isEmpty(),
                  QStringLiteral("inactive clear cannot write a second summary"), err);
    const QVector<Record> lateNoMedia = diagnostics.lateNoMedia(ProcessMemorySample {90, 91, 92, 93});
    const Record* noMediaBoundary = findRecord(lateNoMedia, RecordAction::Boundary, QStringLiteral("no_media"));
    const Record* noMediaSummary = findRecord(lateNoMedia, RecordAction::Summary, QStringLiteral("clear"));
    ok &= require(lateNoMedia.size() == 2 && noMediaBoundary && noMediaSummary
                      && noMediaBoundary->snapshot && noMediaBoundary->sourceId == 2
                      && noMediaBoundary->noMediaAfterClear && noMediaBoundary->frameCount == 2
                      && noMediaBoundary->process.residentBytes == 90
                      && noMediaBoundary->lastProcess.residentBytes == 40
                      && noMediaSummary->noMediaAfterClear,
                  QStringLiteral("late NoMedia binds the old snapshot and finalizes its summary with NoMedia"), err);
    ok &= require(diagnostics.lateNoMedia(ProcessMemorySample {91, 92, 93, 94}).isEmpty(),
                  QStringLiteral("late NoMedia is recorded at most once per clear snapshot"), err);

    // A newer source resolves the old pending summary and cancels its delayed checkpoints.
    Diagnostics cancellation;
    cancellation.beginSource(MediaKind::Video, observation(0, 49));
    cancellation.clear(observation(1, 50));
    const QVector<Record> canceled = cancellation.beginSource(MediaKind::Image, observation(2, 51));
    const Record* canceledSummary = findRecord(canceled, RecordAction::Summary, QStringLiteral("clear"));
    ok &= require(canceled.size() == 1 && canceledSummary && !canceledSummary->noMediaAfterClear
                      && cancellation.postClearCheckpoint(1, kPostClear3SecondsMs, ProcessMemorySample {52, 53, 54, 55}).isEmpty(),
                  QStringLiteral("new non-video source finalizes old clear and cancels its 3-second checkpoint"), err);

    // A current clear epoch permits both checkpoint boundaries; 15 seconds finalizes its summary.
    diagnostics.beginSource(MediaKind::Video, observation(550, 55));
    const QVector<Record> secondClear = diagnostics.clear(observation(600, 60));
    const Record* secondAfter = findRecord(secondClear, RecordAction::Boundary, QStringLiteral("clear_after"));
    const quint64 secondEpoch = secondAfter ? secondAfter->clearEpoch : 0;
    const QVector<Record> checkpoint3 = diagnostics.postClearCheckpoint(secondEpoch, kPostClear3SecondsMs, ProcessMemorySample {61, 62, 63, 64});
    const QVector<Record> checkpoint15 = diagnostics.postClearCheckpoint(secondEpoch, kPostClear15SecondsMs, ProcessMemorySample {62, 63, 64, 65});
    const Record* checkpoint3Boundary = findRecord(checkpoint3, RecordAction::Boundary, QStringLiteral("post_clear_3s"));
    const Record* checkpoint15Boundary = findRecord(checkpoint15, RecordAction::Boundary, QStringLiteral("post_clear_15s"));
    const Record* checkpoint15Summary = findRecord(checkpoint15, RecordAction::Summary, QStringLiteral("clear"));
    ok &= require(checkpoint3.size() == 1 && checkpoint15.size() == 2
                      && checkpoint3Boundary && checkpoint15Boundary && checkpoint15Summary
                      && checkpoint3Boundary->snapshot && checkpoint15Boundary->snapshot
                      && !checkpoint15Summary->noMediaAfterClear,
                  QStringLiteral("current clear epoch emits immutable checkpoints and finalizes at 15 seconds"), err);
    ok &= require(diagnostics.postClearCheckpoint(secondEpoch, kPostClear3SecondsMs, ProcessMemorySample {63, 64, 65, 66}).isEmpty()
                      && diagnostics.postClearCheckpoint(secondEpoch, kPostClear15SecondsMs, ProcessMemorySample {64, 65, 66, 67}).isEmpty(),
                  QStringLiteral("each delayed checkpoint is emitted at most once"), err);
    diagnostics.beginSource(MediaKind::Video, observation(700, 70));
    ok &= require(diagnostics.postClearCheckpoint(secondEpoch, kPostClear15SecondsMs, ProcessMemorySample {71, 72, 73, 74}).isEmpty(),
                  QStringLiteral("newer source cancels old 15-second checkpoint"), err);

    // Timer delivery is not ordered: 15s before 3s retains the old snapshot and
    // delays its sole summary until both checkpoint boundaries exist.
    Diagnostics reversedCheckpoints;
    reversedCheckpoints.beginSource(MediaKind::Video, observation(0, 80));
    const QVector<Record> reversedClear = reversedCheckpoints.clear(observation(1, 81));
    const Record* reversedAfter = findRecord(reversedClear, RecordAction::Boundary, QStringLiteral("clear_after"));
    const quint64 reversedEpoch = reversedAfter ? reversedAfter->clearEpoch : 0;
    const QVector<Record> reversed15 = reversedCheckpoints.postClearCheckpoint(
        reversedEpoch, kPostClear15SecondsMs, ProcessMemorySample {82, 83, 84, 85});
    const QVector<Record> reversed3 = reversedCheckpoints.postClearCheckpoint(
        reversedEpoch, kPostClear3SecondsMs, ProcessMemorySample {83, 84, 85, 86});
    const Record* reversed15Boundary = findRecord(reversed15, RecordAction::Boundary, QStringLiteral("post_clear_15s"));
    const Record* reversed3Boundary = findRecord(reversed3, RecordAction::Boundary, QStringLiteral("post_clear_3s"));
    const Record* reversedSummary = findRecord(reversed3, RecordAction::Summary, QStringLiteral("clear"));
    ok &= require(reversed15.size() == 1 && reversed15Boundary && reversed15Boundary->snapshot
                      && !findRecord(reversed15, RecordAction::Summary, QStringLiteral("clear"))
                      && reversed3.size() == 2 && reversed3Boundary && reversed3Boundary->snapshot
                      && reversedSummary && !reversedSummary->noMediaAfterClear,
                  QStringLiteral("reversed checkpoints retain both snapshots and emit one summary only after both"), err);

    // Recovery is a hard accounting boundary: old source summarized, replacement gets a fresh id.
    Diagnostics recovery;
    recovery.beginSource(MediaKind::Video, observation(0, 1));
    const QVector<Record> recovered = recovery.recover(MediaKind::Video, observation(9, 2), observation(10, 3));
    const Record* recoverySummary = findRecord(recovered, RecordAction::Summary, QStringLiteral("recovery"));
    const Record* recoveryLoad = findRecord(recovered, RecordAction::Boundary, QStringLiteral("source_load"));
    ok &= require(recoverySummary && recoveryLoad && recoverySummary->sourceId == 1
                      && recoveryLoad->sourceId == 2,
                  QStringLiteral("recovery finalizes old load and begins a fresh source"), err);

    Diagnostics destruction;
    destruction.beginSource(MediaKind::Video, observation(0, 1));
    const auto destroySummary = destruction.destroy(observation(12, 2));
    ok &= require(destroySummary && destroySummary->action == RecordAction::Summary
                      && destroySummary->reason == QStringLiteral("destroy")
                      && destroySummary->sourceId == 1
                      && !destruction.destroy(observation(13, 3)).has_value(),
                  QStringLiteral("destruction finalizes an active source exactly once"), err);

    Diagnostics pendingDestruction;
    pendingDestruction.beginSource(MediaKind::Video, observation(0, 1));
    pendingDestruction.clear(observation(1, 2));
    const auto pendingDestroySummary = pendingDestruction.destroy(observation(2, 3));
    ok &= require(pendingDestroySummary && pendingDestroySummary->reason == QStringLiteral("clear")
                      && !pendingDestroySummary->noMediaAfterClear
                      && !pendingDestruction.destroy(observation(3, 4)).has_value(),
                  QStringLiteral("destruction finalizes a pending clear summary exactly once"), err);

    const QString payload = Diagnostics::formatPayload(*aggregate);
    ok &= require(payload.startsWith(QStringLiteral("action=boundary reason=output_attach pv_memory_source_id=2"))
                      && payload.contains(QStringLiteral("to_image_output_bytes_estimate=64"))
                      && payload.contains(QStringLiteral("process_peak_resident_bytes=20"))
                      && !payload.contains(QLatin1Char('/')),
                  QStringLiteral("payload uses stable key=value fields without paths"), err);
    Record pathProbe = *aggregate;
    pathProbe.media.mediaStatus = QStringLiteral("C:\\private\\pv.mov");
    const QString safePayload = Diagnostics::formatPayload(pathProbe);
    ok &= require(!safePayload.contains(QLatin1Char('\\')) && !safePayload.contains(QLatin1Char(':')),
                  QStringLiteral("payload sanitizes accidental platform-path values"), err);

    if (ok) {
        out << "PV memory diagnostics spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
