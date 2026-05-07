#include "preview/ipc/PreviewWorkerSupervisor.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "preview/ipc/PreviewSnapshotRingBuffer.h"
#include "preview/ipc/PreviewWorkerProtocol.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>
#include <QUuid>

#include <chrono>

namespace {

void appendSupervisorLog(const QString& tag, const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/worker_supervisor"),
        QStringLiteral("tag=%1 %2").arg(tag, payload)
    );
}

}  // namespace

namespace miacode::preview::ipc {

PreviewWorkerSupervisor::PreviewWorkerSupervisor(QObject* parent)
    : QObject(parent)
{
    respawnTimer_.setSingleShot(true);
    QObject::connect(&respawnTimer_, &QTimer::timeout,
                     this, &PreviewWorkerSupervisor::onRespawnTimerFired);

    healthyResetTimer_.setSingleShot(true);
    QObject::connect(&healthyResetTimer_, &QTimer::timeout,
                     this, &PreviewWorkerSupervisor::resetCrashRateLimitIfHealthy);

    // 60 Hz publisher cadence — matches the audio-tick rate the editor's
    // PreviewRuntime hits during playback. Phase 1 uses synthetic data;
    // Phase 4+ swaps this for a real PreviewRuntime publisher.
    syntheticPublisherTimer_.setInterval(17);  // ~60 Hz; close to 16.67 ms
    syntheticPublisherTimer_.setSingleShot(false);
    QObject::connect(&syntheticPublisherTimer_, &QTimer::timeout,
                     this, &PreviewWorkerSupervisor::onSyntheticPublisherTick);
}

PreviewWorkerSupervisor::~PreviewWorkerSupervisor()
{
    shutdown();
}

QString PreviewWorkerSupervisor::resolveWorkerExecutablePath() const
{
    // The worker is the same MiaCode binary, relaunched with a flag.
    // QCoreApplication::applicationFilePath returns the absolute path of
    // the running executable, which is the most reliable source —
    // path-search via PATH is unnecessary and would risk picking up a
    // different installed MiaCode.exe if multiple are on PATH.
    return QCoreApplication::applicationFilePath();
}

bool PreviewWorkerSupervisor::startProcess(bool staticTestMode, QString* errorMessage)
{
    if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("preview worker already running");
        }
        return false;
    }

    const QString program = resolveWorkerExecutablePath();
    if (program.isEmpty() || !QFileInfo::exists(program)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("worker executable not found at %1").arg(program);
        }
        return false;
    }

    process_ = new QProcess(this);
    process_->setProcessChannelMode(QProcess::SeparateChannels);
    QObject::connect(process_.data(), &QProcess::readyReadStandardOutput,
                     this, &PreviewWorkerSupervisor::onProcessReadyReadStandardOutput);
    QObject::connect(process_.data(), &QProcess::readyReadStandardError,
                     this, &PreviewWorkerSupervisor::onProcessReadyReadStandardError);
    QObject::connect(process_.data(), QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                     this, &PreviewWorkerSupervisor::onProcessFinished);

    QStringList args;
    args << (staticTestMode
             ? QStringLiteral("--preview-worker-static-test")
             : QStringLiteral("--preview-worker"));
    if (miacode::debug_options::debugModeEnabled()) {
        args << QStringLiteral("--debug");
    }

    // Redirect the worker's runtime log to a separate file in the same
    // dir as the editor's. Without this, the worker's QFile::open(append)
    // silently fails because the editor's AsyncLogWriter holds the
    // runtime log file exclusively on Windows. With separate filenames
    // both processes can write; operators can grep `_worker.log` for the
    // worker-side diagnostics. The fatal channel is left shared (low
    // rate, lock-contention rare; fatal logs are valuable to merge).
    QProcessEnvironment workerEnv = QProcessEnvironment::systemEnvironment();
    const QString editorLogDir = miacode::debug_log::logDirectory();
    QString workerRuntimeLogPath;
    QString workerFatalLogPath;
    if (!editorLogDir.isEmpty()) {
        workerRuntimeLogPath = QDir(editorLogDir).filePath(
            QStringLiteral("miacode_runtime_debug_worker.log"));
        workerFatalLogPath = QDir(editorLogDir).filePath(
            QStringLiteral("miacode_fatal_worker.log"));
        // MIACODE_LOG_DIR forces ALL channels to a specific dir for the
        // worker — covers Fatal, Runtime, etc. without needing a per-
        // channel override for each.
        workerEnv.insert(QStringLiteral("MIACODE_LOG_DIR"), editorLogDir);
        workerEnv.insert(QStringLiteral("MIACODE_RUNTIME_LOG_PATH"), workerRuntimeLogPath);
        workerEnv.insert(QStringLiteral("MIACODE_FATAL_LOG_PATH"), workerFatalLogPath);
    }
    process_->setProcessEnvironment(workerEnv);

    appendSupervisorLog(QStringLiteral("starting"),
                        QStringLiteral("program=%1 args=[%2] static_test=%3 worker_log=%4")
                            .arg(program, args.join(QChar(' ')))
                            .arg(staticTestMode ? 1 : 0)
                            .arg(workerRuntimeLogPath));

    lastSpawnStartedMonotonicNs_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                       std::chrono::steady_clock::now().time_since_epoch())
                                       .count();
    process_->start(program, args);
    if (!process_->waitForStarted(3000)) {
        const QString reason = process_->errorString();
        appendSupervisorLog(QStringLiteral("start_failed"),
                            QStringLiteral("error=%1").arg(reason));
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("preview worker start failed: %1").arg(reason);
        }
        process_->deleteLater();
        process_ = nullptr;
        return false;
    }

    return true;
}

bool PreviewWorkerSupervisor::spawnStaticTest(quint64 editorHwnd, QString* errorMessage)
{
    lastEditorHwnd_ = editorHwnd;
    lastSnapshotShmKey_.clear();
    lastSnapshotSlotByteSize_ = 0;
    lastSnapshotSlotCount_ = 0;
    lastStaticTestMode_ = true;
    if (lastSessionId_.isEmpty()) {
        lastSessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    if (!startProcess(true, errorMessage)) {
        return false;
    }

    sendCommand(buildAttachCommand(
        editorHwnd,
        static_cast<quint32>(QCoreApplication::applicationPid()),
        lastSessionId_,
        miacode::debug_log::logDirectory(),
        QString(),
        0,
        0));
    return true;
}

bool PreviewWorkerSupervisor::spawn(quint64 editorHwnd,
                                    const QString& snapshotShmKey,
                                    int snapshotSlotByteSize,
                                    int snapshotSlotCount,
                                    QString* errorMessage)
{
    lastEditorHwnd_ = editorHwnd;
    lastSnapshotShmKey_ = snapshotShmKey;
    lastSnapshotSlotByteSize_ = snapshotSlotByteSize;
    lastSnapshotSlotCount_ = snapshotSlotCount;
    lastStaticTestMode_ = false;
    if (lastSessionId_.isEmpty()) {
        lastSessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    }

    if (!startProcess(false, errorMessage)) {
        return false;
    }

    sendCommand(buildAttachCommand(
        editorHwnd,
        static_cast<quint32>(QCoreApplication::applicationPid()),
        lastSessionId_,
        miacode::debug_log::logDirectory(),
        snapshotShmKey,
        snapshotSlotByteSize,
        snapshotSlotCount));
    return true;
}

bool PreviewWorkerSupervisor::spawnWithSyntheticPublisher(quint64 editorHwnd, QString* errorMessage)
{
    if (syntheticRingBuffer_ == nullptr) {
        syntheticRingBuffer_ = std::make_unique<PreviewSnapshotRingBuffer>();
        QString ringError;
        if (!syntheticRingBuffer_->createAsPublisher(
                QStringLiteral("miacode_preview_synth"),
                kSnapshotRingDefaultSlots,
                &ringError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("synthetic ring buffer create failed: %1")
                                    .arg(ringError);
            }
            syntheticRingBuffer_.reset();
            return false;
        }
        appendSupervisorLog(QStringLiteral("synthetic_ring_created"),
                            QStringLiteral("key=%1 slot_bytes=%2 slot_count=%3")
                                .arg(syntheticRingBuffer_->key())
                                .arg(syntheticRingBuffer_->slotByteSize())
                                .arg(syntheticRingBuffer_->slotCount()));
    }

    syntheticPublishStartMs_ = QDateTime::currentMSecsSinceEpoch();
    syntheticPublishCount_ = 0;
    syntheticPublisherTimer_.start();

    return spawn(editorHwnd,
                 syntheticRingBuffer_->key(),
                 syntheticRingBuffer_->slotByteSize(),
                 syntheticRingBuffer_->slotCount(),
                 errorMessage);
}

bool PreviewWorkerSupervisor::spawnWithExternalPublisher(quint64 editorHwnd, QString* errorMessage)
{
    if (syntheticPublisherTimer_.isActive()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("synthetic publisher already active");
        }
        return false;
    }

    if (syntheticRingBuffer_ == nullptr) {
        syntheticRingBuffer_ = std::make_unique<PreviewSnapshotRingBuffer>();
        QString ringError;
        if (!syntheticRingBuffer_->createAsPublisher(
                QStringLiteral("miacode_preview_real"),
                kSnapshotRingDefaultSlots,
                &ringError)) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("real ring buffer create failed: %1")
                                    .arg(ringError);
            }
            syntheticRingBuffer_.reset();
            return false;
        }
        appendSupervisorLog(QStringLiteral("real_ring_created"),
                            QStringLiteral("key=%1 slot_bytes=%2 slot_count=%3")
                                .arg(syntheticRingBuffer_->key())
                                .arg(syntheticRingBuffer_->slotByteSize())
                                .arg(syntheticRingBuffer_->slotCount()));
    }

    return spawn(editorHwnd,
                 syntheticRingBuffer_->key(),
                 syntheticRingBuffer_->slotByteSize(),
                 syntheticRingBuffer_->slotCount(),
                 errorMessage);
}

bool PreviewWorkerSupervisor::publishSnapshot(PreviewFrameStateSerial& snapshotInOut)
{
    if (syntheticRingBuffer_ == nullptr) {
        return false;
    }
    return syntheticRingBuffer_->publish(snapshotInOut);
}

void PreviewWorkerSupervisor::onSyntheticPublisherTick()
{
    if (syntheticRingBuffer_ == nullptr || !syntheticRingBuffer_->isAttached()) {
        return;
    }
    // Heap-allocate to avoid blowing the timer-callback stack — the
    // POD is ~150 KB and would crash on Windows's default 1 MB stack
    // when the timer hits during nested Qt event dispatch.
    auto snapshotHeap = std::make_unique<PreviewFrameStateSerial>();
    PreviewFrameStateSerial& snapshot = *snapshotHeap;
    snapshot.layoutVersion = kSerialLayoutVersion;
    snapshot.spriteCount = 0;
    snapshot.stringBlobUsedBytes = 0;
    snapshot.markerGeometryBlobUsedBytes = 0;
    // Synthetic playhead — wall-time minutes past the publisher start, so
    // the worker-side CSV captures a smoothly-advancing value the operator
    // can sanity-check.
    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - syntheticPublishStartMs_;
    snapshot.playheadSeconds = static_cast<double>(elapsedMs) / 1000.0;
    snapshot.framePacingTargetFps = 60.0;
    // publish() overrides sequence + publishMonotonicNs internally — but
    // we set publishMonotonicNs first for symmetry with the future real
    // path that may want to encode the publish timestamp from a chosen
    // reference clock.
    snapshot.publishMonotonicNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                      std::chrono::steady_clock::now().time_since_epoch())
                                      .count();
    if (syntheticRingBuffer_->publish(snapshot)) {
        ++syntheticPublishCount_;
    }
}

void PreviewWorkerSupervisor::shutdown()
{
    respawnTimer_.stop();
    healthyResetTimer_.stop();
    syntheticPublisherTimer_.stop();

    if (process_ != nullptr && process_->state() != QProcess::NotRunning) {
        sendCommand(buildShutdownCommand());
        if (!process_->waitForFinished(kGracefulShutdownTimeoutMs)) {
            appendSupervisorLog(QStringLiteral("graceful_shutdown_timeout"),
                                QStringLiteral("forcing kill"));
            process_->kill();
            process_->waitForFinished(2000);
        }
    }
    if (process_ != nullptr) {
        process_->deleteLater();
        process_ = nullptr;
    }
    attached_ = false;
    popupHwnd_ = 0;
}

void PreviewWorkerSupervisor::setVisualTransform(int xPx, int yPx, int displayWPx, int displayHPx)
{
    // Dedup against the last sent values — callers fire this from per-tick
    // hooks (PreviewRuntime::frameStateChanged at audio rate). With dedup
    // the per-frame call collapses to a single int comparison when the
    // popup hasn't actually moved, which is the common case.
    const bool unchanged = lastVtValid_
                           && lastVtX_ == xPx
                           && lastVtY_ == yPx
                           && lastVtDisplayW_ == displayWPx
                           && lastVtDisplayH_ == displayHPx;

    lastVtX_ = xPx;
    lastVtY_ = yPx;
    lastVtDisplayW_ = displayWPx;
    lastVtDisplayH_ = displayHPx;
    lastVtValid_ = true;

    if (unchanged || !attached_) {
        return;
    }
    appendSupervisorLog(QStringLiteral("set_visual_transform_send"),
                        QStringLiteral("xy=%1,%2 wh=%3x%4")
                            .arg(xPx).arg(yPx).arg(displayWPx).arg(displayHPx));
    sendCommand(buildSetVisualTransformCommand(xPx, yPx, displayWPx, displayHPx));
}

bool PreviewWorkerSupervisor::isRunning() const
{
    return process_ != nullptr && process_->state() != QProcess::NotRunning;
}

bool PreviewWorkerSupervisor::isAttached() const
{
    return attached_;
}

quint64 PreviewWorkerSupervisor::popupHwnd() const
{
    return popupHwnd_;
}

void PreviewWorkerSupervisor::sendCommand(const QByteArray& payload)
{
    if (process_ == nullptr || process_->state() != QProcess::Running) {
        return;
    }
    process_->write(payload);
}

void PreviewWorkerSupervisor::onProcessReadyReadStandardOutput()
{
    if (process_ == nullptr) {
        return;
    }
    stdoutBuffer_.append(process_->readAllStandardOutput());
    int newlineIndex = stdoutBuffer_.indexOf('\n');
    while (newlineIndex >= 0) {
        const QByteArray line = stdoutBuffer_.left(newlineIndex).trimmed();
        stdoutBuffer_.remove(0, newlineIndex + 1);
        if (!line.isEmpty()) {
            parseStdoutLine(line);
        }
        newlineIndex = stdoutBuffer_.indexOf('\n');
    }
}

void PreviewWorkerSupervisor::onProcessReadyReadStandardError()
{
    if (process_ == nullptr) {
        return;
    }
    const QByteArray chunk = process_->readAllStandardError();
    if (chunk.isEmpty()) {
        return;
    }
    stderrBuffer_.append(chunk);
    constexpr int kStderrBufferCap = 32 * 1024;
    if (stderrBuffer_.size() > kStderrBufferCap) {
        stderrBuffer_.remove(0, stderrBuffer_.size() - kStderrBufferCap);
    }
    // Quote the chunk so newlines / non-printable characters survive the
    // log line. The first ~200 chars are usually enough to debug worker
    // startup issues; longer chunks fall through to the buffer cap.
    QString sample = QString::fromUtf8(chunk).trimmed();
    if (sample.size() > 200) {
        sample = sample.left(200) + QStringLiteral("...");
    }
    appendSupervisorLog(QStringLiteral("stderr_chunk"),
                        QStringLiteral("bytes=%1 sample=%2").arg(chunk.size()).arg(sample));
}

void PreviewWorkerSupervisor::parseStdoutLine(const QByteArray& line)
{
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        appendSupervisorLog(QStringLiteral("stdout_parse_failed"),
                            QStringLiteral("offset=%1 err=%2 line_size=%3")
                                .arg(parseError.offset)
                                .arg(parseError.errorString())
                                .arg(line.size()));
        return;
    }
    const QJsonObject obj = doc.object();
    const QString event = obj.value(QStringLiteral("event")).toString();

    if (event == QLatin1String(kEvtWorkerReady)) {
        appendSupervisorLog(QStringLiteral("worker_ready"),
                            QStringLiteral("protocol=%1")
                                .arg(obj.value(QStringLiteral("protocol")).toInt()));
        return;
    }

    if (event == QLatin1String(kEvtAttached)) {
        popupHwnd_ = static_cast<quint64>(obj.value(QStringLiteral("popup_hwnd")).toVariant().toLongLong());
        attached_ = true;
        // Replay last known visual transform so the new popup snaps to
        // the correct position immediately (relevant on respawn).
        if (lastVtValid_) {
            sendCommand(buildSetVisualTransformCommand(lastVtX_, lastVtY_, lastVtDisplayW_, lastVtDisplayH_));
        }
        // Phase 5 stress harness — measure exit-to-attach gap when we have
        // a prior exit timestamp. First attach (no prior exit) is skipped.
        if (lastWorkerExitMonotonicNs_ != 0) {
            const qint64 nowNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count();
            const qint64 totalMs = (nowNs - lastWorkerExitMonotonicNs_) / 1'000'000;
            const qint64 spawnMs = lastSpawnStartedMonotonicNs_ != 0
                                       ? (nowNs - lastSpawnStartedMonotonicNs_) / 1'000'000
                                       : totalMs;
            appendSupervisorLog(QStringLiteral("respawn_to_attach"),
                                QStringLiteral("total_ms=%1 spawn_to_attach_ms=%2")
                                    .arg(totalMs).arg(spawnMs));
            emit workerRespawnTimeRecorded(totalMs, spawnMs);
            lastWorkerExitMonotonicNs_ = 0;
        }
        appendSupervisorLog(QStringLiteral("attached"),
                            QStringLiteral("popup=0x%1").arg(popupHwnd_, 0, 16));
        healthyResetTimer_.start(kHealthyResetWindowMs);
        emit workerAttached(popupHwnd_);
        return;
    }

    if (event == QLatin1String(kEvtDeviceRemoved)) {
        const QString reason = obj.value(QStringLiteral("reason")).toString();
        appendSupervisorLog(QStringLiteral("device_removed"),
                            QStringLiteral("reason=%1").arg(reason));
        emit workerDeviceRemoved(reason);
        // The worker exits after emitting device_removed; respawn is
        // handled by onProcessFinished below.
        return;
    }

    if (event == QLatin1String(kEvtFatal)) {
        const QString tag = obj.value(QStringLiteral("tag")).toString();
        const QString message = obj.value(QStringLiteral("message")).toString();
        appendSupervisorLog(QStringLiteral("worker_fatal"),
                            QStringLiteral("tag=%1 message=%2").arg(tag, message));
        return;
    }
}

void PreviewWorkerSupervisor::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    // Capture timestamp first thing — every later step (logging, signal
    // dispatch) adds noise to the respawn-to-attach measurement.
    lastWorkerExitMonotonicNs_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count();
    appendSupervisorLog(QStringLiteral("finished"),
                        QStringLiteral("exit_code=%1 status=%2 attempts_in_window=%3")
                            .arg(exitCode)
                            .arg(exitStatus == QProcess::CrashExit ? "crash" : "normal")
                            .arg(respawnAttemptsInWindow_));
    attached_ = false;
    popupHwnd_ = 0;
    emit workerExited(exitCode, exitStatus);

    if (process_ != nullptr) {
        process_->deleteLater();
        process_ = nullptr;
    }

    // Crash → respawn (rate-limited). Normal exit (graceful shutdown) is a
    // terminal state; we do not respawn.
    if (exitStatus != QProcess::CrashExit && exitCode == 0) {
        lastWorkerExitMonotonicNs_ = 0;  // graceful, no respawn measurement
        return;
    }

    scheduleRespawn();
}

void PreviewWorkerSupervisor::scheduleRespawn()
{
    ++respawnAttemptsInWindow_;
    // Phase 5 stress test override — production code respects the
    // crash-loop limit so a deterministic-fail bug doesn't infinite-loop
    // through respawns. The stress test deliberately injects N>5 crashes
    // in a row and needs the supervisor to keep restarting; gated env
    // var lets the test bypass without weakening the production guard.
    const bool bypassLimit =
        miacode::debug_options::envFlagEnabled("MIACODE_PREVIEW_WORKER_DISABLE_CRASH_LIMIT");
    if (!bypassLimit && respawnAttemptsInWindow_ > kMaxRespawnAttemptsInWindow) {
        appendSupervisorLog(QStringLiteral("respawn_giving_up"),
                            QStringLiteral("attempts=%1 > %2 within %3 ms")
                                .arg(respawnAttemptsInWindow_)
                                .arg(kMaxRespawnAttemptsInWindow)
                                .arg(kHealthyResetWindowMs));
        emit workerCrashLoopGivenUp();
        return;
    }

    appendSupervisorLog(QStringLiteral("respawn_scheduled"),
                        QStringLiteral("delay_ms=%1 attempt=%2")
                            .arg(respawnDelayMs_)
                            .arg(respawnAttemptsInWindow_));
    respawnTimer_.start(respawnDelayMs_);
    respawnDelayMs_ = qMin(respawnDelayMs_ * 2, kMaxRespawnDelayMs);
}

void PreviewWorkerSupervisor::onRespawnTimerFired()
{
    QString errorMessage;
    bool ok;
    if (lastStaticTestMode_) {
        ok = spawnStaticTest(lastEditorHwnd_, &errorMessage);
    } else {
        ok = spawn(lastEditorHwnd_, lastSnapshotShmKey_, lastSnapshotSlotByteSize_, lastSnapshotSlotCount_, &errorMessage);
    }
    if (!ok) {
        appendSupervisorLog(QStringLiteral("respawn_failed"),
                            QStringLiteral("error=%1").arg(errorMessage));
        scheduleRespawn();
    }
}

void PreviewWorkerSupervisor::resetCrashRateLimitIfHealthy()
{
    if (attached_) {
        appendSupervisorLog(QStringLiteral("respawn_window_reset"),
                            QStringLiteral("prev_attempts=%1")
                                .arg(respawnAttemptsInWindow_));
        respawnAttemptsInWindow_ = 0;
        respawnDelayMs_ = 100;
    }
}

}  // namespace miacode::preview::ipc
