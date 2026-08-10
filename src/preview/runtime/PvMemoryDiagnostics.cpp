#include "preview/runtime/PvMemoryDiagnostics.h"

#include <algorithm>

namespace miacode::preview::pv_memory {

namespace {

qint64 peakValue(qint64 previous, qint64 current)
{
    if (current < 0) {
        return previous;
    }
    return previous < 0 ? current : std::max(previous, current);
}

void updatePeak(ProcessMemorySample* peak, const ProcessMemorySample& current)
{
    peak->residentBytes = peakValue(peak->residentBytes, current.residentBytes);
    peak->footprintBytes = peakValue(peak->footprintBytes, current.footprintBytes);
    peak->internalBytes = peakValue(peak->internalBytes, current.internalBytes);
    peak->compressedBytes = peakValue(peak->compressedBytes, current.compressedBytes);
}

QString actionName(RecordAction action)
{
    switch (action) {
    case RecordAction::Boundary:
        return QStringLiteral("boundary");
    case RecordAction::Sample:
        return QStringLiteral("sample");
    case RecordAction::Summary:
        return QStringLiteral("summary");
    }
    return QStringLiteral("boundary");
}

QString scalarValue(QString value)
{
    for (QChar& character : value) {
        if (character.isSpace() || character == QLatin1Char('=') || character == QLatin1Char('/')
            || character == QLatin1Char('\\') || character == QLatin1Char(':')) {
            character = QLatin1Char('_');
        }
    }
    return value.isEmpty() ? QStringLiteral("unknown") : value;
}

}  // namespace

void Diagnostics::update(SourceState* state, const Observation& observation)
{
    if (state == nullptr) {
        return;
    }
    state->media = observation.media;
    state->lastElapsedMs = observation.elapsedMs;
    state->lastProcess = observation.process;
    updatePeak(&state->peakProcess, observation.process);
}

QVector<Record> Diagnostics::beginSource(MediaKind kind, const Observation& observation)
{
    QVector<Record> records;
    if (pendingClear_) {
        pendingClear_->checkpointsCanceled = true;
        records.append(finalizeClearSnapshot(*pendingClear_));
        pendingClear_.reset();
    }
    if (current_) {
        records.append(makeRecord(
            *current_, RecordAction::Summary, QStringLiteral("replace"), current_->lastProcess));
        current_.reset();
    }
    if (kind != MediaKind::Video) {
        return records;
    }
    SourceState state;
    state.sourceId = nextSourceId_++;
    state.startElapsedMs = observation.elapsedMs;
    state.lastElapsedMs = observation.elapsedMs;
    state.media = observation.media;
    state.firstProcess = observation.process;
    state.lastProcess = observation.process;
    state.peakProcess = observation.process;
    current_ = state;
    records.append(makeRecord(
        *current_, RecordAction::Boundary, QStringLiteral("source_load"), observation.process));
    return records;
}

QVector<Record> Diagnostics::observeFrame(
    const Observation& observation,
    const FrameMetadata& frame,
    const ImageConversionFact& conversion)
{
    if (!current_) {
        return {};
    }
    update(&*current_, observation);
    current_->frameCount += 1;
    current_->frame = frame;
    if (conversion.attempted) {
        current_->toImageAttempts += 1;
        current_->toImageTotalMs += std::max<qint64>(0, conversion.elapsedMs);
        current_->toImageMaxMs = std::max(current_->toImageMaxMs, conversion.elapsedMs);
        current_->toImageLastBytes = std::max<qint64>(0, conversion.resultBytes);
        if (conversion.succeeded) {
            current_->toImageSuccesses += 1;
            current_->toImagePeakBytes = std::max(current_->toImagePeakBytes, current_->toImageLastBytes);
            current_->toImageOutputBytesEstimate += current_->toImageLastBytes;
        } else {
            current_->toImageNulls += 1;
        }
    }
    if (current_->frameCount != 1) {
        return {};
    }
    return {makeRecord(
        *current_, RecordAction::Boundary, QStringLiteral("first_frame"), observation.process)};
}

std::optional<Record> Diagnostics::boundary(BoundaryReason reason, const Observation& observation)
{
    if (!current_) {
        return std::nullopt;
    }
    update(&*current_, observation);
    return makeRecord(*current_, RecordAction::Boundary, boundaryReasonName(reason), observation.process);
}

std::optional<Record> Diagnostics::sample(const Observation& observation)
{
    if (!current_) {
        return std::nullopt;
    }
    update(&*current_, observation);
    return makeRecord(*current_, RecordAction::Sample, QStringLiteral("periodic"), observation.process);
}

QVector<Record> Diagnostics::clear(const Observation& observation)
{
    if (!current_) {
        return {};
    }
    update(&*current_, observation);
    QVector<Record> records;
    records.append(makeRecord(
        *current_, RecordAction::Boundary, QStringLiteral("clear_before"), observation.process));

    ClearSnapshot snapshot;
    snapshot.state = *current_;
    snapshot.clearEpoch = ++clearEpoch_;
    current_.reset();
    pendingClear_ = snapshot;
    records.append(makeRecord(
        pendingClear_->state,
        RecordAction::Boundary,
        QStringLiteral("clear_after"),
        observation.process,
        pendingClear_->clearEpoch,
        true));
    return records;
}

QVector<Record> Diagnostics::lateNoMedia(const ProcessMemorySample& process)
{
    if (!pendingClear_ || pendingClear_->noMediaObserved) {
        return {};
    }
    pendingClear_->noMediaObserved = true;
    QVector<Record> records;
    records.append(makeRecord(
        pendingClear_->state,
        RecordAction::Boundary,
        QStringLiteral("no_media"),
        process,
        pendingClear_->clearEpoch,
        true,
        true));
    records.append(finalizeClearSnapshot(*pendingClear_));
    pendingClear_.reset();
    return records;
}

QVector<Record> Diagnostics::postClearCheckpoint(
    quint64 clearEpoch,
    qint64 delayMs,
    const ProcessMemorySample& process)
{
    if (!pendingClear_ || pendingClear_->checkpointsCanceled || pendingClear_->clearEpoch != clearEpoch) {
        return {};
    }
    QString reason;
    if (delayMs == kPostClear3SecondsMs) {
        if (pendingClear_->checkpoint3sEmitted) {
            return {};
        }
        pendingClear_->checkpoint3sEmitted = true;
        reason = QStringLiteral("post_clear_3s");
    } else if (delayMs == kPostClear15SecondsMs) {
        if (pendingClear_->checkpoint15sEmitted) {
            return {};
        }
        pendingClear_->checkpoint15sEmitted = true;
        reason = QStringLiteral("post_clear_15s");
    } else {
        return {};
    }
    QVector<Record> records;
    records.append(makeRecord(
        pendingClear_->state,
        RecordAction::Boundary,
        reason,
        process,
        pendingClear_->clearEpoch,
        true,
        pendingClear_->noMediaObserved));
    if (pendingClear_->checkpoint3sEmitted && pendingClear_->checkpoint15sEmitted) {
        records.append(finalizeClearSnapshot(*pendingClear_));
        pendingClear_.reset();
    }
    return records;
}

QVector<Record> Diagnostics::recover(
    MediaKind replacementKind,
    const Observation& oldObservation,
    const Observation& replacementObservation)
{
    QVector<Record> records;
    if (current_) {
        update(&*current_, oldObservation);
        records.append(makeRecord(
            *current_, RecordAction::Summary, QStringLiteral("recovery"), oldObservation.process));
        current_.reset();
    }
    records += beginSource(replacementKind, replacementObservation);
    return records;
}

std::optional<Record> Diagnostics::destroy(const Observation& observation)
{
    if (!current_) {
        if (!pendingClear_) {
            return std::nullopt;
        }
        const Record summary = finalizeClearSnapshot(*pendingClear_);
        pendingClear_.reset();
        return summary;
    }
    update(&*current_, observation);
    const Record summary = makeRecord(
        *current_, RecordAction::Summary, QStringLiteral("destroy"), observation.process);
    current_.reset();
    return summary;
}

bool Diagnostics::hasCurrentSource() const
{
    return current_.has_value();
}

QString Diagnostics::boundaryReasonName(BoundaryReason reason)
{
    switch (reason) {
    case BoundaryReason::Play:
        return QStringLiteral("play");
    case BoundaryReason::Pause:
        return QStringLiteral("pause");
    case BoundaryReason::EndOfMedia:
        return QStringLiteral("end_of_media");
    case BoundaryReason::OutputAttach:
        return QStringLiteral("output_attach");
    case BoundaryReason::OutputDetach:
        return QStringLiteral("output_detach");
    case BoundaryReason::PlayerDestroyBefore:
        return QStringLiteral("player_destroy_before");
    case BoundaryReason::PlayerDestroyAfter:
        return QStringLiteral("player_destroy_after");
    }
    return QStringLiteral("unknown");
}

Record Diagnostics::makeRecord(
    const SourceState& state,
    RecordAction action,
    const QString& reason,
    const ProcessMemorySample& currentProcess,
    quint64 clearEpoch,
    bool snapshot,
    bool noMediaAfterClear)
{
    Record record;
    record.action = action;
    record.reason = reason;
    record.sourceId = state.sourceId;
    record.clearEpoch = clearEpoch;
    record.snapshot = snapshot;
    record.noMediaAfterClear = noMediaAfterClear;
    record.sourceElapsedMs = std::max<qint64>(0, state.lastElapsedMs - state.startElapsedMs);
    record.media = state.media;
    record.frameCount = state.frameCount;
    record.frame = state.frame;
    record.toImageAttempts = state.toImageAttempts;
    record.toImageSuccesses = state.toImageSuccesses;
    record.toImageNulls = state.toImageNulls;
    record.toImageTotalMs = state.toImageTotalMs;
    record.toImageMaxMs = state.toImageMaxMs;
    record.toImageLastBytes = state.toImageLastBytes;
    record.toImagePeakBytes = state.toImagePeakBytes;
    record.toImageOutputBytesEstimate = state.toImageOutputBytesEstimate;
    record.process = currentProcess;
    record.firstProcess = state.firstProcess;
    record.lastProcess = state.lastProcess;
    record.peakProcess = state.peakProcess;
    return record;
}

Record Diagnostics::finalizeClearSnapshot(const ClearSnapshot& snapshot)
{
    return makeRecord(
        snapshot.state,
        RecordAction::Summary,
        QStringLiteral("clear"),
        snapshot.state.lastProcess,
        snapshot.clearEpoch,
        true,
        snapshot.noMediaObserved);
}

QString Diagnostics::formatPayload(const Record& record)
{
    const double frameRate = record.sourceElapsedMs > 0
        ? static_cast<double>(record.frameCount) * 1000.0 / static_cast<double>(record.sourceElapsedMs)
        : 0.0;
    return QStringLiteral(
               "action=%1 reason=%2 pv_memory_source_id=%3 clear_epoch=%4 snapshot=%5 no_media_after_clear=%6 "
               "source_elapsed_ms=%7 media_visible=%8 playback_state=%9 media_status=%10 position_ms=%11 "
               "frame_count=%12 frame_rate_estimate=%13 frame_width=%14 frame_height=%15 pixel_format=%16 "
               "video_output_attached=%17 video_sink_attached=%18 to_image_attempts=%19 to_image_successes=%20 "
               "to_image_nulls=%21 to_image_total_ms=%22 to_image_max_ms=%23 to_image_last_bytes=%24 "
               "to_image_peak_bytes=%25 to_image_output_bytes_estimate=%26 process_resident_bytes=%27 "
               "process_footprint_bytes=%28 process_internal_bytes=%29 process_compressed_bytes=%30 "
               "process_first_resident_bytes=%31 process_first_footprint_bytes=%32 process_first_internal_bytes=%33 "
               "process_first_compressed_bytes=%34 process_last_resident_bytes=%35 process_last_footprint_bytes=%36 "
               "process_last_internal_bytes=%37 process_last_compressed_bytes=%38 process_peak_resident_bytes=%39 "
               "process_peak_footprint_bytes=%40 process_peak_internal_bytes=%41 process_peak_compressed_bytes=%42")
        .arg(actionName(record.action))
        .arg(scalarValue(record.reason))
        .arg(record.sourceId)
        .arg(record.clearEpoch)
        .arg(record.snapshot ? 1 : 0)
        .arg(record.noMediaAfterClear ? 1 : 0)
        .arg(record.sourceElapsedMs)
        .arg(record.media.mediaVisible ? 1 : 0)
        .arg(scalarValue(record.media.playbackState))
        .arg(scalarValue(record.media.mediaStatus))
        .arg(record.media.positionMs)
        .arg(record.frameCount)
        .arg(frameRate, 0, 'f', 3)
        .arg(record.frame.width)
        .arg(record.frame.height)
        .arg(scalarValue(record.frame.pixelFormat))
        .arg(record.media.videoOutputAttached ? 1 : 0)
        .arg(record.media.videoSinkAttached ? 1 : 0)
        .arg(record.toImageAttempts)
        .arg(record.toImageSuccesses)
        .arg(record.toImageNulls)
        .arg(record.toImageTotalMs)
        .arg(record.toImageMaxMs)
        .arg(record.toImageLastBytes)
        .arg(record.toImagePeakBytes)
        .arg(record.toImageOutputBytesEstimate)
        .arg(record.process.residentBytes)
        .arg(record.process.footprintBytes)
        .arg(record.process.internalBytes)
        .arg(record.process.compressedBytes)
        .arg(record.firstProcess.residentBytes)
        .arg(record.firstProcess.footprintBytes)
        .arg(record.firstProcess.internalBytes)
        .arg(record.firstProcess.compressedBytes)
        .arg(record.lastProcess.residentBytes)
        .arg(record.lastProcess.footprintBytes)
        .arg(record.lastProcess.internalBytes)
        .arg(record.lastProcess.compressedBytes)
        .arg(record.peakProcess.residentBytes)
        .arg(record.peakProcess.footprintBytes)
        .arg(record.peakProcess.internalBytes)
        .arg(record.peakProcess.compressedBytes);
}

}  // namespace miacode::preview::pv_memory
