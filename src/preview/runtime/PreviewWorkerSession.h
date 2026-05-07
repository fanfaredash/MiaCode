#pragma once

// Worker-side run loop for the out-of-process preview worker.
// Sibling of PreviewQuickExportSession but drives DComp instead of QQuickRenderControl.
//
// Lifecycle (Phase 0 of the validation plan):
//
//   1. main.cpp's runCliPreviewWorker() constructs a PreviewWorkerSession.
//   2. session.runStaticTest() blocks until the editor sends "shutdown" or
//      stdin closes (parent process exited).
//   3. Internally the session:
//        - reads JSON commands from stdin via a buffered reader
//        - on "attach": creates the popup HWND owned by editor_hwnd, attaches
//          PreviewDCompCore, starts a low-frequency render timer that calls
//          renderTestFrame() (red rectangle).
//        - on "set_visual_transform" / "resize": forwards to MoveWindow / Core.
//        - on "shutdown": stops the timer, tears down the Core, exits the loop.
//
// Phase 1+ adds a snapshot ring-buffer reader and replaces the static red
// rectangle with the real PreviewDCompSurface render path — at that point
// run() (non-static) becomes the production entry point. Phase 0 keeps both
// modes (`runStaticTest`, `run`) so we can validate the HWND topology without
// any IPC complexity yet.
//
// Single-threaded by design (all DComp ownership stays on the worker's main
// thread, which also runs the QCoreApplication event loop).

#include "preview/ipc/PreviewFrameStateSerial.h"
#include "core/scene/PreviewFrameState.h"

#include <QByteArray>
#include <QFile>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <cstdint>
#include <memory>

class QQuickWindow;
class QQuickItem;
class QMediaPlayer;
class QVideoSink;
class QVideoFrame;
class QAudioOutput;
class PreviewQuickSceneRoot;
class PreviewQuickHudLayer;

namespace miacode::preview::dcomp {
class PreviewDCompCore;
class PreviewDCompSpritePipeline;
class PreviewDCompTextureCache;
}

namespace miacode::preview::ipc {
class PreviewSnapshotRingBuffer;
}

namespace miacode::preview::runtime {
class PreviewSceneAssetRepository;
}

namespace miacode::preview::worker {

class OwnerHwndTracker;

// Worker-thread helper that does blocking stdin reads and dispatches each
// completed JSON command line via a queued signal. The session's main thread
// can then run a real QCoreApplication::exec() loop — timers (testFrameTimer_,
// snapshotPollTimer_) fire normally, instead of being starved by the worker
// blocking on fgetc().
class StdinCommandReader : public QObject
{
    Q_OBJECT
public:
    explicit StdinCommandReader(QObject* parent = nullptr);

public slots:
    void runReadLoop();   // invoked on the reader thread via QThread::started
    void requestStop();

signals:
    void commandLineReceived(const QByteArray& line);
    void stdinClosed();

private:
    std::atomic<bool> stopRequested_{false};
};

struct AttachCommand
{
    quint64 editorHwnd = 0;
    quint32 parentPid = 0;
    QString sessionId;
    QString logDirectory;
    QString snapshotShmKey;
    int snapshotSlotByteSize = 0;
    int snapshotSlotCount = 0;
    int protocolVersion = 0;
};

class PreviewWorkerSession : public QObject
{
    Q_OBJECT
public:
    explicit PreviewWorkerSession(QObject* parent = nullptr);
    ~PreviewWorkerSession() override;

    // Phase 0 entry — runs the static-popup proof-of-concept. Reads JSON
    // commands from stdin (blocking), creates the popup, renders red,
    // returns when "shutdown" arrives or stdin closes. Returns process
    // exit code (0 on graceful shutdown, non-zero on protocol / setup
    // failure).
    int runStaticTest();

    // Phase 1+ entry — same control protocol but consumes the snapshot
    // ring buffer for real frame rendering. Stub for now; falls back to
    // runStaticTest() until PreviewWorkerSession is wired through to
    // the full PreviewDCompSurface pipeline.
    int run();

private slots:
    // Reader-thread → main-thread slot. Parses JSON, dispatches to the
    // appropriate handler. Posts QCoreApplication::quit() on shutdown.
    void onStdinCommandLine(const QByteArray& line);
    void onStdinClosed();

private:
    bool handleAttach(const AttachCommand& attach);
    void handleSetVisualTransform(int xPx, int yPx, int displayWPx, int displayHPx);
    void handleResize(int wPx, int hPx);
    void teardown();

    bool createPopupHwnd(quint64 editorHwnd, QString* errorOut);
    void destroyPopupHwnd();

    // Render-thread-equivalent: the static-popup mode just calls
    // renderTestFrame() at ~30 Hz from a QTimer on the main thread.
    // Phase 1+ wires a real render thread similar to PreviewDCompRenderer.
    void onTestFrameTimerFired();

    // QSG mode per-snapshot pump: copies the latest serial snapshot into
    // frameState_, recomputes the cache-invalidation fingerprint, and
    // schedules a QSG repaint. Called from onSnapshotPollTick whenever a
    // fresh sequence arrives so the visible refresh rate matches the
    // editor's publish rate (vsync-aligned by QSG's render loop) instead
    // of being throttled to testFrameTimer_'s 30 Hz cadence.
    void pumpQsgFrame();

    // Wire up render-thread hooks on the QSG QQuickWindow: MMCSS
    // registration, real present-counter, optional render-phase profiler.
    // Called from handleAttach (QSG branch) right after qsgQuickWindow_
    // is created and shown.
    void installQsgRenderHooks();
    // Tear down render-thread hooks. Called from teardown(). Safe to
    // call multiple times; each disconnect on a default-constructed
    // QMetaObject::Connection is a no-op.
    void uninstallQsgRenderHooks();
    // Render-thread render-phase logger. Reads the per-phase atomic
    // timestamps and emits one `render_frame_profile` line. Rate-limited
    // and only fires when frame-pacing diagnostics are enabled.
    void recordWorkerRenderPhaseProfile();

    // Phase 1 — snapshot polling tick. Called from a separate higher-rate
    // QTimer when the ring buffer is attached. Reads the latest snapshot,
    // computes IPC latency (now_ns - publishMonotonicNs), accumulates
    // distribution stats, and periodically flushes a CSV row + a summary
    // log line.
    void onSnapshotPollTick();

    // Helpers for the latency CSV — opens lazily on first sample, written
    // line-by-line. Lives in the per-session log directory.
    void ensureLatencyCsvOpen();
    void writeLatencySample(qint64 latencyNs, quint64 sequence, double playheadSeconds);
    void flushLatencySummaryToLog();

    void onOwnerLocationChanged(int left, int top, int right, int bottom);

    // Asset-loader integration. The worker maintains its own
    // PreviewSceneAssetRepository populated from disk via
    // PreviewSceneAssetLoader. The skin directory comes from each
    // snapshot's `skinDirectory` field; when it changes we kick off an
    // async load. Once the repository emits `assetsChanged` we have
    // skin / judge / firework images available locally and can render
    // textured sprites — the actual render integration is a follow-up
    // chunk (this file just sets up the loader plumbing).
    void onSnapshotSkinDirectoryChanged(const QString& newDirectory);
    void onAssetRepositoryAssetsChanged();

    // Image bg path-change handler. Kicks off an async QImageReader
    // load on QThreadPool. When the load completes the result is
    // stashed in `workerStageImage_` and `workerStageImageDirty_` is
    // flipped on the main thread; the next pumpQsgFrame copies it
    // into `frameState_.media`.
    void onSnapshotMediaImagePathChanged(const QString& path, quint64 serial);

    // Video bg path-change handler. Sets the source on the already-
    // constructed player, calls play(). Empty path stops it.
    void onSnapshotMediaVideoPathChanged(const QString& path);
    // Constructs QMediaPlayer + QAudioOutput + QVideoSink eagerly. The
    // FIRST construction of these in a process triggers ~1-2 seconds
    // of audio-device enumeration on Windows; doing it at attach time
    // (when the worker is still warming up and not rendering chart
    // sprites yet) keeps that stall from registering as a 3 s+ frame
    // hitch mid-playback. Idempotent — second call is a no-op.
    void ensureVideoBackend();
    void teardownVideoBackend();
    // Slot for QVideoSink::videoFrameChanged. Throttled toImage().
    void onVideoFrameArrived(const QVideoFrame& frame);
    // Per-pump playhead sync for the video player. >= 40 ms drift seek.
    void syncVideoPlayhead(double playheadSeconds);


    // Phase 5 stress test — crash injection. Reads the env var
    // `MIACODE_PREVIEW_WORKER_INJECT_CRASH=N` once at attach time;
    // when frames presented reaches N the worker calls `__fastfail` to
    // simulate a render-thread crash. Used by
    // `--preview-worker-crash-recovery-test` to measure supervisor
    // respawn-and-attach time. Default 0 = disabled.
    void maybeInjectCrash();

    void* popupHwnd_ = nullptr;        // HWND when running on Windows.
    quint64 editorOwnerHwnd_ = 0;
    QString sessionId_;
    quint64 framesPresented_ = 0;
    int injectCrashAtFrame_ = 0;       // 0 = disabled

    // Last popup geometry pushed via "set_visual_transform" — used by the
    // OwnerHwndTracker callback to compute the popup's screen position
    // from the editor's window rect (the tracker only knows about the
    // editor's window, not the preview region inside it; the editor's
    // set_visual_transform supplies the offset within the client area).
    int lastPopupOffsetXPx_ = 0;
    int lastPopupOffsetYPx_ = 0;
    int lastPopupDisplayWPx_ = 0;
    int lastPopupDisplayHPx_ = 0;
    int lastEditorOriginXPx_ = 0;
    int lastEditorOriginYPx_ = 0;
    bool hasLastPopupGeometry_ = false;

    std::unique_ptr<miacode::preview::dcomp::PreviewDCompCore> core_;
    std::unique_ptr<miacode::preview::dcomp::PreviewDCompSpritePipeline> spritePipeline_;
    std::unique_ptr<miacode::preview::dcomp::PreviewDCompTextureCache> textureCache_;
    std::unique_ptr<OwnerHwndTracker> ownerTracker_;
    std::unique_ptr<miacode::preview::ipc::PreviewSnapshotRingBuffer> ringBuffer_;
    QTimer testFrameTimer_;
    QTimer snapshotPollTimer_;

    // Phase 4 demo render state. When a snapshot has been read at least
    // once, the test-frame timer renders a color cycle driven by the
    // snapshot's playhead + sprite count instead of a static red. This
    // gives operators a visible end-to-end signal that the snapshot
    // stream actually reaches the worker — the popup pulses through hue
    // in sync with the editor's playhead, and brightness modulates with
    // active sprite count.
    miacode::preview::ipc::PreviewFrameStateSerial lastSnapshot_{};
    bool lastSnapshotValid_ = false;

    // Worker-side asset infrastructure. Created lazily on first snapshot
    // that includes a non-empty skin directory. Lives on the worker's
    // main thread; the actual disk load runs on QThreadPool. The
    // repository emits assetsChanged when the load completes.
    std::unique_ptr<miacode::preview::runtime::PreviewSceneAssetRepository> assetRepository_;
    QString lastAppliedSkinDirectory_;
    int assetLoadAttempts_ = 0;
    // Set true on first attach + every onAssetRepositoryAssetsChanged
    // (so the next pumpQsgFrame refreshes frameState_.skin/judge/etc.).
    // Cleared at the end of that pump. Avoids paying ~60 atomic
    // ref-bumps per frame to copy unchanged QImage assets.
    bool assetsDirty_ = true;

    // Set true when assets first finish loading. Triggers a one-shot
    // synthetic firework injection in the next pumpQsgFrame so the
    // PreviewQuickJudgeFireworkMaterial PSO compiles + firework
    // textures upload at a benign moment, before the user's first
    // real firework would otherwise stall the render thread for
    // ~50-300 ms (Qt RHI compiles custom-material PSOs lazily on
    // first draw — same cold-cache pattern game engines warm up
    // explicitly at level load).
    bool warmupFireworkPending_ = false;

    // Image-bg state. Populated by an async load when the snapshot's
    // mediaImagePath / mediaSerial change. The next pumpQsgFrame copies
    // this into `frameState_.media.resolvedStageImage` so
    // PreviewQuickStageBackgroundLayer can render the bg natively in
    // the worker popup. Video bg (Phase 2) will go through a separate
    // QMediaPlayer-backed path; for now `mediaKind == Video` snapshots
    // leave the worker's media slot empty and the editor's QML
    // PreviewStageMediaItem keeps rendering through the worker's
    // transparent regions.
    QString lastAppliedMediaImagePath_;
    quint64 lastAppliedMediaSerial_ = 0;
    QImage workerStageImage_;
    quint64 workerStageImageSerial_ = 0;
    bool workerStageImageDirty_ = false;
    int mediaImageLoadGeneration_ = 0;

    // Video bg pipeline — pure C++. Worker hosts a muted QMediaPlayer
    // that decodes the chart's pv.mp4 / bg.mp4; a QVideoSink connected
    // to the player receives QVideoFrame events on the worker's main
    // thread. Each frame is converted to QImage with a 33 ms throttle
    // (matches PreviewStageMediaHost::noteVideoFrameArrived's pattern
    // — line ~1558 — capping at ~30 toImage()/s for a 30 fps source).
    // The next pumpQsgFrame copies the image into
    // frameState_.media.mediaFrame so PreviewQuickStageBackgroundLayer
    // (already in the QSG scene root) renders it via its existing
    // QImage path. No QML, no worker-side QQmlEngine, no per-pixel-
    // alpha popup tricks.
    //
    // The user explicitly flagged toImage() CPU cost. The throttle is
    // the same mitigation the editor uses — there is no GPU-side
    // bypass in the codebase outside QML VideoOutput, which we can't
    // reliably wire programmatically inside the worker.
    QMediaPlayer* videoPlayer_ = nullptr;
    QVideoSink* videoSink_ = nullptr;
    QAudioOutput* videoAudioOutput_ = nullptr;
    QString lastAppliedMediaVideoPath_;
    qint64 lastVideoSeekMs_ = -1;
    QImage workerVideoFrame_;
    quint64 workerVideoFrameSerial_ = 0;
    bool workerVideoFrameDirty_ = false;
    QElapsedTimer videoFrameToImageThrottle_;

    // Tracks the editor-side `setMediaVisible(bool)` decision (driven by
    // the user's "Pause and do not display PV/BG" Video Settings option:
    // applyPreviewStageMediaRouteVisualSettings sets it to
    // `!previewForceLabeledJudgeLineWhenPaused_ || playing`). Projected
    // as `latest.mediaVisible`. When false the worker hides image + video
    // bg AND pauses the QMediaPlayer so the decoder isn't burning CPU on
    // frames the user has chosen not to see. Starts true so first-frame
    // before any snapshot lands defaults to "show".
    bool lastAppliedMediaVisible_ = true;

    // QSG render mode (opt-in via MIACODE_PREVIEW_WORKER_QSG_RENDER).
    // When active, qsgQuickWindow_ replaces the popupHwnd_ + Core +
    // sprite pipeline as the popup's render surface; PreviewQuickSceneRoot
    // hosts the chart visuals via the editor's existing QSG layer
    // implementations. frameState_ is the worker-side scratch
    // PreviewFrameState that the scene root reads from — populated each
    // tick from the snapshot's serial fields plus the loaded asset
    // repository.
    bool qsgMode_ = false;
    QQuickWindow* qsgQuickWindow_ = nullptr;
    PreviewQuickSceneRoot* qsgSceneRoot_ = nullptr;
    // PreviewQuickHudLayer paints FPS / timestamp / debug-info text. It
    // is a sibling of the scene root in the editor's QML preview surface
    // — without it the worker popup has chart sprites but no HUD.
    PreviewQuickHudLayer* qsgHudLayer_ = nullptr;
    miacode::preview::scene::PreviewFrameState frameState_;

    // Cheap fingerprint of the visible-window sprite list — used to bump
    // `frameState_.sceneContentRevision` only when the window contents
    // actually changed, instead of every tick. The editor's revision
    // isn't projected through IPC, so without this gate the prepared
    // cache rebuilds 60×/s and stutters the preview. Hash combines
    // spriteCount + first/last sprite's startSeconds + lanes — enough
    // to catch window slides during playback and chart edits, while
    // staying constant for a paused / static frame stream.
    quint64 lastWindowFingerprint_ = 0;
    quint64 lastWindowRevision_ = 0;

    // Cached per-type marker counts — recomputed only when the visible
    // window fingerprint changes. Feeds the periodic qsg_tick log
    // without re-tallying ~30 QString comparisons per frame.
    int cachedMarkerTaps_ = 0;
    int cachedMarkerHolds_ = 0;
    int cachedMarkerSlides_ = 0;
    int cachedMarkerWifis_ = 0;
    int cachedMarkerTouches_ = 0;
    int cachedMarkerTouchHolds_ = 0;
    int cachedMarkerUnknowns_ = 0;

    // Render-thread hooks for the QSG QQuickWindow. Mirror the in-process
    // PreviewQuickSceneRoot hooks (which gate on `runtime_ != nullptr`,
    // a condition the worker never satisfies because it doesn't own a
    // PreviewRuntime instance). These connections are direct-connected
    // and fire on the QSG render thread.
    QMetaObject::Connection renderMmcssBootstrapConnection_;
    QMetaObject::Connection renderSceneGraphInvalidatedConnection_;
    QMetaObject::Connection renderFrameSwappedConnection_;
    QMetaObject::Connection renderBeforeSyncConnection_;
    QMetaObject::Connection renderAfterSyncConnection_;
    QMetaObject::Connection renderBeforeRenderConnection_;
    QMetaObject::Connection renderAfterRenderConnection_;
    QMetaObject::Connection renderProfileSwapConnection_;

    // One-shot guard so the MMCSS register lambda only runs once per
    // scene-graph lifetime even though `beforeSynchronizing` fires
    // every frame.
    std::atomic<bool> renderMmcssRegistrationAttempted_{false};

    // Render-thread present counter + interval stats. Updated under
    // DirectConnection from `frameSwapped`. Read by the main thread
    // periodically via the qsg_tick log; atomics avoid the need for a
    // mutex for these few scalars.
    std::atomic<quint64> renderFramesPresented_{0};
    std::atomic<qint64> renderLastPresentNs_{-1};
    std::atomic<qint64> renderPresentIntervalSumNs_{0};
    std::atomic<qint64> renderPresentIntervalMaxNs_{0};
    std::atomic<quint64> renderPresentIntervalSampleCount_{0};
    // 60-bin rolling counter for stutter detection (interval > 2× target).
    // Reset by the main thread when it logs the periodic summary.
    std::atomic<quint64> renderPresentStutterCount_{0};

    // Render-phase profiler timestamps (per-frame, written on render
    // thread). Mirror PreviewQuickSceneRoot's renderPhase*Ns_ fields,
    // but live on the worker so we don't depend on `runtime_`. All in
    // ns from `renderPhaseTimer_` which starts on first registration.
    QElapsedTimer renderPhaseTimer_;
    std::atomic<qint64> renderPhaseSyncStartNs_{-1};
    std::atomic<qint64> renderPhaseSyncEndNs_{-1};
    std::atomic<qint64> renderPhaseRenderStartNs_{-1};
    std::atomic<qint64> renderPhaseRenderEndNs_{-1};
    // Last-log-time for rate-limited render-phase profile output.
    qint64 renderPhaseLastLogMs_ = -1;

    // Stdin reader thread — fires `commandLineReceived` queued back to the
    // main thread. The session retains ownership; teardown() joins it.
    QThread* stdinReaderThread_ = nullptr;
    StdinCommandReader* stdinReader_ = nullptr;
    bool attached_ = false;

    // Latency stats for the current 1-second logging window. Reset every
    // flushLatencySummaryToLog().
    quint64 latencySampleCount_ = 0;
    qint64 latencySumNs_ = 0;
    qint64 latencyMaxNs_ = 0;
    qint64 latencyMinNs_ = INT64_MAX;
    quint64 lastObservedSequence_ = 0;
    quint64 missedSequenceJumps_ = 0;  // seq diff > 1 between successive reads
    quint64 staleReadCount_ = 0;       // seq unchanged between successive reads
    qint64 lastLatencyFlushNs_ = 0;

    QFile latencyCsvFile_;
    bool latencyCsvHeaderWritten_ = false;
};

}  // namespace miacode::preview::worker
