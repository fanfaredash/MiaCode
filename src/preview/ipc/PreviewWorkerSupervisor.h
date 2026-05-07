#pragma once

// Editor-side lifecycle supervisor for the out-of-process preview worker.
// Sibling pattern to MainWindow.ExportWorker.cpp — the export worker uses
// QProcess + line-buffered JSON protocol on stdout, and the preview worker
// reuses that pattern. The differences:
//
//   - The preview worker is long-running (lives for the editor session,
//     not per-export). It is respawned automatically on crash with
//     exponential backoff so that DXGI_ERROR_DEVICE_REMOVED becomes a
//     transparent ~500 ms popup blank instead of a permanent freeze.
//
//   - The preview worker's output is consumed asynchronously via Qt
//     signals (`workerAttached`, `workerDeviceRemoved`, `workerCrashed`).
//     The editor never blocks on the worker.
//
// Phase 0 of the plan validates the cross-process HWND topology with a
// static red-rectangle worker. Phase 1+ adds the snapshot ring buffer.
// This class supports both modes via spawnStaticTest() and spawn() — the
// difference is the CLI flag passed to the worker process and whether
// the supervisor publishes per-frame snapshots.
//
// See docs/PREVIEW_DEVICE_LOSS_MITIGATION_AND_PROCESS_ISOLATION_PLAN.md.

#include "preview/ipc/PreviewFrameStateSerial.h"

#include <QByteArray>
#include <QObject>
#include <QPointer>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <memory>

namespace miacode::preview::ipc {

class PreviewSnapshotRingBuffer;

class PreviewWorkerSupervisor : public QObject
{
    Q_OBJECT
public:
    explicit PreviewWorkerSupervisor(QObject* parent = nullptr);
    ~PreviewWorkerSupervisor() override;

    // Spawn the worker in Phase 0 static-popup mode. The worker creates a
    // top-level transparent owned popup HWND against `editorHwnd` and renders
    // a static red rectangle. Returns false if QProcess::start failed or the
    // executable cannot be located. Errors are reported via errorMessage and
    // also logged through the runtime debug channel.
    bool spawnStaticTest(quint64 editorHwnd, QString* errorMessage = nullptr);

    // Phase 1+ — production mode. Spawns the worker with the snapshot ring
    // buffer key already published, and on attach the worker drives the
    // PreviewDCompSurface render path. snapshotShmKey, slot byte size and
    // slot count come from the editor's PreviewSnapshotRingBuffer publisher.
    bool spawn(quint64 editorHwnd,
               const QString& snapshotShmKey,
               int snapshotSlotByteSize,
               int snapshotSlotCount,
               QString* errorMessage = nullptr);

    // Phase 1 convenience entry — owns the ring buffer + synthetic publisher
    // internally. Call this when MIACODE_PREVIEW_OUT_OF_PROCESS=1 to get
    // Phase 0 popup verification AND Phase 1 IPC-latency CSV in one shot.
    // The synthetic publisher writes a `PreviewFrameStateSerial` every
    // ~16.67 ms with a synthetic monotonically-increasing playhead and the
    // current monotonic timestamp; the worker's ring-buffer reader logs
    // `now_ns - publishMonotonicNs` as the IPC latency sample.
    //
    // Returns false on ring-buffer creation or worker-spawn failure.
    bool spawnWithSyntheticPublisher(quint64 editorHwnd, QString* errorMessage = nullptr);

    // Phase 4 entry — owns the ring buffer but does NOT start a synthetic
    // publisher. The caller drives `publishSnapshot()` from its own
    // `PreviewRuntime::frameStateChanged` path. Used when the editor wants
    // the worker to render real chart content rather than just be a
    // latency harness. Mutually exclusive with `spawnWithSyntheticPublisher`.
    bool spawnWithExternalPublisher(quint64 editorHwnd, QString* errorMessage = nullptr);

    // Push one `PreviewFrameStateSerial` to the ring buffer. Must be called
    // only after `spawnWithExternalPublisher`. Cheap (memcpy + 8-byte
    // release store). Caller fills the projection; this method handles
    // sequence + publish-monotonic timestamp internally.
    bool publishSnapshot(PreviewFrameStateSerial& snapshotInOut);

    // Stop the worker gracefully — sends "shutdown" via stdin, then waits up
    // to ~500 ms for QProcess::finished. Force-terminates if the worker
    // doesn't exit. Idempotent.
    void shutdown();

    // Forward popup geometry to the worker (screen-pixel origin + display
    // size). Cheap; cached when the worker is mid-respawn.
    void setVisualTransform(int xPx, int yPx, int displayWPx, int displayHPx);

    // True iff the worker process is running and has emitted "attached".
    bool isRunning() const;
    bool isAttached() const;

    // Most recent popup HWND value (0 when not attached). The editor uses
    // this for diagnostics; the worker is the canonical owner of the HWND.
    quint64 popupHwnd() const;

signals:
    // Worker emitted "attached" — the popup HWND is now visible and owned by
    // the editor HWND. Editor side may now publish geometry / snapshots.
    void workerAttached(quint64 popupHwnd);

    // Worker emitted "device_removed" — DXGI_ERROR_DEVICE_REMOVED was
    // detected by the worker's render thread. The supervisor will respawn
    // the worker after a short backoff.
    void workerDeviceRemoved(const QString& reason);

    // QProcess exited (crash or graceful). Emitted before any respawn
    // attempt; respawn is logged separately.
    void workerExited(int exitCode, QProcess::ExitStatus exitStatus);

    // The respawn supervisor tripped its rate-limit (5 retries in 60 s).
    // Editor should fall back to in-process mode and surface a UI message.
    void workerCrashLoopGivenUp();

    // Phase 5 stress harness — emitted on each successful respawn-and-attach
    // cycle. Two timings:
    //   * `totalRespawnMs`     — wall clock from prev worker's QProcess::finished
    //                            to new worker's "attached" event (includes backoff)
    //   * `spawnToAttachMs`    — wall clock from new QProcess::start() to "attached"
    //                            (excludes the scheduled backoff delay)
    // The first measures end-user impact; the second measures the actual
    // cost under test. Both are reported so the operator can decompose
    // backoff time vs. pure spawn cost.
    // Not emitted on the first attach (no prior exit).
    void workerRespawnTimeRecorded(qint64 totalRespawnMs, qint64 spawnToAttachMs);

private slots:
    void onProcessReadyReadStandardOutput();
    void onProcessReadyReadStandardError();
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onRespawnTimerFired();
    void onSyntheticPublisherTick();

private:
    bool startProcess(bool staticTestMode, QString* errorMessage);
    QString resolveWorkerExecutablePath() const;
    void parseStdoutLine(const QByteArray& line);
    void sendCommand(const QByteArray& payload);
    void scheduleRespawn();
    void resetCrashRateLimitIfHealthy();

    QPointer<QProcess> process_;
    QByteArray stdoutBuffer_;
    QByteArray stderrBuffer_;

    // Last successful "attach" payload — replayed on respawn so the new
    // worker re-creates the same popup against the same editor HWND.
    quint64 lastEditorHwnd_ = 0;
    QString lastSessionId_;
    QString lastSnapshotShmKey_;
    int lastSnapshotSlotByteSize_ = 0;
    int lastSnapshotSlotCount_ = 0;
    bool lastStaticTestMode_ = false;

    // Last published geometry — replayed on respawn so the new popup snaps
    // back to the correct position before its first paint.
    int lastVtX_ = 0;
    int lastVtY_ = 0;
    int lastVtDisplayW_ = 0;
    int lastVtDisplayH_ = 0;
    bool lastVtValid_ = false;

    quint64 popupHwnd_ = 0;
    bool attached_ = false;

    // Owned ring buffer — used by both the synthetic harness
    // (`spawnWithSyntheticPublisher`) and the external-publisher mode
    // (`spawnWithExternalPublisher`). Only one of those modes can be active
    // per supervisor lifetime; the synthetic publisher timer is started
    // only in the former.
    std::unique_ptr<PreviewSnapshotRingBuffer> syntheticRingBuffer_;
    QTimer syntheticPublisherTimer_;
    qint64 syntheticPublishStartMs_ = 0;
    quint64 syntheticPublishCount_ = 0;

    // Respawn-on-crash state. Backoff resets after the worker has been
    // healthy (attached) for kHealthyResetWindowMs.
    int respawnAttemptsInWindow_ = 0;
    int respawnDelayMs_ = 100;  // doubles per retry, cap kMaxRespawnDelayMs
    QTimer respawnTimer_;
    QTimer healthyResetTimer_;

    // Phase 5 stress harness — wall-clock ns when QProcess::finished last
    // fired. 0 == never (first spawn). When the next worker emits
    // "attached", we compute respawn-to-attach delta and emit
    // workerRespawnTimeRecorded(ms).
    qint64 lastWorkerExitMonotonicNs_ = 0;
    // Set by startProcess() right before QProcess::start() returns; used
    // by the attached event to compute spawn_to_attach_ms (excluding
    // the scheduled backoff delay).
    qint64 lastSpawnStartedMonotonicNs_ = 0;

    static constexpr int kMaxRespawnDelayMs = 5000;
    static constexpr int kMaxRespawnAttemptsInWindow = 5;
    static constexpr int kHealthyResetWindowMs = 30000;
    static constexpr int kGracefulShutdownTimeoutMs = 500;
};

}  // namespace miacode::preview::ipc
