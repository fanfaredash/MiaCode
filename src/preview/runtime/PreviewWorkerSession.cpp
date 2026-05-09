#include "preview/runtime/PreviewWorkerSession.h"

#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/DebugOptions.h"
#include "common/Mmcss.h"

#include <QPointer>
#include <QThreadPool>
#include <QUrl>

#ifdef HAVE_QT_MULTIMEDIA
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QVideoFrame>
#include <QVideoSink>
#endif

#include "preview/ipc/PreviewFrameStateProjector.h"
#include "preview/ipc/PreviewSnapshotRingBuffer.h"
#include "preview/ipc/PreviewWorkerProtocol.h"
#include "preview/runtime/PreviewSceneAssetRepository.h"
#include "core/scene/PreviewSceneConstants.h"
#include "core/scene/PreviewSceneMath.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "render/backend_d3d11/OwnerHwndTracker.h"
#include "render/backend_d3d11/PreviewDCompCore.h"
#include "render/backend_d3d11/PreviewDCompFrameStateSnapshot.h"
#include "render/backend_d3d11/PreviewDCompSpritePipeline.h"
#include "render/backend_d3d11/PreviewDCompTextureCache.h"

#include <QQuickWindow>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSize>

#include <chrono>
#include <cmath>
#include <cstdio>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <intrin.h>  // __fastfail
#endif

#include <cstdlib>

namespace {

void appendWorkerLog(const QString& tag, const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/worker"),
        QStringLiteral("tag=%1 %2").arg(tag, payload)
    );
    // Diagnostic mirror: bypass ALL log infrastructure and write directly
    // to a hand-opened file. The async-Runtime path has been silently
    // dropping worker writes across recent runs; this trace path proves
    // whether appendWorkerLog calls are even happening.
    static QFile traceFile;
    static bool traceFileTried = false;
    if (!traceFileTried) {
        traceFileTried = true;
        const QString runtimeOverride =
            qEnvironmentVariable("MIACODE_RUNTIME_LOG_PATH");
        QString tracePath;
        if (!runtimeOverride.isEmpty()) {
            const QFileInfo overrideInfo(runtimeOverride);
            tracePath = overrideInfo.absolutePath()
                        + QStringLiteral("/miacode_worker_trace.log");
        } else {
            tracePath = QDir(QDir::tempPath()).filePath(
                QStringLiteral("miacode_worker_trace.log"));
        }
        QDir().mkpath(QFileInfo(tracePath).absolutePath());
        traceFile.setFileName(tracePath);
        traceFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    }
    if (traceFile.isOpen()) {
        const QString line = QStringLiteral("%1 [preview/worker] tag=%2 %3\n")
                                 .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs))
                                 .arg(tag)
                                 .arg(payload);
        traceFile.write(line.toUtf8());
        traceFile.flush();
    }
}

}  // namespace

namespace miacode::preview::worker {

namespace ipc = miacode::preview::ipc;

PreviewWorkerSession::PreviewWorkerSession(QObject* parent)
    : QObject(parent)
{
    testFrameTimer_.setInterval(33);  // ~30 Hz; static rectangle only.
    testFrameTimer_.setSingleShot(false);
    QObject::connect(&testFrameTimer_, &QTimer::timeout, this, &PreviewWorkerSession::onTestFrameTimerFired);

    // Snapshot ring buffer poll cadence — kept short enough that p99
    // jitter from the worker side stays below half a vsync (8 ms). 5 ms
    // is the cadence the plan section 6.1 recommends. The publish side
    // produces at ~16.67 ms, so the worker reads ≈3 polls per published
    // snapshot — most are stale-read, only the first after a publish
    // counts toward the latency CSV.
    snapshotPollTimer_.setInterval(5);
    snapshotPollTimer_.setSingleShot(false);
    QObject::connect(&snapshotPollTimer_, &QTimer::timeout, this, &PreviewWorkerSession::onSnapshotPollTick);
}

PreviewWorkerSession::~PreviewWorkerSession()
{
    teardown();
}

// StdinCommandReader runs on its own QThread. It does blocking fgetc reads
// and emits one queued signal per complete line. Lets the main thread keep
// the QCoreApplication event loop running so timers fire on schedule.
StdinCommandReader::StdinCommandReader(QObject* parent)
    : QObject(parent)
{
}

void StdinCommandReader::requestStop()
{
    stopRequested_.store(true, std::memory_order_relaxed);
}

void StdinCommandReader::runReadLoop()
{
    QByteArray line;
    while (!stopRequested_.load(std::memory_order_relaxed)) {
        const int ch = std::fgetc(stdin);
        if (ch == EOF) {
            emit stdinClosed();
            return;
        }
        if (ch == '\n') {
            emit commandLineReceived(line);
            line.clear();
            continue;
        }
        if (ch != '\r') {
            line.append(static_cast<char>(ch));
        }
    }
}

bool PreviewWorkerSession::createPopupHwnd(quint64 editorHwnd, QString* errorOut)
{
    editorOwnerHwnd_ = editorHwnd;
#ifdef Q_OS_WIN
    const HWND owner = reinterpret_cast<HWND>(editorHwnd);
    if (owner == nullptr || !::IsWindow(owner)) {
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("editor_hwnd is invalid");
        }
        return false;
    }

    // Phase 0 popup style — mirrors the in-process PreviewDCompSurface
    // top-level popup logic. WS_POPUP for borderless transparent;
    // WS_EX_NOREDIRECTIONBITMAP so DComp's per-pixel alpha shows the
    // editor scene through transparent regions; WS_EX_TOOLWINDOW so it
    // stays out of taskbar / Alt-Tab; WS_EX_NOACTIVATE so clicks don't
    // steal focus from the editor; WS_EX_TRANSPARENT so input passes
    // through to whatever's beneath.
    const DWORD exStyle = WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW
                          | WS_EX_NOREDIRECTIONBITMAP;
    const HWND popup = ::CreateWindowExW(
        exStyle,
        L"STATIC",
        L"MiaCode Preview Worker",
        WS_POPUP,
        0, 0, 1, 1,
        owner, nullptr,
        ::GetModuleHandleW(nullptr), nullptr);
    if (popup == nullptr) {
        const DWORD err = ::GetLastError();
        if (errorOut != nullptr) {
            *errorOut = QStringLiteral("CreateWindowExW failed err=%1").arg(err);
        }
        return false;
    }

    // Set the editor as the OWNER (not parent) so the popup follows the
    // editor's z-order / minimize lifecycle without being clipped to the
    // editor's client area. CreateWindowExW already sets the owner via
    // the hWndParent arg when WS_POPUP is used, but we also call
    // SetWindowLongPtr explicitly because some Win10 1809-and-earlier
    // configurations have been observed to ignore the constructor-time
    // owner relationship for cross-process windows.
    ::SetWindowLongPtrW(popup, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));

    popupHwnd_ = popup;
    appendWorkerLog(QStringLiteral("popup_created"),
                    QStringLiteral("owner=0x%1 popup=0x%2")
                        .arg(reinterpret_cast<quintptr>(owner), 0, 16)
                        .arg(reinterpret_cast<quintptr>(popup), 0, 16));
    return true;
#else
    Q_UNUSED(editorHwnd);
    if (errorOut != nullptr) {
        *errorOut = QStringLiteral("preview worker is Windows-only");
    }
    return false;
#endif
}

void PreviewWorkerSession::destroyPopupHwnd()
{
#ifdef Q_OS_WIN
    if (popupHwnd_ != nullptr) {
        ::DestroyWindow(reinterpret_cast<HWND>(popupHwnd_));
        popupHwnd_ = nullptr;
    }
#endif
}

bool PreviewWorkerSession::handleAttach(const AttachCommand& attach)
{
    MC_OP("PreviewWorkerSession::handleAttach");
    _mc_op_.note(QStringLiteral("editor_hwnd=0x%1 session=%2 proto=%3")
                     .arg(attach.editorHwnd, 0, 16)
                     .arg(attach.sessionId)
                     .arg(attach.protocolVersion));
    if (attach.protocolVersion != ipc::kPreviewWorkerProtocolVersion) {
        _mc_op_.fail(QStringLiteral("protocol_mismatch editor_proto=%1 worker_proto=%2")
                         .arg(attach.protocolVersion)
                         .arg(ipc::kPreviewWorkerProtocolVersion));
        ipc::emitFatalEvent(
            QStringLiteral("protocol_mismatch"),
            QStringLiteral("editor_proto=%1 worker_proto=%2")
                .arg(attach.protocolVersion)
                .arg(ipc::kPreviewWorkerProtocolVersion));
        return false;
    }
    sessionId_ = attach.sessionId;
    if (!attach.logDirectory.isEmpty()) {
        miacode::debug_log::setSessionProjectLogDirectory(attach.logDirectory);
    }

    // Phase 5 stress harness — inherited from the parent process env.
    // Resolved here (not on every frame) so the value is fixed for the
    // lifetime of this worker process; respawned workers re-read it.
    bool ok = false;
    const int n = qEnvironmentVariable("MIACODE_PREVIEW_WORKER_INJECT_CRASH").toInt(&ok);
    injectCrashAtFrame_ = (ok && n > 0) ? n : 0;
    if (injectCrashAtFrame_ > 0) {
        appendWorkerLog(QStringLiteral("inject_crash_armed"),
                        QStringLiteral("crash_at_frame=%1").arg(injectCrashAtFrame_));
    }

    qsgMode_ = miacode::debug_options::previewWorkerQsgRenderEnabled();
    editorOwnerHwnd_ = attach.editorHwnd;

    if (qsgMode_) {
        // QSG render path — instantiate a QQuickWindow as our popup
        // surface and host PreviewQuickSceneRoot inside it. The window's
        // own HWND is the popup; we'll set GWLP_HWNDPARENT to the editor
        // HWND so DWM treats it as an owned popup. No DComp Core, no
        // sprite pipeline, no manual MoveWindow loop — Qt handles all
        // of that via the standard QQuickWindow infrastructure.
#ifdef Q_OS_WIN
        const HWND owner = reinterpret_cast<HWND>(attach.editorHwnd);
        if (owner == nullptr || !::IsWindow(owner)) {
            _mc_op_.fail(QStringLiteral("qsg_attach_invalid_editor_hwnd hwnd=0x%1")
                             .arg(attach.editorHwnd, 0, 16));
            ipc::emitFatalEvent(QStringLiteral("qsg_attach_invalid_editor_hwnd"),
                                QStringLiteral("hwnd=0x%1").arg(attach.editorHwnd, 0, 16));
            return false;
        }

        qsgQuickWindow_ = new QQuickWindow();
        // Worker draws its own bg (image via PreviewQuickStageBackgroundLayer
        // from frameState_.media.resolvedStageImage; video via QMediaPlayer
        // → QVideoSink → toImage() → frameState_.media.mediaFrame). Cleared
        // to opaque black; bg layers composite on top. We deliberately do
        // NOT chase popup transparency because Qt 6's Windows plugin
        // doesn't reliably apply WS_EX_NOREDIRECTIONBITMAP for
        // programmatically-created QQuickWindows.
        qsgQuickWindow_->setColor(Qt::black);
        qsgQuickWindow_->setFlag(Qt::FramelessWindowHint, true);
        qsgQuickWindow_->setFlag(Qt::Tool, true);  // out of taskbar / Alt-Tab
        qsgQuickWindow_->setFlag(Qt::WindowDoesNotAcceptFocus, true);
        qsgQuickWindow_->setFlag(Qt::WindowTransparentForInput, true);
        qsgQuickWindow_->resize(256, 256);
        qsgQuickWindow_->show();

        // Force HWND realisation, apply the parent + ex-styles.
        const HWND quickHwnd = reinterpret_cast<HWND>(qsgQuickWindow_->winId());
        if (quickHwnd != nullptr) {
            ::SetWindowLongPtrW(quickHwnd, GWLP_HWNDPARENT, reinterpret_cast<LONG_PTR>(owner));
            const LONG_PTR exStyle = ::GetWindowLongPtrW(quickHwnd, GWL_EXSTYLE);
            ::SetWindowLongPtrW(quickHwnd, GWL_EXSTYLE,
                                exStyle | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW);
            popupHwnd_ = quickHwnd;
        }

        // Host PreviewQuickSceneRoot inside the window. setDCompFallbackActive(true)
        // tells the scene root + its layers to render via the QSG path even
        // when the global previewDCompExclusiveEnabled() flag is set in
        // the worker's env (the in-process surface uses that flag to
        // short-circuit, but the worker has no DComp-exclusive popup
        // — its only renderer IS the QSG path).
        qsgSceneRoot_ = new PreviewQuickSceneRoot(qsgQuickWindow_->contentItem());
        qsgSceneRoot_->setSize(QSizeF(qsgQuickWindow_->size()));
        qsgSceneRoot_->setZ(1.0);
        qsgSceneRoot_->setDCompFallbackActive(true);
        qsgSceneRoot_->setFrameState(&frameState_);

        // Sibling HUD overlay — paints FPS / timestamp / debug-info on top
        // of chart sprites. The editor's QML PreviewQuickHudLayer
        // short-circuits when DComp-exclusive is on, leaving HUD rendering
        // to the in-process DComp surface. With the worker now owning chart
        // render, the worker must host the HUD layer too — otherwise the
        // user sees chart sprites but no FPS/timestamp text inside the
        // chart popup. dcompFallbackActive=true so the HUD's own DComp
        // short-circuit yields to QSG paint.
        qsgHudLayer_ = new PreviewQuickHudLayer(qsgQuickWindow_->contentItem());
        qsgHudLayer_->setSize(QSizeF(qsgQuickWindow_->size()));
        qsgHudLayer_->setZ(2.0);
        qsgHudLayer_->setDCompFallbackActive(true);
        qsgHudLayer_->setFrameState(&frameState_);

        appendWorkerLog(QStringLiteral("qsg_window_created"),
                        QStringLiteral("hwnd=0x%1 owner=0x%2")
                            .arg(reinterpret_cast<quintptr>(quickHwnd), 0, 16)
                            .arg(reinterpret_cast<quintptr>(owner), 0, 16));

        // Wire the render-thread hooks (MMCSS, present-counter, optional
        // render-phase profiler). Must happen AFTER show() so QSG has
        // started spinning up its render thread, but BEFORE the first
        // pump so we don't miss the first frame's beforeSynchronizing.
        installQsgRenderHooks();

        // Pre-construct QMediaPlayer + QAudioOutput. The first
        // construction in the worker process triggers WASAPI/WMF
        // device enumeration which can stall the main thread for
        // 1-3 seconds. Doing this at attach time keeps the stall
        // outside the chart-render path — the user's FIRST firework
        // (or any other content event) won't show up as a 3-second
        // freeze waiting on audio device probe.
        ensureVideoBackend();
#endif
        // Phase 2 — start tracking the editor HWND so the popup follows
        // it. Same as DComp path — the QSG popup HWND moves via the
        // editor's setVisualTransform stream too.
        ownerTracker_ = std::make_unique<OwnerHwndTracker>();
        ownerTracker_->registerOwner(
            static_cast<quintptr>(editorOwnerHwnd_),
            [this](int left, int top, int right, int bottom) {
                onOwnerLocationChanged(left, top, right, bottom);
            });

        // Asset repository + ring buffer — same setup as DComp path.
        if (!attach.snapshotShmKey.isEmpty()) {
            ringBuffer_ = std::make_unique<miacode::preview::ipc::PreviewSnapshotRingBuffer>();
            QString attachError;
            if (!ringBuffer_->attachAsConsumer(attach.snapshotShmKey, &attachError)) {
                appendWorkerLog(QStringLiteral("ring_buffer_attach_failed"),
                                QStringLiteral("key=%1 err=%2 popup_only_mode")
                                    .arg(attach.snapshotShmKey, attachError));
                ringBuffer_.reset();
            } else {
                snapshotPollTimer_.start();
                appendWorkerLog(QStringLiteral("ring_buffer_attached"),
                                QStringLiteral("key=%1 slot_bytes=%2 slot_count=%3")
                                    .arg(attach.snapshotShmKey)
                                    .arg(attach.snapshotSlotByteSize)
                                    .arg(attach.snapshotSlotCount));
            }
        }

        // QSG mode renders are driven directly from the snapshot poll
        // (pumpQsgFrame() at the end of onSnapshotPollTick). We do NOT
        // start testFrameTimer_ here — it would only throttle visible
        // updates to its 30 Hz cadence on top of what the editor is
        // already publishing at vsync.

        ipc::emitAttachedEvent(sessionId_, reinterpret_cast<quintptr>(popupHwnd_));
        appendWorkerLog(QStringLiteral("attached_qsg"),
                        QStringLiteral("session=%1 popup=0x%2")
                            .arg(sessionId_)
                            .arg(reinterpret_cast<quintptr>(popupHwnd_), 0, 16));
        return true;
    }

    // ---- DComp path (default) ----
    QString errorMessage;
    if (!createPopupHwnd(attach.editorHwnd, &errorMessage)) {
        ipc::emitFatalEvent(QStringLiteral("popup_create_failed"), errorMessage);
        return false;
    }

#ifdef Q_OS_WIN
    // Default 256x256 size centred via MoveWindow; the editor will issue
    // resize / set_visual_transform shortly after attach to point us at
    // the actual preview region.
    constexpr int kInitialEdge = 256;
    ::MoveWindow(reinterpret_cast<HWND>(popupHwnd_), 0, 0, kInitialEdge, kInitialEdge, FALSE);

    core_ = std::make_unique<miacode::preview::dcomp::PreviewDCompCore>();
    if (!core_->initialise(reinterpret_cast<HWND>(popupHwnd_), QSize(kInitialEdge, kInitialEdge))) {
        ipc::emitFatalEvent(QStringLiteral("core_initialise_failed"),
                            QStringLiteral("popup=0x%1")
                                .arg(reinterpret_cast<quintptr>(popupHwnd_), 0, 16));
        destroyPopupHwnd();
        core_.reset();
        return false;
    }

    // Phase 4 worker render path — initialise the sprite pipeline so the
    // render tick can draw textured quads driven by the snapshot's
    // playhead. Pipeline failure is non-fatal: the render tick falls
    // back to the snapshot-driven color cycle, so the worker still
    // produces visible output even on hardware where shader compile
    // fails (rare on Win10+).
    spritePipeline_ = std::make_unique<miacode::preview::dcomp::PreviewDCompSpritePipeline>();
    if (!spritePipeline_->initialise(core_->device())) {
        appendWorkerLog(QStringLiteral("sprite_pipeline_init_failed"),
                        QStringLiteral("falling back to color cycle"));
        spritePipeline_.reset();
    } else {
        appendWorkerLog(QStringLiteral("sprite_pipeline_initialised"), QString());
        // Texture cache is required by renderSnapshot's signature even
        // for circle-only batches (which don't sample any texture). Spin
        // it up alongside the pipeline so we have it ready.
        textureCache_ = std::make_unique<miacode::preview::dcomp::PreviewDCompTextureCache>();
    }

    ::ShowWindow(reinterpret_cast<HWND>(popupHwnd_), SW_SHOWNOACTIVATE);
    testFrameTimer_.start();

    // Phase 2 — start tracking the editor HWND so the popup follows it
    // across drag, resize, multi-monitor, RDP attach/detach.
    ownerTracker_ = std::make_unique<OwnerHwndTracker>();
    ownerTracker_->registerOwner(
        static_cast<quintptr>(editorOwnerHwnd_),
        [this](int left, int top, int right, int bottom) {
            onOwnerLocationChanged(left, top, right, bottom);
        });

    // Phase 1 — snapshot ring buffer attach + latency measurement. Optional;
    // the supervisor only includes a key when the editor publisher is
    // wired. Without a key the worker behaves exactly like Phase 0
    // (static rectangle, no ring read).
    if (!attach.snapshotShmKey.isEmpty()) {
        ringBuffer_ = std::make_unique<miacode::preview::ipc::PreviewSnapshotRingBuffer>();
        QString attachError;
        if (!ringBuffer_->attachAsConsumer(attach.snapshotShmKey, &attachError)) {
            // Ring buffer attach failure is non-fatal for Phase 0/1 use:
            // we still want the visible popup. Log + continue. We
            // deliberately do NOT emit a fatal event upstream — the
            // supervisor would log it as a worker error and the operator
            // would treat the popup-only mode as a bug. A plain runtime
            // log entry is enough for diagnostics.
            appendWorkerLog(QStringLiteral("ring_buffer_attach_failed"),
                            QStringLiteral("key=%1 err=%2 popup_only_mode")
                                .arg(attach.snapshotShmKey, attachError));
            ringBuffer_.reset();
        } else {
            snapshotPollTimer_.start();
            appendWorkerLog(QStringLiteral("ring_buffer_attached"),
                            QStringLiteral("key=%1 slot_bytes=%2 slot_count=%3")
                                .arg(attach.snapshotShmKey)
                                .arg(attach.snapshotSlotByteSize)
                                .arg(attach.snapshotSlotCount));
        }
    }

    ipc::emitAttachedEvent(sessionId_, reinterpret_cast<quintptr>(popupHwnd_));
    appendWorkerLog(QStringLiteral("attached"),
                    QStringLiteral("session=%1 popup=0x%2 size=%3x%3")
                        .arg(sessionId_)
                        .arg(reinterpret_cast<quintptr>(popupHwnd_), 0, 16)
                        .arg(kInitialEdge));
    return true;
#else
    return false;
#endif
}

void PreviewWorkerSession::handleSetVisualTransform(int xPx, int yPx, int displayWPx, int displayHPx)
{
    MC_OP("PreviewWorkerSession::handleSetVisualTransform");
    _mc_op_.note(QStringLiteral("x=%1 y=%2 w=%3 h=%4").arg(xPx).arg(yPx).arg(displayWPx).arg(displayHPx));
#ifdef Q_OS_WIN
    if (popupHwnd_ == nullptr || (!qsgMode_ && core_ == nullptr)) {
        appendWorkerLog(QStringLiteral("set_visual_transform_drop"),
                        QStringLiteral("popup_null=%1 core_null=%2 qsg=%3")
                            .arg(popupHwnd_ == nullptr ? 1 : 0)
                            .arg(core_ == nullptr ? 1 : 0)
                            .arg(qsgMode_ ? 1 : 0));
        return;
    }
    if (displayWPx <= 0 || displayHPx <= 0) {
        appendWorkerLog(QStringLiteral("set_visual_transform_invalid"),
                        QStringLiteral("xy=%1,%2 wh=%3x%4")
                            .arg(xPx).arg(yPx).arg(displayWPx).arg(displayHPx));
        return;
    }
    if (qsgMode_) {
        // For the QSG path, Qt expects logical (device-independent) pixels
        // in `setGeometry` / `resize` etc. The editor sent us PHYSICAL
        // pixels (via mapToGlobal × DPR), so divide by the worker's
        // effective DPR before handing values to Qt. Otherwise the
        // QQuickWindow internally believes its size is 2× / 1.5× / etc.
        // larger than the visible HWND, the QQuickItem layouts at
        // double scale, and the rendered scene appears stretched /
        // mispositioned.
        const qreal dpr = (qsgQuickWindow_ != nullptr
                            && qsgQuickWindow_->effectiveDevicePixelRatio() > 0.0)
                          ? qsgQuickWindow_->effectiveDevicePixelRatio()
                          : 1.0;
        const int xLogical = qRound(xPx / dpr);
        const int yLogical = qRound(yPx / dpr);
        const int wLogical = qRound(displayWPx / dpr);
        const int hLogical = qRound(displayHPx / dpr);
        if (qsgQuickWindow_ != nullptr) {
            qsgQuickWindow_->setGeometry(xLogical, yLogical, wLogical, hLogical);
        }
        if (qsgSceneRoot_ != nullptr) {
            qsgSceneRoot_->setSize(QSizeF(wLogical, hLogical));
        }
        if (qsgHudLayer_ != nullptr) {
            qsgHudLayer_->setSize(QSizeF(wLogical, hLogical));
        }
    } else {
        ::MoveWindow(reinterpret_cast<HWND>(popupHwnd_),
                     xPx, yPx, displayWPx, displayHPx, TRUE);
        if (core_ != nullptr) {
            core_->setVisualTransform(0, 0, QSize(displayWPx, displayHPx));
        }
    }

    // Cache so the OwnerHwndTracker callback can recompute the popup's
    // screen position from the editor's new window rect when the editor
    // moves without changing the relative preview offset.
    if (ownerTracker_ != nullptr) {
        RECT editorRect{};
        if (::GetWindowRect(reinterpret_cast<HWND>(editorOwnerHwnd_), &editorRect)) {
            // Detect size changes BEFORE updating the cached values.
            // setVisualTransform fires every frame (~60 Hz) — most calls
            // are position-only and must NOT invalidate the prepared
            // scene cache (would defeat the cache and stutter playback).
            // But size changes need a fresh layout: the prepared cache
            // computes per-marker screen positions from the canvas size,
            // so a stale cache after a resize keeps the scene laid out
            // for the OLD canvas (the "playfield offset to the right
            // of the popup" symptom on rapid resize bursts).
            const bool sizeChanged = !hasLastPopupGeometry_
                || lastPopupDisplayWPx_ != displayWPx
                || lastPopupDisplayHPx_ != displayHPx;
            lastEditorOriginXPx_ = editorRect.left;
            lastEditorOriginYPx_ = editorRect.top;
            lastPopupOffsetXPx_ = xPx - editorRect.left;
            lastPopupOffsetYPx_ = yPx - editorRect.top;
            lastPopupDisplayWPx_ = displayWPx;
            lastPopupDisplayHPx_ = displayHPx;
            hasLastPopupGeometry_ = true;
            if (sizeChanged) {
                ++lastWindowRevision_;
                frameState_.sceneContentRevision = lastWindowRevision_;
                appendWorkerLog(
                    QStringLiteral("scene_cache_invalidate_resize"),
                    QStringLiteral("trigger=set_visual_transform wh=%1x%2 revision=%3")
                        .arg(displayWPx).arg(displayHPx).arg(lastWindowRevision_));
            }
            appendWorkerLog(QStringLiteral("set_visual_transform_apply"),
                            QStringLiteral("popup_xy=%1,%2 wh=%3x%4 editor_origin=%5,%6 popup_offset=%7,%8")
                                .arg(xPx).arg(yPx).arg(displayWPx).arg(displayHPx)
                                .arg(editorRect.left).arg(editorRect.top)
                                .arg(lastPopupOffsetXPx_).arg(lastPopupOffsetYPx_));
        }
    }
#else
    Q_UNUSED(xPx);
    Q_UNUSED(yPx);
    Q_UNUSED(displayWPx);
    Q_UNUSED(displayHPx);
#endif
}

void PreviewWorkerSession::onOwnerLocationChanged(int left, int top, int right, int bottom)
{
#ifdef Q_OS_WIN
    if (popupHwnd_ != nullptr && hasLastPopupGeometry_) {
        const int xPx = left + lastPopupOffsetXPx_;
        const int yPx = top + lastPopupOffsetYPx_;
        if (qsgMode_) {
            const qreal dpr = (qsgQuickWindow_ != nullptr
                                && qsgQuickWindow_->effectiveDevicePixelRatio() > 0.0)
                              ? qsgQuickWindow_->effectiveDevicePixelRatio()
                              : 1.0;
            if (qsgQuickWindow_ != nullptr) {
                qsgQuickWindow_->setPosition(qRound(xPx / dpr), qRound(yPx / dpr));
            }
        } else {
            ::SetWindowPos(reinterpret_cast<HWND>(popupHwnd_),
                           nullptr,
                           xPx,
                           yPx,
                           lastPopupDisplayWPx_,
                           lastPopupDisplayHPx_,
                           SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOREDRAW);
        }

        lastEditorOriginXPx_ = left;
        lastEditorOriginYPx_ = top;
        appendWorkerLog(QStringLiteral("owner_location_follow"),
                        QStringLiteral("popup_xy=%1,%2 wh=%3x%4 editor_origin=%5,%6 qsg=%7")
                            .arg(xPx).arg(yPx)
                            .arg(lastPopupDisplayWPx_).arg(lastPopupDisplayHPx_)
                            .arg(left).arg(top)
                            .arg(qsgMode_ ? 1 : 0));
        return;
    }
    Q_UNUSED(left);
    Q_UNUSED(top);
    Q_UNUSED(right);
    Q_UNUSED(bottom);
    // Phase 4 update — the editor publishes geometry via setVisualTransform
    // on every PreviewRuntime::frameStateChanged tick (~60 Hz) with
    // freshly-projected scene→global pixel coords that account for
    // current DPR. The OwnerHwndTracker's location-driven popup
    // reposition was originally a fast-path for the gap between
    // snapshot publishes, but it caches a relative offset that becomes
    // stale across DPI changes (editor moves to a different-DPI
    // monitor: editor rect is in new physical pixels, cached offset
    // is in old physical pixels → popup lands at the wrong place).
    //
    // We keep the tracker registration alive for two purposes that
    // remain valid:
    //   1. The watchdog re-registers the WinEventHook after RDP
    //      detach (without it the popup would freeze if the user
    //      reconnects an RDP session).
    //   2. Future fast-path: if we observe the editor having moved
    //      without a corresponding setVisualTransform within ~50 ms,
    //      we can use this callback to nudge the popup. Not yet
    //      wired since the per-tick push from the editor side
    //      handles all observed cases.
    //
    // Until a divergence is observed, the callback is a no-op so it
    // can't race with handleSetVisualTransform.
#endif
}

void PreviewWorkerSession::handleResize(int wPx, int hPx)
{
    MC_OP("PreviewWorkerSession::handleResize");
    _mc_op_.note(QStringLiteral("w=%1 h=%2").arg(wPx).arg(hPx));
#ifdef Q_OS_WIN
    if (core_ == nullptr || wPx <= 0 || hPx <= 0) {
        return;
    }
    core_->resize(QSize(wPx, hPx));
    // The prepared scene cache lays out marker screen positions from
    // the canvas size at sync() time; without invalidation here the
    // cache holds the OLD layout and the next render places sprites
    // for a viewport that's no longer current. Bump unconditionally
    // since handleResize is only called on real geometry deltas.
    ++lastWindowRevision_;
    frameState_.sceneContentRevision = lastWindowRevision_;
    appendWorkerLog(
        QStringLiteral("scene_cache_invalidate_resize"),
        QStringLiteral("trigger=handle_resize wh=%1x%2 revision=%3")
            .arg(wPx).arg(hPx).arg(lastWindowRevision_));
#else
    Q_UNUSED(wPx);
    Q_UNUSED(hPx);
#endif
}

void PreviewWorkerSession::pumpQsgFrame()
{
#ifdef Q_OS_WIN
    if (!qsgMode_) {
        return;
    }
    // Per-frame timing: capture work breakdown for stutter analysis.
    // QElapsedTimer at ns precision; readings are cheap (~30 ns).
    QElapsedTimer pumpTimer;
    pumpTimer.start();
    qint64 inflateNs = 0;
    qint64 fingerprintNs = 0;
    qint64 updateRequestNs = 0;
    int taps = 0;
    int holds = 0;
    int slides = 0;
    int wifis = 0;
    int touches = 0;
    int touchHolds = 0;
    int unknowns = 0;
    bool didCacheBump = false;
    if (lastSnapshotValid_ && qsgSceneRoot_ != nullptr) {
        frameState_.playheadSeconds = lastSnapshot_.playheadSeconds;
        frameState_.fpsDisplay = lastSnapshot_.fpsDisplay;
        frameState_.tickFpsDisplay = lastSnapshot_.tickFpsDisplay;
        frameState_.updateRequestFpsDisplay = lastSnapshot_.updateRequestFpsDisplay;
        frameState_.framePacingTargetFps = lastSnapshot_.framePacingTargetFps;
        frameState_.displayRefreshRate = lastSnapshot_.displayRefreshRate;
        frameState_.framePacingUsesDisplayRefresh = lastSnapshot_.framePacingUsesDisplayRefresh != 0;
        frameState_.tickCount = lastSnapshot_.tickCount;
        frameState_.updateRequestCount = lastSnapshot_.updateRequestCount;
        frameState_.presentedFrameCount = lastSnapshot_.presentedFrameCount;
        // HUD reads these for the stutter row — without copy, the
        // worker's frameState_ keeps zeros and the HUD shows "0 / 0 ms"
        // even when the editor recorded spikes.
        frameState_.presentMaxMsDisplay = lastSnapshot_.presentMaxMsDisplay;
        frameState_.tickMaxMsDisplay = lastSnapshot_.tickMaxMsDisplay;
        frameState_.updateRequestMaxMsDisplay = lastSnapshot_.updateRequestMaxMsDisplay;
        frameState_.presentStutterCountDisplay = lastSnapshot_.presentStutterCountDisplay;
        frameState_.tickStutterCountDisplay = lastSnapshot_.tickStutterCountDisplay;
        frameState_.updateRequestStutterCountDisplay = lastSnapshot_.updateRequestStutterCountDisplay;

        // muriRenderOptions — these gate which Muri / chart-review
        // layers render and feed the prepared-cache key. Without this
        // projection the worker stayed on `MuriRenderOptions{}` defaults
        // (Native, no judge markers, no touch trail, etc.), so user
        // toggles never affected the popup.
        const auto muriBits = lastSnapshot_.muriRenderFlagsBitmap;
        frameState_.muriRenderOptions.renderMode =
            static_cast<RenderMode>(lastSnapshot_.muriRenderModeKind);
        frameState_.muriRenderOptions.showSlideTracks =
            (muriBits & miacode::preview::ipc::MuriRenderFlags::kShowSlideTracks) != 0;
        frameState_.muriRenderOptions.showJudgeMarkers =
            (muriBits & miacode::preview::ipc::MuriRenderFlags::kShowJudgeMarkers) != 0;
        frameState_.muriRenderOptions.showTouchTrail =
            (muriBits & miacode::preview::ipc::MuriRenderFlags::kShowTouchTrail) != 0;
        frameState_.muriRenderOptions.showChartReviewSlideJudgeOverlay =
            (muriBits & miacode::preview::ipc::MuriRenderFlags::kShowChartReviewSlideJudgeOverlay) != 0;
        frameState_.muriRenderOptions.showChartReviewTapJudgeOverlay =
            (muriBits & miacode::preview::ipc::MuriRenderFlags::kShowChartReviewTapJudgeOverlay) != 0;
        frameState_.muriRenderOptions.showChartReviewTouchJudgeOverlay =
            (muriBits & miacode::preview::ipc::MuriRenderFlags::kShowChartReviewTouchJudgeOverlay) != 0;
        frameState_.muriRenderOptions.wifiNeedC =
            (muriBits & miacode::preview::ipc::MuriRenderFlags::kWifiNeedC) != 0;
        frameState_.muriRenderOptions.excludeTouchFromMultiTouch =
            (muriBits & miacode::preview::ipc::MuriRenderFlags::kExcludeTouchFromMultiTouch) != 0;

        // PreviewRenderState — drives sprite scroll cadence (flow
        // speeds), backdrop scale, background brightness, slide /
        // text z-order. Wrong defaults here meant the worker drew
        // sprites with the engine-default flow speed (1.0×) regardless
        // of the user's "Tap flow speed" / "Touch flow speed" sliders,
        // so visible-window timing diverged from the editor.
        const auto renderBits = lastSnapshot_.renderFlagsBitmap;
        frameState_.render.tapFlowSpeed = lastSnapshot_.tapFlowSpeed;
        frameState_.render.touchFlowSpeed = lastSnapshot_.touchFlowSpeed;
        frameState_.render.backgroundBrightnessOuter = lastSnapshot_.backgroundBrightnessOuter;
        frameState_.render.backgroundBrightnessInner = lastSnapshot_.backgroundBrightnessInner;
        frameState_.render.layoutSquareScale = lastSnapshot_.layoutSquareScale;
        frameState_.render.backgroundScaleMode =
            static_cast<PreviewBackgroundScaleMode>(
                lastSnapshot_.backgroundScaleModeKind);
        frameState_.render.smoothBrightness =
            (renderBits & miacode::preview::ipc::RenderFlags::kSmoothBrightness) != 0;
        frameState_.render.slideEarlierSecondAndTextOnTop =
            (renderBits & miacode::preview::ipc::RenderFlags::kSlideEarlierSecondAndTextOnTop) != 0;
        frameState_.render.showDebugInfo =
            (renderBits & miacode::preview::ipc::RenderFlags::kShowDebugInfo) != 0;
        frameState_.render.showTimestamp =
            (renderBits & miacode::preview::ipc::RenderFlags::kShowTimestamp) != 0;
        frameState_.render.showObjectStatsHud =
            (renderBits & miacode::preview::ipc::RenderFlags::kShowObjectStatsHud) != 0;

        // Asset state + skin / judge / firework images come from the
        // worker's repository. Refresh only when the repository signals
        // a change: skinAssets() etc. return a struct of ~60 QImages by
        // value, and even though QImage is implicitly shared, doing 60
        // atomic ref-bumps per frame at 60 Hz is wasted work when the
        // assets have not changed.
        if (assetsDirty_ && assetRepository_ != nullptr) {
            frameState_.skin = assetRepository_->skinAssets();
            frameState_.judgeOverlay = assetRepository_->judgeOverlayAssets();
            frameState_.judgeEffect = assetRepository_->judgeEffectAssets();
            frameState_.assets = assetRepository_->assetState();
            assetsDirty_ = false;
        }

        // Stage media — Phase 1: image bg only. Image was async-loaded
        // by onSnapshotMediaImagePathChanged on the worker; copy it into
        // frameState_.media so PreviewQuickStageBackgroundLayer renders
        // it via QSG inside the worker popup. With a non-null image the
        // worker's popup becomes opaque in the chart area; the editor's
        // QML PreviewStageMediaItem behind the popup is hidden by it.
        // Video bg (mediaKind=2) is handled in Phase 2 — for now the
        // worker leaves these fields default and the editor's QML
        // continues to render video through the popup's transparent
        // areas.
        // "Pause and do not display PV/BG" gate. When the editor's
        // setting hides bg, force-clear the bg slots regardless of the
        // worker's cached image / video frame. The dirty flags are
        // bumped on the visibility transition so this branch fires
        // exactly once per toggle.
        const bool bgVisible = lastAppliedMediaVisible_;

        if (workerStageImageDirty_) {
            if (!bgVisible || workerStageImage_.isNull()) {
                frameState_.media.resolvedStageImage = QImage();
                frameState_.media.stageMediaAvailable = false;
                frameState_.media.stageMediaSerial = 0;
                frameState_.media.resolvedStageImageCacheable = false;
            } else {
                frameState_.media.resolvedStageImage = workerStageImage_;
                frameState_.media.resolvedStageImageCacheable = true;
                frameState_.media.resolvedStageImageToImageMs = 0.0;
                frameState_.media.stageMediaSerial = workerStageImageSerial_;
                frameState_.media.stageMediaAvailable = true;
                // Internal layer: editor's QML PreviewStageMediaItem
                // sits at z=0 behind us, but our opaque bg covers it.
                // PreviewStageMediaPresentationMode::InternalLayer is
                // the default and matches the legacy LWA path —
                // keeping it tells PreviewQuickStageBackgroundLayer to
                // render the image directly without trying to fall
                // through to a sibling QQuickItem.
                frameState_.media.presentationMode =
                    miacode::preview::scene::PreviewStageMediaPresentationMode::InternalLayer;
            }
            workerStageImageDirty_ = false;
        }

        // Video bg — feed the latest decoded video frame into
        // mediaFrame (kept distinct from resolvedStageImage so the
        // layer's image-vs-video gate works). The 33 ms throttle on the
        // sink connection caps the upload rate to ~30/s.
        if (workerVideoFrameDirty_) {
            if (!bgVisible || workerVideoFrame_.isNull()) {
                frameState_.media.mediaFrame = QImage();
                frameState_.media.stageMediaAvailable = false;
                frameState_.media.stageMediaSerial = 0;
            } else {
                frameState_.media.mediaFrame = workerVideoFrame_;
                frameState_.media.stageMediaSerial = workerVideoFrameSerial_;
                frameState_.media.stageMediaAvailable = true;
                frameState_.media.presentationMode =
                    miacode::preview::scene::PreviewStageMediaPresentationMode::InternalLayer;
            }
            workerVideoFrameDirty_ = false;
        }
        // Keep video position aligned to the editor's audio-authoritative
        // playhead. Throttled internally (>= 40 ms drift threshold).
        syncVideoPlayhead(lastSnapshot_.playheadSeconds);
        // Compute the cheap visible-window fingerprint FIRST. If the
        // sprite set is unchanged, we skip the per-frame re-inflate
        // entirely — TimelineNoteMarker contains nested QVectors for
        // slide segment geometry, so allocating ~30 of them every
        // frame at 60 Hz is one of the dominant per-frame costs.
        // Cursor advancement based on playhead happens inside the QSG
        // layer code, which still runs every frame — so visuals stay
        // up-to-date even when the underlying marker list is reused.
        const qint64 fpStartNs = pumpTimer.nsecsElapsed();
        const quint32 spriteCount = lastSnapshot_.spriteCount;
        quint64 fp = spriteCount;
        if (spriteCount > 0) {
            const auto& first = lastSnapshot_.sprites[0];
            const auto& last = lastSnapshot_.sprites[spriteCount - 1];
            quint64 firstStartBits;
            quint64 lastStartBits;
            std::memcpy(&firstStartBits, &first.startSeconds, sizeof(firstStartBits));
            std::memcpy(&lastStartBits, &last.startSeconds, sizeof(lastStartBits));
            fp = (fp * 0x9E3779B97F4A7C15ULL)
                 ^ firstStartBits
                 ^ (lastStartBits << 1)
                 ^ (static_cast<quint64>(first.lane) << 32)
                 ^ (static_cast<quint64>(last.lane) << 48)
                 ^ (static_cast<quint64>(first.typeKind) << 8)
                 ^ (static_cast<quint64>(last.typeKind) << 24);
        }
        const bool fingerprintChanged = (fp != lastWindowFingerprint_);
        if (fingerprintChanged) {
            ++lastWindowRevision_;
            lastWindowFingerprint_ = fp;
            didCacheBump = true;
        }
        frameState_.sceneContentRevision = lastWindowRevision_;
        fingerprintNs = pumpTimer.nsecsElapsed() - fpStartNs;

        // Re-inflate ONLY when the fingerprint changed. Inflating ~30
        // markers (each with nested slide-segment QVectors) costs a
        // few hundred µs of allocator work; doing it 60×/s when the
        // set is stable is wasted cost. unpackSlideGeometry inside
        // inflateSerialSpriteToMarker restores slide + wifi geometry so
        // slide tracks and wifi notes render with full fidelity.
        // unpackMuriAnalysisReport restores padWindows / actionTrails /
        // judgeSpriteEvents / markerStates so MuriPad / MuriAction /
        // MaimuriDxJudge layers render correctly when toggled on.
        if (fingerprintChanged) {
            const qint64 inflateStartNs = pumpTimer.nsecsElapsed();
            miacode::preview::ipc::inflateActiveSpritesToMarkers(
                lastSnapshot_, &frameState_.noteMarkers);
            miacode::preview::ipc::unpackMuriAnalysisReport(
                lastSnapshot_, &frameState_);
            inflateNs = pumpTimer.nsecsElapsed() - inflateStartNs;
        }

        // PSO warm-up: inject a synthetic touch+firework marker once,
        // right after assets have finished loading. The firework layer
        // builds geometry for it, the QSG render thread compiles the
        // PreviewQuickJudgeFireworkMaterial PSO, and firework textures
        // upload to GPU. After this single frame the synthetic marker
        // is gone (next pump's inflate replaces noteMarkers) — but the
        // PSO + textures are now warm in the QSG cache. The user sees
        // a brief firework flash here, but never the ~50-300 ms hitch
        // when the first real firework fires later.
        //
        // Touch-firework rules from PreviewJudgeFireworkLayerState:
        //   - type must be "touch" or "touch_hold"
        //   - touchPoint must be non-zero (qFuzzyIsNull check)
        //   - elapsed = playhead - (second + 0.05) must be in [0, 1.33]
        // Setting second = playhead - 0.05 puts elapsed = 0.0 — the
        // first frame of the firework's curve, where scale + alpha
        // are both > 0 and the layer issues a real draw.
        //
        // touchPoint is in logical canvas coordinates (0..kLogicalCanvasSize
        // ≈ 1024). Setting it far off-canvas (-1e6) puts the firework
        // center, the touch sprite (touchLayer also picks up this marker
        // via the prepared cache rebuild), AND the judge sprite
        // (touchJudgeLayer too) all far outside the popup's viewport.
        // QSG still builds the nodes and the render thread binds the
        // PreviewQuickJudgeFireworkMaterial — the goal of this whole
        // warmup, since the PSO compile is what stalls the first real
        // firework — but the GPU rasterizer clips every triangle outside
        // the viewport so no pixels are visible. Earlier revisions used
        // (1.0, 1.0) which compiled the PSO but ALSO produced a visible
        // burst + extra touch sprite at the playfield's upper-left
        // corner during the first frame after attach, which polluted
        // the chart's visible content and confused users about what
        // notes the chart actually contained.
        if (warmupFireworkPending_) {
            TimelineNoteMarker synth;
            synth.type = QStringLiteral("touch");
            synth.isFirework = true;
            synth.second = lastSnapshot_.playheadSeconds - 0.05;
            synth.endSecond = -1.0;
            synth.touchPoint = QPointF(-1.0e6, -1.0e6);  // off-screen, non-zero
            synth.lane = 1;
            frameState_.noteMarkers.append(synth);
            // Force a cache rebuild so the synthetic firework actually
            // lands in the prepared judgeFireworkLayer slot. Without
            // this bump the cache key would still match the prior
            // fingerprint and sync() would short-circuit.
            ++lastWindowRevision_;
            frameState_.sceneContentRevision = lastWindowRevision_;
            warmupFireworkPending_ = false;
            didCacheBump = true;
            appendWorkerLog(QStringLiteral("firework_warmup_inject"),
                            QStringLiteral("playhead=%1 touch_point=offscreen")
                                .arg(lastSnapshot_.playheadSeconds, 0, 'f', 3));
        }
        // Tally markers by type so we can spot which layers are empty
        // when "objects are missing". Only re-tally when the marker set
        // actually changed (otherwise prior counts still apply). Cached
        // values feed the periodic qsg_tick log.
        if (fingerprintChanged) {
            cachedMarkerTaps_ = 0;
            cachedMarkerHolds_ = 0;
            cachedMarkerSlides_ = 0;
            cachedMarkerWifis_ = 0;
            cachedMarkerTouches_ = 0;
            cachedMarkerTouchHolds_ = 0;
            cachedMarkerUnknowns_ = 0;
            for (const TimelineNoteMarker& m : frameState_.noteMarkers) {
                const QString t = m.type.toLower();
                if (t == QLatin1String("tap")) ++cachedMarkerTaps_;
                else if (t == QLatin1String("hold")) ++cachedMarkerHolds_;
                else if (t == QLatin1String("slide")) ++cachedMarkerSlides_;
                else if (t == QLatin1String("wifi")) ++cachedMarkerWifis_;
                else if (t == QLatin1String("touch")) ++cachedMarkerTouches_;
                else if (t == QLatin1String("touchhold") || t == QLatin1String("touch_hold")) ++cachedMarkerTouchHolds_;
                else ++cachedMarkerUnknowns_;
            }
        }
        taps = cachedMarkerTaps_;
        holds = cachedMarkerHolds_;
        slides = cachedMarkerSlides_;
        wifis = cachedMarkerWifis_;
        touches = cachedMarkerTouches_;
        touchHolds = cachedMarkerTouchHolds_;
        unknowns = cachedMarkerUnknowns_;

        const qint64 updateStartNs = pumpTimer.nsecsElapsed();
        qsgSceneRoot_->update();
        if (qsgHudLayer_ != nullptr) {
            qsgHudLayer_->update();
        }
        updateRequestNs = pumpTimer.nsecsElapsed() - updateStartNs;
    }
    ++framesPresented_;
    maybeInjectCrash();

    const qint64 totalNs = pumpTimer.nsecsElapsed();
    // Log on a periodic cadence (60 frames ≈ 1 s) AND on any slow frame
    // (> 8 ms in pumpQsgFrame is unexpected — most of it should be
    // sub-millisecond; the heavy lifting is in QSG's render thread).
    static thread_local int s_qsgTickCounter = 0;
    constexpr qint64 kSlowPumpNs = 8 * 1000 * 1000;
    const bool slowFrame = totalNs > kSlowPumpNs;
    const bool periodicSample = (++s_qsgTickCounter % 60 == 1);
    if (periodicSample || slowFrame) {
        // Sample the render-thread present stats and reset the rolling
        // counters so each qsg_tick line covers the prior ~1 s window.
        const quint64 renderPresented = renderFramesPresented_.load(std::memory_order_relaxed);
        const qint64 renderIntervalSumNs = renderPresentIntervalSumNs_.exchange(0, std::memory_order_relaxed);
        const qint64 renderIntervalMaxNs = renderPresentIntervalMaxNs_.exchange(0, std::memory_order_relaxed);
        const quint64 renderIntervalSamples = renderPresentIntervalSampleCount_.exchange(0, std::memory_order_relaxed);
        const quint64 renderStutters = renderPresentStutterCount_.exchange(0, std::memory_order_relaxed);
        const double avgIntervalMs = renderIntervalSamples > 0
            ? (static_cast<double>(renderIntervalSumNs) / renderIntervalSamples) / 1.0e6
            : -1.0;
        const double maxIntervalMs = static_cast<double>(renderIntervalMaxNs) / 1.0e6;

        appendWorkerLog(slowFrame ? QStringLiteral("qsg_pump_slow")
                                   : QStringLiteral("qsg_tick"),
                        QStringLiteral(
                            "total_ms=%1 inflate_ms=%2 fp_ms=%3 update_req_ms=%4 "
                            "markers=%5 tap=%6 hold=%7 slide=%8 wifi=%9 touch=%10 thold=%11 unk=%12 "
                            "cache_bump=%13 playhead=%14 skin_loaded=%15 win=%16x%17 "
                            "pump_frames=%18 present_frames=%19 present_avg_ms=%20 present_max_ms=%21 "
                            "present_samples=%22 present_stutters=%23")
                            .arg(static_cast<double>(totalNs) / 1.0e6, 0, 'f', 3)
                            .arg(static_cast<double>(inflateNs) / 1.0e6, 0, 'f', 3)
                            .arg(static_cast<double>(fingerprintNs) / 1.0e6, 0, 'f', 3)
                            .arg(static_cast<double>(updateRequestNs) / 1.0e6, 0, 'f', 3)
                            .arg(frameState_.noteMarkers.size())
                            .arg(taps).arg(holds).arg(slides).arg(wifis)
                            .arg(touches).arg(touchHolds).arg(unknowns)
                            .arg(didCacheBump ? 1 : 0)
                            .arg(frameState_.playheadSeconds, 0, 'f', 3)
                            .arg(assetRepository_ != nullptr
                                     && assetRepository_->hasCoreSkinAssetsLoaded() ? 1 : 0)
                            .arg(qsgQuickWindow_ != nullptr ? qsgQuickWindow_->width() : -1)
                            .arg(qsgQuickWindow_ != nullptr ? qsgQuickWindow_->height() : -1)
                            .arg(framesPresented_)
                            .arg(renderPresented)
                            .arg(avgIntervalMs < 0 ? QStringLiteral("na")
                                                    : QString::number(avgIntervalMs, 'f', 3))
                            .arg(QString::number(maxIntervalMs, 'f', 3))
                            .arg(renderIntervalSamples)
                            .arg(renderStutters));
    }
#endif
}

void PreviewWorkerSession::installQsgRenderHooks()
{
    if (qsgQuickWindow_ == nullptr) {
        return;
    }

    // Phase 4 — Render-thread MMCSS registration. Mirrors the inline
    // PreviewQuickSceneRoot pattern at PreviewQuickSceneRoot.cpp:467.
    // The inline path's lambda is gated on `runtime_ != nullptr` and the
    // worker never owns a runtime — so without this parallel hook the
    // worker's QSG render thread runs at default priority and gets
    // preempted for ~one full quantum (~22 ms) under load. Direct
    // connection so the lambda runs on the render thread itself; the
    // atomic guard collapses subsequent frames to a single load.
    renderMmcssRegistrationAttempted_.store(false, std::memory_order_relaxed);
    renderMmcssBootstrapConnection_ = QObject::connect(
        qsgQuickWindow_, &QQuickWindow::beforeSynchronizing, this,
        [this]() {
            bool expected = false;
            if (!renderMmcssRegistrationAttempted_.compare_exchange_strong(
                    expected, true, std::memory_order_acq_rel)) {
                return;
            }
            const miacode::mmcss::RegistrationResult result =
                miacode::mmcss::registerCurrentThread(QStringLiteral("Games"));
            appendWorkerLog(
                QStringLiteral("mmcss_register"),
                QStringLiteral("registered=%1 task_class=%2 reason=%3 errno=%4")
                    .arg(result.registered ? 1 : 0)
                    .arg(result.taskClassUsed.isEmpty() ? QStringLiteral("(none)") : result.taskClassUsed)
                    .arg(result.skipReason.isEmpty() ? QStringLiteral("(ok)") : result.skipReason)
                    .arg(result.lastErrorCode));
        },
        Qt::DirectConnection);

    // sceneGraphInvalidated fires on the render thread before the QSG
    // tears down (e.g. window hide). Release the MMCSS reservation so
    // a subsequent rebind picks it up again on the new render thread.
    renderSceneGraphInvalidatedConnection_ = QObject::connect(
        qsgQuickWindow_, &QQuickWindow::sceneGraphInvalidated, this,
        [this]() {
            miacode::mmcss::unregisterCurrentThread();
            renderMmcssRegistrationAttempted_.store(false, std::memory_order_release);
        },
        Qt::DirectConnection);

    // Real present-counter on the render thread. `framesPresented_` (in
    // pumpQsgFrame) tracks pump invocations / fresh snapshot reads — NOT
    // actual presented frames, so the two values diverge whenever QSG
    // skips a vsync. This counter reflects what the user actually sees.
    // Also captures inter-present interval for stutter detection.
    renderFrameSwappedConnection_ = QObject::connect(
        qsgQuickWindow_, &QQuickWindow::frameSwapped, this,
        [this]() {
            const qint64 nowNs = renderPhaseTimer_.isValid()
                ? renderPhaseTimer_.nsecsElapsed()
                : 0;
            const qint64 lastNs = renderLastPresentNs_.exchange(
                nowNs, std::memory_order_acq_rel);
            renderFramesPresented_.fetch_add(1, std::memory_order_release);
            if (lastNs >= 0 && nowNs > lastNs) {
                const qint64 intervalNs = nowNs - lastNs;
                renderPresentIntervalSumNs_.fetch_add(
                    intervalNs, std::memory_order_relaxed);
                renderPresentIntervalSampleCount_.fetch_add(
                    1, std::memory_order_relaxed);
                qint64 prevMax = renderPresentIntervalMaxNs_.load(
                    std::memory_order_relaxed);
                while (intervalNs > prevMax
                       && !renderPresentIntervalMaxNs_.compare_exchange_weak(
                              prevMax, intervalNs, std::memory_order_release,
                              std::memory_order_relaxed)) {
                }
                // Stutter = interval > 2× target (33 ms at 60 Hz). Plain
                // increment counter; periodic main-thread reset clears it
                // for the next sample window.
                constexpr qint64 kStutterThresholdNs = 33 * 1000 * 1000;
                if (intervalNs > kStutterThresholdNs) {
                    renderPresentStutterCount_.fetch_add(
                        1, std::memory_order_relaxed);
                }
            }
        },
        Qt::DirectConnection);

    // Render-phase profiler — same four-phase breakdown the inline scene
    // root logs, but driven from the worker's own atomics. Gated on the
    // diagnostic flag because the per-frame write traffic isn't free.
    if (miacode::debug_options::previewFramePacingDiagnosticsEnabled()) {
        if (!renderPhaseTimer_.isValid()) {
            renderPhaseTimer_.start();
        }
        renderBeforeSyncConnection_ = QObject::connect(
            qsgQuickWindow_, &QQuickWindow::beforeSynchronizing, this,
            [this]() {
                renderPhaseSyncStartNs_.store(
                    renderPhaseTimer_.nsecsElapsed(), std::memory_order_release);
            },
            Qt::DirectConnection);
        renderAfterSyncConnection_ = QObject::connect(
            qsgQuickWindow_, &QQuickWindow::afterSynchronizing, this,
            [this]() {
                renderPhaseSyncEndNs_.store(
                    renderPhaseTimer_.nsecsElapsed(), std::memory_order_release);
            },
            Qt::DirectConnection);
        renderBeforeRenderConnection_ = QObject::connect(
            qsgQuickWindow_, &QQuickWindow::beforeRendering, this,
            [this]() {
                renderPhaseRenderStartNs_.store(
                    renderPhaseTimer_.nsecsElapsed(), std::memory_order_release);
            },
            Qt::DirectConnection);
        renderAfterRenderConnection_ = QObject::connect(
            qsgQuickWindow_, &QQuickWindow::afterRendering, this,
            [this]() {
                renderPhaseRenderEndNs_.store(
                    renderPhaseTimer_.nsecsElapsed(), std::memory_order_release);
            },
            Qt::DirectConnection);
        renderProfileSwapConnection_ = QObject::connect(
            qsgQuickWindow_, &QQuickWindow::frameSwapped, this,
            [this]() { recordWorkerRenderPhaseProfile(); },
            Qt::DirectConnection);
    }

    // Always start the timer — the present-counter lambda above uses
    // it for interval stats even when the diagnostic profiler is off.
    if (!renderPhaseTimer_.isValid()) {
        renderPhaseTimer_.start();
    }

    // Diagnostic identification: tag the render thread for WPA / VS
    // debugger so render-thread CPU samples correlate to a name. Set on
    // the first beforeSynchronizing tick (same hook as MMCSS) since the
    // render thread isn't created until QSG starts up. Mirrors the
    // PreviewDCompRenderer L"MiaCodeDComp" tag.
}

void PreviewWorkerSession::uninstallQsgRenderHooks()
{
    QObject::disconnect(renderMmcssBootstrapConnection_);
    renderMmcssBootstrapConnection_ = QMetaObject::Connection();
    QObject::disconnect(renderSceneGraphInvalidatedConnection_);
    renderSceneGraphInvalidatedConnection_ = QMetaObject::Connection();
    QObject::disconnect(renderFrameSwappedConnection_);
    renderFrameSwappedConnection_ = QMetaObject::Connection();
    QObject::disconnect(renderBeforeSyncConnection_);
    renderBeforeSyncConnection_ = QMetaObject::Connection();
    QObject::disconnect(renderAfterSyncConnection_);
    renderAfterSyncConnection_ = QMetaObject::Connection();
    QObject::disconnect(renderBeforeRenderConnection_);
    renderBeforeRenderConnection_ = QMetaObject::Connection();
    QObject::disconnect(renderAfterRenderConnection_);
    renderAfterRenderConnection_ = QMetaObject::Connection();
    QObject::disconnect(renderProfileSwapConnection_);
    renderProfileSwapConnection_ = QMetaObject::Connection();
    // MMCSS unregister was already wired to sceneGraphInvalidated above,
    // but Qt destroys the QQuickWindow before that fires in some teardown
    // orders — release explicitly here too. unregisterCurrentThread is
    // per-thread so this only releases the *main* thread's registration
    // (a no-op because we never registered the main thread); the render
    // thread's reservation is released by the sceneGraphInvalidated
    // path or by the OS on thread exit.
}

void PreviewWorkerSession::recordWorkerRenderPhaseProfile()
{
    // Runs on the render thread (DirectConnection from frameSwapped).
    // Mirrors PreviewQuickSceneRoot::recordRenderPhaseProfile but reads
    // the worker's own atomic phase timestamps. Rate-limited to once per
    // sample interval, plus unconditional log of slow frames.
    if (!renderPhaseTimer_.isValid()) {
        return;
    }
    const qint64 swapNs = renderPhaseTimer_.nsecsElapsed();
    const qint64 syncStart = renderPhaseSyncStartNs_.load(std::memory_order_acquire);
    const qint64 syncEnd = renderPhaseSyncEndNs_.load(std::memory_order_acquire);
    const qint64 renderStart = renderPhaseRenderStartNs_.load(std::memory_order_acquire);
    const qint64 renderEnd = renderPhaseRenderEndNs_.load(std::memory_order_acquire);

    auto deltaMs = [](qint64 endNs, qint64 startNs) -> double {
        if (endNs < 0 || startNs < 0 || endNs < startNs) return -1.0;
        return static_cast<double>(endNs - startNs) / 1.0e6;
    };

    const double syncMs = deltaMs(syncEnd, syncStart);
    const double renderSubmitMs = deltaMs(renderEnd, renderStart);
    const double swapGpuMs = deltaMs(swapNs, renderEnd);
    const double totalMs = deltaMs(swapNs, syncStart);
    // pre_render_wait_ms = afterSync → beforeRender gap. Fat values mean
    // DXGI flip-queue back-pressure (waiting for the GPU to free a
    // back-buffer); near-zero means the render-submit time below is
    // genuinely command-buffer build.
    const double preRenderWaitMs = deltaMs(renderStart, syncEnd);

    constexpr double kSlowFrameMs = 30.0;
    const bool slowFrame = totalMs >= kSlowFrameMs;
    const qint64 nowMs = swapNs / 1000000LL;
    const qint64 sampleMs = miacode::debug_options::previewFramePacingDiagnosticSampleMs();
    const bool sampleReady =
        renderPhaseLastLogMs_ < 0 || (nowMs - renderPhaseLastLogMs_) >= sampleMs;
    if (!slowFrame && !sampleReady) {
        return;
    }
    renderPhaseLastLogMs_ = nowMs;

    appendWorkerLog(
        slowFrame ? QStringLiteral("render_frame_profile_slow")
                  : QStringLiteral("render_frame_profile"),
        QStringLiteral(
            "total_ms=%1 sync_ms=%2 pre_render_wait_ms=%3 render_submit_ms=%4 "
            "swap_gpu_ms=%5 frames_presented=%6 slow=%7")
            .arg(totalMs < 0 ? QStringLiteral("na") : QString::number(totalMs, 'f', 3))
            .arg(syncMs < 0 ? QStringLiteral("na") : QString::number(syncMs, 'f', 3))
            .arg(preRenderWaitMs < 0 ? QStringLiteral("na") : QString::number(preRenderWaitMs, 'f', 3))
            .arg(renderSubmitMs < 0 ? QStringLiteral("na") : QString::number(renderSubmitMs, 'f', 3))
            .arg(swapGpuMs < 0 ? QStringLiteral("na") : QString::number(swapGpuMs, 'f', 3))
            .arg(renderFramesPresented_.load(std::memory_order_relaxed))
            .arg(slowFrame ? 1 : 0));
}

void PreviewWorkerSession::onTestFrameTimerFired()
{
#ifdef Q_OS_WIN
    if (qsgMode_) {
        // QSG mode is driven directly from onSnapshotPollTick — see
        // pumpQsgFrame(). The 30 Hz testFrameTimer_ would just throttle
        // visible refresh and isn't started in QSG mode. Defensive no-op
        // in case it got started somehow.
        return;
    }

    if (core_ == nullptr || !core_->isReady()) {
        return;
    }
    if (core_->isDeviceRemoved()) {
        ipc::emitDeviceRemovedEvent(QStringLiteral("phase0_static_test"), framesPresented_);
        testFrameTimer_.stop();
        snapshotPollTimer_.stop();
        QCoreApplication::quit();
        return;
    }

    bool rendered = false;
    if (lastSnapshotValid_ && spritePipeline_ != nullptr && spritePipeline_->isReady()
        && textureCache_ != nullptr) {
        // Phase 4 worker descriptor-list render — for each sprite in the
        // snapshot, place a colored circle at its lane position based on
        // playhead distance. Visible chart-shaped content driven by the
        // editor's snapshot. Sprites flow inward from the playfield edge
        // toward the center as the playhead approaches their trigger.
        //
        // Slide tracks, judge effects, fireworks, HUD — all deferred.
        // This is the minimum viable descriptor consumer that uses real
        // sprite metadata (lane, type, time) to position visible content.
        miacode::preview::dcomp::PreviewDCompFrameStateSnapshot pipeSnapshot;
        pipeSnapshot.revision = static_cast<qint64>(lastSnapshot_.sequence);
        pipeSnapshot.playheadSeconds = lastSnapshot_.playheadSeconds;
        const QSize swapSize = core_->swapChainPixelSize();
        pipeSnapshot.sceneLogicalSize = swapSize.isValid() ? swapSize : QSize(256, 256);
        pipeSnapshot.playing = lastSnapshot_.framePacingTargetFps > 0.0;

        // Map each visible sprite to a circle descriptor.
        const double playhead = lastSnapshot_.playheadSeconds;
        constexpr double kFlowSeconds = 2.0;  // notes appear ~2s before trigger
        const qreal halfEdge = miacode::preview::scene::kLogicalDistanceEdge;
        const QPointF center(
            miacode::preview::scene::kLogicalCanvasCenter,
            miacode::preview::scene::kLogicalCanvasCenter);

        const quint32 visibleLimit = qMin(
            lastSnapshot_.spriteCount,
            static_cast<quint32>(miacode::preview::ipc::kMaxSerializedSpriteCount));
        pipeSnapshot.circles.reserve(static_cast<int>(visibleLimit));
        for (quint32 i = 0; i < visibleLimit; ++i) {
            const auto& s = lastSnapshot_.sprites[i];
            const double secondsToHit = s.startSeconds - playhead;
            // Sprites within [-0.1s, +flowSeconds] are visible — anything
            // earlier has already passed off-screen, anything later is
            // outside the flow window.
            if (secondsToHit < -0.1 || secondsToHit > kFlowSeconds) {
                continue;
            }
            // Distance from center scales linearly: at trigger (0s) we're
            // at the playfield edge; well before trigger we're near the
            // center; tiny negative is handled by clamping. This matches
            // the maimai standard "notes flow outward from center" model
            // closely enough for a worker preview indicator.
            const double progress = qBound(0.0, secondsToHit / kFlowSeconds, 1.0);
            const qreal radiusFromCenter = (1.0 - progress) * halfEdge;

            const QPointF unit = miacode::preview::scene::laneUnitVector(s.lane);
            const QPointF logicalCenter = center + unit * radiusFromCenter;

            miacode::preview::scene::PreviewCircleDescriptor desc;
            desc.center = logicalCenter;
            desc.radiusX = 25.0;  // logical px; scales with the projection matrix
            desc.radiusY = 25.0;

            // Color by typeKind so different note types are visually
            // distinguishable. Break notes get a desaturated mix.
            using K = miacode::preview::ipc::SerialSpriteTypeKind;
            QColor fill;
            switch (s.typeKind) {
            case K::Tap:       fill = QColor( 80, 200, 240); break;  // cyan
            case K::Hold:      fill = QColor(255, 165,  60); break;  // orange
            case K::Slide:     fill = QColor(255, 220,  50); break;  // yellow
            case K::Wifi:      fill = QColor(180, 130, 240); break;  // violet
            case K::Touch:     fill = QColor(120, 220, 130); break;  // green
            case K::TouchHold: fill = QColor(255, 100, 100); break;  // red
            default:           fill = QColor(200, 200, 200); break;  // grey
            }
            const bool isBreak = (s.flagsBitmap & miacode::preview::ipc::SerialSpriteFlags::kIsBreak) != 0;
            if (isBreak) {
                // Break notes pulsate red-ish — a visible cue.
                fill = QColor::fromRgb(
                    qMin(255, fill.red() + 40),
                    qMax(0, fill.green() - 60),
                    qMax(0, fill.blue() - 60));
            }
            desc.fillColor = fill;
            desc.strokeColor = QColor(255, 255, 255);
            desc.strokeWidth = 2.0;
            pipeSnapshot.circles.push_back(desc);
        }

        if (!pipeSnapshot.circles.isEmpty()) {
            miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
            batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::Circles;
            batch.firstIndex = 0;
            batch.count = pipeSnapshot.circles.size();
            pipeSnapshot.batches.push_back(batch);
        }

        ID3D11DeviceContext* ctx = core_->context();
        ID3D11RenderTargetView* rtv = core_->backBufferRtv();
        if (ctx != nullptr && rtv != nullptr) {
            const float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
            ctx->ClearRenderTargetView(rtv, clearColor);
            spritePipeline_->renderSnapshot(
                ctx, core_->device(), rtv, pipeSnapshot.sceneLogicalSize,
                pipeSnapshot, *textureCache_);
            rendered = core_->present(1);
        }
    } else if (lastSnapshotValid_) {
        // Sprite pipeline unavailable — fall back to the snapshot-driven
        // color cycle so we still get visible motion.
        constexpr float kTwoPi = 6.28318530717958647692f;
        const float playhead = static_cast<float>(lastSnapshot_.playheadSeconds);
        const float r = 0.5f + 0.5f * std::sin(kTwoPi * playhead);
        const float g = 0.5f + 0.5f * std::sin(kTwoPi * playhead + kTwoPi / 3.0f);
        const float b = 0.5f + 0.5f * std::sin(kTwoPi * playhead + 2.0f * kTwoPi / 3.0f);
        const float spriteScale = qBound(
            0.15f,
            0.15f + static_cast<float>(lastSnapshot_.spriteCount) / 64.0f,
            1.0f);
        rendered = core_->renderClear(r * spriteScale, g * spriteScale, b * spriteScale, 1.0f)
                   && core_->present(1);
    } else {
        // No snapshot yet — fall back to the Phase 0 static red rectangle.
        rendered = core_->renderTestFrame();
    }
    if (!rendered) {
        return;
    }
    ++framesPresented_;
    maybeInjectCrash();
#endif
}

void PreviewWorkerSession::maybeInjectCrash()
{
    if (injectCrashAtFrame_ <= 0) {
        return;
    }
    if (framesPresented_ < static_cast<quint64>(injectCrashAtFrame_)) {
        return;
    }
    // Two nested MC_OP frames so the SEH path's shadow dump shows a
    // non-trivial chain — demonstrates that the crash captured
    // logical context, not just "we died at frame N." In production
    // the chain naturally has parent frames; here we fabricate them.
    MC_OP("PreviewWorkerSession::maybeInjectCrash");
    _mc_op_.note(QStringLiteral("frames_presented=%1 crash_at=%2")
                     .arg(framesPresented_)
                     .arg(injectCrashAtFrame_));

    // Log + flush the planned crash so the supervisor scraper can correlate
    // the worker's last log line with the QProcess::CrashExit event. After
    // __fastfail the process is gone — no further IO will happen.
    appendWorkerLog(QStringLiteral("inject_crash_firing"),
                    QStringLiteral("frames_presented=%1 crash_at=%2")
                        .arg(framesPresented_)
                        .arg(injectCrashAtFrame_));
    miacode::debug_log::flushAsyncLogWriter(200);
    std::fflush(stdout);
    std::fflush(stderr);

    // Crash injection mode — controls whether the SEH filter (and
    // hence the Phase 4 shadow buffer) gets a chance to run.
    //
    //   "fastfail" (default) — __fastfail(7) bypasses SEH entirely.
    //                          QProcess sees CrashExit; no shadow log.
    //                          Closest analogue to driver/OS-level
    //                          forcible termination.
    //
    //   "segv" / "av"        — null pointer write triggers an access
    //                          violation that runs through the SEH
    //                          filter. flushSnapshotToDisk() fires
    //                          and writes the shadow chain. QProcess
    //                          still sees CrashExit. Use this mode to
    //                          exercise the cross-process crash log
    //                          collation path.
    //
    //   "abort"              — std::abort(): SIGABRT handler runs,
    //                          shadow chain captured, then re-raised.
    const QByteArray modeRaw = qgetenv("MIACODE_PREVIEW_WORKER_INJECT_CRASH_MODE");
    const QByteArray mode = modeRaw.toLower();
#ifdef _WIN32
    if (mode == "segv" || mode == "av") {
        // Volatile null deref — must be volatile so the compiler
        // doesn't optimise it away as undefined-behaviour-with-no-
        // observable-effect.
        volatile int* p = nullptr;
        *p = 0xDEADBEEF;
        __fastfail(7);  // unreachable; here for compiler control flow
    } else if (mode == "abort") {
        std::abort();
    }
    // FAST_FAIL_FATAL_APP_EXIT (7) — terminates the process via a
    // STATUS_STACK_BUFFER_OVERRUN exception that bypasses every SEH /
    // catch handler. QProcess sees CrashExit. This is closer to a real
    // driver fault than std::abort() because abort() can be intercepted
    // by structured handlers on some Windows builds.
    __fastfail(7);
#else
    Q_UNUSED(mode);
    std::abort();
#endif
}

namespace {

qint64 monotonicNs()
{
    using clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               clock::now().time_since_epoch())
        .count();
}

}  // namespace

void PreviewWorkerSession::onSnapshotPollTick()
{
    if (ringBuffer_ == nullptr || !ringBuffer_->isAttached()) {
        return;
    }

    // Heap-allocate the per-poll scratch — the POD is ~150 KB and the
    // poll fires at 5 ms cadence; cumulative stack pressure would
    // otherwise eventually overflow with deep Qt event dispatch.
    auto latestHeap = std::make_unique<miacode::preview::ipc::PreviewFrameStateSerial>();
    miacode::preview::ipc::PreviewFrameStateSerial& latest = *latestHeap;
    if (!ringBuffer_->readLatest(&latest)) {
        return;
    }

    // De-duplicate: skip rows where the publisher hasn't advanced. The
    // 5 ms poll cadence will see ~3 stale reads per 16.67 ms publish; we
    // only want to record samples on the first observation of a fresh
    // sequence.
    if (latest.sequence == lastObservedSequence_) {
        ++staleReadCount_;
        return;
    }
    if (lastObservedSequence_ != 0 && latest.sequence > lastObservedSequence_ + 1) {
        // Publisher outpaced our read window — count the gap so the
        // summary log surfaces back-pressure events.
        missedSequenceJumps_ += (latest.sequence - lastObservedSequence_ - 1);
    }
    lastObservedSequence_ = latest.sequence;

    // Snapshot is fresh — stash it for the next render frame. Cheap
    // (memcpy of one POD; sized to bound the worst case at compile time).
    lastSnapshot_ = latest;
    lastSnapshotValid_ = true;

    // Asset path: react to changes in the skin directory by kicking off
    // a disk load. The repository internally dedupes against its own
    // current path, so this is safe to call when paths match too — but
    // we filter here to avoid logging churn.
    if (latest.skinDirectory.length > 0
        && latest.skinDirectory.offset + latest.skinDirectory.length <=
               static_cast<quint32>(latest.stringBlob.size())) {
        const QString skinDir = QString::fromUtf8(
            latest.stringBlob.data() + latest.skinDirectory.offset,
            static_cast<int>(latest.skinDirectory.length));
        if (skinDir != lastAppliedSkinDirectory_) {
            onSnapshotSkinDirectoryChanged(skinDir);
        }
    }

    // Chart media — image AND video both rendered inside the worker
    // popup so we don't depend on cross-process popup transparency.
    //   Image (mediaKind==1): async QImageReader, fills
    //                          frameState_.media.resolvedStageImage.
    //   Video (mediaKind==2): QMediaPlayer + QVideoSink; throttled
    //                          toImage() converts each frame and fills
    //                          frameState_.media.mediaFrame.
    //   None  (mediaKind==0): both pipelines cleared.
    QString mediaImagePathFromSnapshot;
    QString mediaVideoPathFromSnapshot;
    if (latest.mediaImagePath.length > 0
        && latest.mediaImagePath.offset + latest.mediaImagePath.length
               <= static_cast<quint32>(latest.stringBlob.size())) {
        mediaImagePathFromSnapshot = QString::fromUtf8(
            latest.stringBlob.data() + latest.mediaImagePath.offset,
            static_cast<int>(latest.mediaImagePath.length));
    }
    if (latest.mediaVideoPath.length > 0
        && latest.mediaVideoPath.offset + latest.mediaVideoPath.length
               <= static_cast<quint32>(latest.stringBlob.size())) {
        mediaVideoPathFromSnapshot = QString::fromUtf8(
            latest.stringBlob.data() + latest.mediaVideoPath.offset,
            static_cast<int>(latest.mediaVideoPath.length));
    }

    // Visibility transition — when the editor's "Pause and do not
    // display PV/BG" toggle goes ON (mediaVisible=0), pause the
    // QMediaPlayer so the FFmpeg/WMF pipeline stops decoding frames
    // we won't show; when it goes OFF (mediaVisible=1), resume play().
    // The pumpQsgFrame body also clears frameState_.media to hide the
    // last visible bg image / frame.
    const bool mediaVisibleNow = latest.mediaVisible != 0;
    if (mediaVisibleNow != lastAppliedMediaVisible_) {
        lastAppliedMediaVisible_ = mediaVisibleNow;
#ifdef HAVE_QT_MULTIMEDIA
        if (videoPlayer_ != nullptr) {
            if (mediaVisibleNow) {
                videoPlayer_->play();
            } else {
                videoPlayer_->pause();
            }
        }
#endif
        appendWorkerLog(QStringLiteral("media_visible_changed"),
                        QStringLiteral("visible=%1").arg(mediaVisibleNow ? 1 : 0));
        // Force the next pump to push a fresh state.media
        // (clear or re-apply the cached image/video frame).
        workerStageImageDirty_ = true;
        workerVideoFrameDirty_ = true;
    }

    if (latest.mediaKind == 1) {
        if (mediaImagePathFromSnapshot != lastAppliedMediaImagePath_
            || latest.mediaSerial != lastAppliedMediaSerial_) {
            onSnapshotMediaImagePathChanged(mediaImagePathFromSnapshot,
                                             latest.mediaSerial);
        }
        if (!lastAppliedMediaVideoPath_.isEmpty()) {
            teardownVideoBackend();
        }
    } else if (latest.mediaKind == 2) {
        if (mediaVideoPathFromSnapshot != lastAppliedMediaVideoPath_) {
            onSnapshotMediaVideoPathChanged(mediaVideoPathFromSnapshot);
        }
        if (!lastAppliedMediaImagePath_.isEmpty()
            || !workerStageImage_.isNull()) {
            workerStageImage_ = QImage();
            workerStageImageSerial_ = latest.mediaSerial;
            workerStageImageDirty_ = true;
            lastAppliedMediaImagePath_.clear();
            lastAppliedMediaSerial_ = latest.mediaSerial;
        }
    } else {
        if (!lastAppliedMediaImagePath_.isEmpty()
            || !workerStageImage_.isNull()) {
            workerStageImage_ = QImage();
            workerStageImageSerial_ = latest.mediaSerial;
            workerStageImageDirty_ = true;
            lastAppliedMediaImagePath_.clear();
            lastAppliedMediaSerial_ = latest.mediaSerial;
        }
        if (!lastAppliedMediaVideoPath_.isEmpty()) {
            teardownVideoBackend();
        }
    }

    // QSG mode: drive the render directly from each fresh snapshot.
    // QSG coalesces back-to-back update() calls and renders at the
    // window's vsync, so the visible refresh rate matches the editor's
    // publish rate (~60 Hz) instead of being capped by testFrameTimer_.
    if (qsgMode_) {
        pumpQsgFrame();
    }

    const qint64 nowNs = monotonicNs();
    const qint64 latencyNs = nowNs - latest.publishMonotonicNs;
    if (latencyNs < 0) {
        // Clock skew between publisher and consumer — the sample is not
        // useful for the latency CSV, but a non-zero count tells us
        // something is off. Log only via summary; don't write a row.
        return;
    }

    ++latencySampleCount_;
    latencySumNs_ += latencyNs;
    if (latencyNs > latencyMaxNs_) {
        latencyMaxNs_ = latencyNs;
    }
    if (latencyNs < latencyMinNs_) {
        latencyMinNs_ = latencyNs;
    }

    writeLatencySample(latencyNs, latest.sequence, latest.playheadSeconds);

    // Flush a rolling summary every ~1 second of wall time. Cheap; just an
    // append to the runtime log channel. Avoids burying the log under
    // per-sample lines while still giving operators a quick read.
    if (lastLatencyFlushNs_ == 0 || (nowNs - lastLatencyFlushNs_) > 1'000'000'000LL) {
        flushLatencySummaryToLog();
        lastLatencyFlushNs_ = nowNs;
    }
}

void PreviewWorkerSession::ensureLatencyCsvOpen()
{
    if (latencyCsvFile_.isOpen()) {
        return;
    }
    const QString logDir = miacode::debug_log::logDirectory();
    if (logDir.isEmpty()) {
        return;
    }
    QDir dir(logDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    const QString fileName = sessionId_.isEmpty()
        ? QStringLiteral("preview_worker_latency.csv")
        : QStringLiteral("preview_worker_latency_%1.csv").arg(sessionId_.left(8));
    latencyCsvFile_.setFileName(dir.filePath(fileName));
    if (!latencyCsvFile_.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        appendWorkerLog(QStringLiteral("latency_csv_open_failed"),
                        QStringLiteral("path=%1 err=%2")
                            .arg(latencyCsvFile_.fileName())
                            .arg(latencyCsvFile_.errorString()));
        return;
    }
    if (latencyCsvFile_.size() == 0) {
        latencyCsvFile_.write("recv_monotonic_ns,sequence,publish_monotonic_ns,latency_ns,playhead_s\n");
        latencyCsvHeaderWritten_ = true;
    }
}

void PreviewWorkerSession::writeLatencySample(qint64 latencyNs, quint64 sequence, double playheadSeconds)
{
    ensureLatencyCsvOpen();
    if (!latencyCsvFile_.isOpen()) {
        return;
    }
    const qint64 nowNs = monotonicNs();
    const QByteArray line = QStringLiteral("%1,%2,%3,%4,%5\n")
                                .arg(nowNs)
                                .arg(sequence)
                                .arg(nowNs - latencyNs)
                                .arg(latencyNs)
                                .arg(playheadSeconds, 0, 'f', 6)
                                .toUtf8();
    latencyCsvFile_.write(line);
}

void PreviewWorkerSession::flushLatencySummaryToLog()
{
    if (latencySampleCount_ == 0) {
        return;
    }
    const qint64 avgNs = latencySumNs_ / static_cast<qint64>(latencySampleCount_);
    appendWorkerLog(
        QStringLiteral("latency_summary_1s"),
        QStringLiteral("samples=%1 avg_us=%2 min_us=%3 max_us=%4 stale_polls=%5 missed_jumps=%6 last_seq=%7")
            .arg(latencySampleCount_)
            .arg(avgNs / 1000)
            .arg(latencyMinNs_ == INT64_MAX ? 0 : latencyMinNs_ / 1000)
            .arg(latencyMaxNs_ / 1000)
            .arg(staleReadCount_)
            .arg(missedSequenceJumps_)
            .arg(lastObservedSequence_));
    if (latencyCsvFile_.isOpen()) {
        latencyCsvFile_.flush();
    }
    // Reset rolling window so the next 1-second log is independent.
    latencySampleCount_ = 0;
    latencySumNs_ = 0;
    latencyMaxNs_ = 0;
    latencyMinNs_ = INT64_MAX;
    staleReadCount_ = 0;
    missedSequenceJumps_ = 0;
}

void PreviewWorkerSession::onSnapshotSkinDirectoryChanged(const QString& newDirectory)
{
    lastAppliedSkinDirectory_ = newDirectory;
    if (assetRepository_ == nullptr) {
        assetRepository_ = std::make_unique<miacode::preview::runtime::PreviewSceneAssetRepository>();
        QObject::connect(assetRepository_.get(),
                         &miacode::preview::runtime::PreviewSceneAssetRepository::assetsChanged,
                         this,
                         &PreviewWorkerSession::onAssetRepositoryAssetsChanged);
    }
    ++assetLoadAttempts_;
    appendWorkerLog(QStringLiteral("asset_load_request"),
                    QStringLiteral("dir=%1 attempt=%2").arg(newDirectory).arg(assetLoadAttempts_));
    assetRepository_->setSkinDirectory(newDirectory);
}

void PreviewWorkerSession::onAssetRepositoryAssetsChanged()
{
    if (assetRepository_ == nullptr) {
        return;
    }
    const bool coreLoaded = assetRepository_->hasCoreSkinAssetsLoaded();
    appendWorkerLog(QStringLiteral("asset_load_complete"),
                    QStringLiteral("dir=%1 core_loaded=%2 attempt=%3")
                        .arg(assetRepository_->skinDirectory())
                        .arg(coreLoaded ? 1 : 0)
                        .arg(assetLoadAttempts_));
    assetsDirty_ = true;
    // Pre-warm the firework PSO on the next pump. We need both the assets
    // (judgeEffect.fireworkColorBallImage etc — must exist for the layer
    // to actually issue a draw) and the QSG render thread up. Both are
    // satisfied by the time this signal fires.
    if (coreLoaded) {
        warmupFireworkPending_ = true;
    }
}

void PreviewWorkerSession::onSnapshotMediaImagePathChanged(const QString& path,
                                                            quint64 serial)
{
    lastAppliedMediaImagePath_ = path;
    lastAppliedMediaSerial_ = serial;
    appendWorkerLog(QStringLiteral("media_image_load_request"),
                    QStringLiteral("path=%1 serial=%2 generation=%3")
                        .arg(path).arg(serial).arg(mediaImageLoadGeneration_ + 1));
    const int generation = ++mediaImageLoadGeneration_;
    QPointer<PreviewWorkerSession> guard(this);
    QThreadPool::globalInstance()->start([guard, path, serial, generation]() {
        QImage loaded;
        if (!path.isEmpty()) {
            loaded = QImage(path);
        }
        // Convert to a render-friendly format ahead of QSG upload so
        // the render thread isn't surprised with a Mono / Indexed8 /
        // ARGB32 input. Premultiplied RGBA is what Qt RHI prefers.
        if (!loaded.isNull()
            && loaded.format() != QImage::Format_RGBA8888_Premultiplied) {
            loaded = loaded.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
        }
        QMetaObject::invokeMethod(
            guard.data(),
            [guard, generation, loaded, serial]() {
                if (guard.isNull()) {
                    return;
                }
                // Drop stale results — another path-change may have come
                // through while this load was in flight.
                if (generation != guard->mediaImageLoadGeneration_) {
                    return;
                }
                guard->workerStageImage_ = loaded;
                guard->workerStageImageSerial_ = serial;
                guard->workerStageImageDirty_ = true;
                appendWorkerLog(
                    QStringLiteral("media_image_load_complete"),
                    QStringLiteral("loaded=%1 size=%2x%3 serial=%4")
                        .arg(loaded.isNull() ? 0 : 1)
                        .arg(loaded.isNull() ? 0 : loaded.width())
                        .arg(loaded.isNull() ? 0 : loaded.height())
                        .arg(serial));
            },
            Qt::QueuedConnection);
    });
}

void PreviewWorkerSession::ensureVideoBackend()
{
#ifdef HAVE_QT_MULTIMEDIA
    if (videoPlayer_ != nullptr) {
        return;
    }
    // FIRST QMediaPlayer + QAudioOutput construction in this process
    // triggers WASAPI/WMF device enumeration which can block the main
    // thread for 1-3 seconds. We do this at attach time so the stall
    // happens during initial worker warm-up — before the user's chart
    // is being rendered — rather than mid-playback when it shows up as
    // a 3-second visible freeze. The QElapsedTimer around the call
    // captures the cost so the log surfaces it.
    QElapsedTimer initTimer;
    initTimer.start();
    videoPlayer_ = new QMediaPlayer(this);
    // Editor owns audio playback. Mirror the editor's
    // PreviewStageMediaHost (lines 172-175): muted+silent QAudioOutput
    // rather than nullptr — Qt 6 QMediaPlayer's pipeline can stall
    // after the first decoded frame when no audio output is attached.
    videoAudioOutput_ = new QAudioOutput(this);
    videoAudioOutput_->setMuted(true);
    videoAudioOutput_->setVolume(0.0f);
    videoPlayer_->setAudioOutput(videoAudioOutput_);
    videoSink_ = new QVideoSink(this);
    videoPlayer_->setVideoSink(videoSink_);
    QObject::connect(
        videoSink_, &QVideoSink::videoFrameChanged, this,
        &PreviewWorkerSession::onVideoFrameArrived);
    QObject::connect(
        videoPlayer_, &QMediaPlayer::errorOccurred, this,
        [this](QMediaPlayer::Error err, const QString& msg) {
            appendWorkerLog(QStringLiteral("media_video_error"),
                            QStringLiteral("error=%1 msg=%2")
                                .arg(static_cast<int>(err)).arg(msg));
        });
    QObject::connect(
        videoPlayer_, &QMediaPlayer::mediaStatusChanged, this,
        [this](QMediaPlayer::MediaStatus s) {
            appendWorkerLog(QStringLiteral("media_video_status"),
                            QStringLiteral("status=%1").arg(static_cast<int>(s)));
        });
    appendWorkerLog(QStringLiteral("media_video_backend_created"),
                    QStringLiteral("audio_disabled=1 init_ms=%1")
                        .arg(initTimer.elapsed()));
#endif
}

void PreviewWorkerSession::onSnapshotMediaVideoPathChanged(const QString& path)
{
#ifdef HAVE_QT_MULTIMEDIA
    lastAppliedMediaVideoPath_ = path;
    lastVideoSeekMs_ = -1;
    appendWorkerLog(QStringLiteral("media_video_path_changed"),
                    QStringLiteral("path=%1").arg(path));

    if (path.isEmpty()) {
        if (videoPlayer_ != nullptr) {
            videoPlayer_->stop();
            videoPlayer_->setSource(QUrl());
        }
        return;
    }

    // Backend should already exist (constructed at attach time). If the
    // editor didn't pre-warm — e.g. some first-time-launch racing
    // condition — fall back to lazy construction here. Will incur the
    // 1-3s stall but at least video plays.
    ensureVideoBackend();

    videoFrameToImageThrottle_.invalidate();
    videoPlayer_->setSource(QUrl::fromLocalFile(path));
    videoPlayer_->play();
#else
    Q_UNUSED(path);
#endif
}

void PreviewWorkerSession::teardownVideoBackend()
{
#ifdef HAVE_QT_MULTIMEDIA
    if (videoPlayer_ != nullptr) {
        videoPlayer_->stop();
        videoPlayer_->setVideoSink(nullptr);
        videoPlayer_->setAudioOutput(nullptr);
        videoPlayer_->deleteLater();
        videoPlayer_ = nullptr;
    }
    if (videoSink_ != nullptr) {
        videoSink_->deleteLater();
        videoSink_ = nullptr;
    }
    if (videoAudioOutput_ != nullptr) {
        videoAudioOutput_->deleteLater();
        videoAudioOutput_ = nullptr;
    }
    workerVideoFrame_ = QImage();
    workerVideoFrameSerial_ = 0;
    workerVideoFrameDirty_ = true;  // next pump clears frameState_.media
    lastAppliedMediaVideoPath_.clear();
    lastVideoSeekMs_ = -1;
    videoFrameToImageThrottle_.invalidate();
    appendWorkerLog(QStringLiteral("media_video_backend_teardown"), QString());
#endif
}

void PreviewWorkerSession::onVideoFrameArrived(const QVideoFrame& frame)
{
#ifdef HAVE_QT_MULTIMEDIA
    if (!frame.isValid()) {
        return;
    }
    // Defensive: when bg is hidden by the editor's "Pause and do not
    // display PV/BG" setting, skip toImage() entirely — saves the
    // ~5-15 ms per-frame CPU cost even if Qt's player happens to emit
    // a straggler frame after pause(). Same gate the editor's host
    // uses (PreviewStageMediaHost.cpp:1568).
    if (!lastAppliedMediaVisible_) {
        return;
    }
    // Same throttle the editor uses for the DComp bg path
    // (PreviewStageMediaHost.cpp:1558). 33 ms cap → ~30 toImage()/s
    // for a 30 fps source, halving CPU on the worker's main thread.
    constexpr qint64 kVideoFrameToImageThrottleMs = 33;
    if (videoFrameToImageThrottle_.isValid()
        && videoFrameToImageThrottle_.elapsed() < kVideoFrameToImageThrottleMs) {
        return;
    }
    QImage decoded = frame.toImage();
    if (decoded.isNull()) {
        return;
    }
    // Convert to a render-friendly format up front so the QSG render
    // thread never has to do an implicit conversion. Premultiplied RGBA
    // matches Qt RHI's preferred upload path on Windows.
    if (decoded.format() != QImage::Format_RGBA8888_Premultiplied) {
        decoded = decoded.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    }
    workerVideoFrame_ = std::move(decoded);
    ++workerVideoFrameSerial_;
    workerVideoFrameDirty_ = true;
    videoFrameToImageThrottle_.restart();
#else
    Q_UNUSED(frame);
#endif
}

void PreviewWorkerSession::syncVideoPlayhead(double playheadSeconds)
{
#ifdef HAVE_QT_MULTIMEDIA
    if (videoPlayer_ == nullptr) {
        return;
    }
    const qint64 targetMs = qMax<qint64>(0, qRound64(playheadSeconds * 1000.0));
    // Compare to the player's ACTUAL position rather than the last seek
    // target. The player advances its own clock when in PlayingState, so
    // the natural progression should keep us in sync; we only need a
    // setPosition when real drift exceeds the tolerance. Calling
    // setPosition on every minor diff (the editor's host pattern of
    // throttling on lastSeekMs_) effectively re-seeks every ~50 ms while
    // playing, fighting the playback pipeline and stalling the player on
    // the first frame after each seek. 200 ms tolerance lets the player
    // run at its native rate while still snapping back if audio playhead
    // jumps (user scrub, paused-seek, or accumulated drift).
    constexpr qint64 kVideoDriftToleranceMs = 200;
    const qint64 playerMs = videoPlayer_->position();
    const qint64 driftMs = qAbs(targetMs - playerMs);
    if (driftMs <= kVideoDriftToleranceMs) {
        return;
    }
    lastVideoSeekMs_ = targetMs;
    videoPlayer_->setPosition(targetMs);
#else
    Q_UNUSED(playheadSeconds);
#endif
}

void PreviewWorkerSession::teardown()
{
    testFrameTimer_.stop();
    snapshotPollTimer_.stop();
    if (ringBuffer_) {
        // Last-second flush so the operator sees the final bucket of
        // samples even if shutdown lands mid-window.
        flushLatencySummaryToLog();
        ringBuffer_->detach();
        ringBuffer_.reset();
    }
    if (latencyCsvFile_.isOpen()) {
        latencyCsvFile_.close();
    }
    if (ownerTracker_) {
        ownerTracker_->unregister();
        ownerTracker_.reset();
    }
    if (assetRepository_) {
        // Disconnect first so an in-flight QThreadPool load completing
        // mid-shutdown doesn't dispatch onAssetRepositoryAssetsChanged
        // against a half-destroyed session.
        QObject::disconnect(assetRepository_.get(), nullptr, this, nullptr);
        assetRepository_.reset();
    }
    // QSG path teardown — unbind frame state, then delete the window
    // (which deletes the scene root child too). Order matters: clearing
    // the frame state pointer first ensures the scene root doesn't
    // dereference a dangling reference during shutdown.
    // Render-thread hooks first so the connections don't fire after
    // their captured state has been torn down. The MMCSS unregister
    // wired to sceneGraphInvalidated will still run when QSG tears
    // down, releasing the render thread's reservation.
    uninstallQsgRenderHooks();
    teardownVideoBackend();
    if (qsgHudLayer_ != nullptr) {
        qsgHudLayer_->setFrameState(nullptr);
        qsgHudLayer_ = nullptr;  // owned by qsgQuickWindow_->contentItem()
    }
    if (qsgSceneRoot_ != nullptr) {
        qsgSceneRoot_->setFrameState(nullptr);
        qsgSceneRoot_ = nullptr;  // owned by qsgQuickWindow_->contentItem()
    }
    if (qsgQuickWindow_ != nullptr) {
        qsgQuickWindow_->hide();
        delete qsgQuickWindow_;
        qsgQuickWindow_ = nullptr;
        // popupHwnd_ is the QQuickWindow's HWND in QSG mode — Qt's
        // delete already destroyed it, so clear the cached pointer to
        // avoid the destroyPopupHwnd() at the end calling DestroyWindow
        // on a stale handle.
        if (qsgMode_) {
            popupHwnd_ = nullptr;
        }
    }
    // Sprite pipeline + texture cache hold GPU resources backed by
    // core_->device(), so they MUST be torn down before core_->shutdown()
    // releases the device. Texture cache first since the pipeline may
    // still hold borrowed SRVs from the cache.
    if (textureCache_) {
        textureCache_.reset();
    }
    if (spritePipeline_) {
        spritePipeline_->shutdown();
        spritePipeline_.reset();
    }
    if (core_) {
        core_->shutdown();
        core_.reset();
    }
    destroyPopupHwnd();
}

void PreviewWorkerSession::onStdinCommandLine(const QByteArray& line)
{
    MC_OP("PreviewWorkerSession::onStdinCommandLine");
    if (line.isEmpty()) {
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        _mc_op_.fail(QStringLiteral("json_parse_failed offset=%1 err=%2")
                         .arg(parseError.offset)
                         .arg(parseError.errorString()));
        ipc::emitFatalEvent(QStringLiteral("json_parse_failed"),
                            QStringLiteral("offset=%1 err=%2")
                                .arg(parseError.offset)
                                .arg(parseError.errorString()));
        QCoreApplication::exit(1);
        return;
    }

    const QJsonObject obj = doc.object();
    const QString cmd = obj.value(QStringLiteral("cmd")).toString();
    _mc_op_.note(QStringLiteral("cmd=%1").arg(cmd));

    if (cmd == QLatin1String(ipc::kCmdAttach)) {
        if (attached_) {
            _mc_op_.fail(QStringLiteral("double_attach"));
            ipc::emitFatalEvent(QStringLiteral("double_attach"), QString());
            QCoreApplication::exit(1);
            return;
        }
        AttachCommand attach;
        attach.editorHwnd = static_cast<quint64>(obj.value(QStringLiteral("editor_hwnd")).toVariant().toLongLong());
        attach.parentPid = static_cast<quint32>(obj.value(QStringLiteral("parent_pid")).toVariant().toLongLong());
        attach.sessionId = obj.value(QStringLiteral("session_id")).toString();
        attach.logDirectory = obj.value(QStringLiteral("log_dir")).toString();
        attach.snapshotShmKey = obj.value(QStringLiteral("snapshot_shm_key")).toString();
        attach.snapshotSlotByteSize = obj.value(QStringLiteral("snapshot_slot_bytes")).toInt();
        attach.snapshotSlotCount = obj.value(QStringLiteral("snapshot_slot_count")).toInt();
        attach.protocolVersion = obj.value(QStringLiteral("protocol")).toInt();
        if (!handleAttach(attach)) {
            QCoreApplication::exit(1);
            return;
        }
        attached_ = true;
        return;
    }

    if (cmd == QLatin1String(ipc::kCmdShutdown)) {
        appendWorkerLog(QStringLiteral("shutdown_received"),
                        QStringLiteral("frames_presented=%1").arg(framesPresented_));
        QCoreApplication::quit();
        return;
    }

    if (cmd == QLatin1String(ipc::kCmdSetVisualTransform)) {
        const int xPx = obj.value(QStringLiteral("x")).toInt();
        const int yPx = obj.value(QStringLiteral("y")).toInt();
        const int displayWPx = obj.value(QStringLiteral("display_w")).toInt();
        const int displayHPx = obj.value(QStringLiteral("display_h")).toInt();
        handleSetVisualTransform(xPx, yPx, displayWPx, displayHPx);
        return;
    }

    if (cmd == QLatin1String(ipc::kCmdResize)) {
        const int wPx = obj.value(QStringLiteral("w")).toInt();
        const int hPx = obj.value(QStringLiteral("h")).toInt();
        handleResize(wPx, hPx);
        return;
    }

    _mc_op_.fail(QStringLiteral("unknown_cmd %1").arg(cmd));
    ipc::emitFatalEvent(QStringLiteral("unknown_cmd"), cmd);
    QCoreApplication::exit(1);
}

void PreviewWorkerSession::onStdinClosed()
{
    // EOF → editor died or closed stdin. Treat as implicit shutdown so the
    // worker doesn't linger as a zombie if the editor crashes.
    appendWorkerLog(QStringLiteral("stdin_eof"), QStringLiteral("treated as shutdown"));
    QCoreApplication::quit();
}

int PreviewWorkerSession::runStaticTest()
{
    MC_OP("PreviewWorkerSession::runStaticTest");
    // Diagnostic startup line on stderr — captured by the supervisor's
    // readyReadStandardError handler. Tells us whether the worker
    // process actually entered runStaticTest and what env vars it
    // saw, so we can confirm the supervisor's QProcessEnvironment
    // really propagated. Also dumps DPI awareness — drift on cross-
    // process MoveWindow would happen if the editor and worker have
    // different DPI awareness contexts, since MoveWindow's
    // pixel-vs-logical interpretation is awareness-dependent.
    {
        const QString runtimeLogPath =
            qEnvironmentVariable("MIACODE_RUNTIME_LOG_PATH");
        const QString debugMode = miacode::debug_options::debugModeEnabled()
                                      ? QStringLiteral("on")
                                      : QStringLiteral("off");
#ifdef Q_OS_WIN
        const DPI_AWARENESS_CONTEXT awareCtx =
            ::GetThreadDpiAwarenessContext();
        const DPI_AWARENESS awareness = ::GetAwarenessFromDpiAwarenessContext(awareCtx);
        const char* awarenessName = "unknown";
        switch (awareness) {
        case DPI_AWARENESS_INVALID:           awarenessName = "invalid"; break;
        case DPI_AWARENESS_UNAWARE:           awarenessName = "unaware"; break;
        case DPI_AWARENESS_SYSTEM_AWARE:      awarenessName = "system"; break;
        case DPI_AWARENESS_PER_MONITOR_AWARE: awarenessName = "per_monitor"; break;
        }
        const bool isPerMonitorV2 = ::AreDpiAwarenessContextsEqual(
            awareCtx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        std::fprintf(stderr,
                     "[preview-worker] enter runStaticTest debug=%s "
                     "dpi_awareness=%s per_monitor_v2=%d "
                     "runtime_log_path=%s\n",
                     debugMode.toUtf8().constData(),
                     awarenessName,
                     isPerMonitorV2 ? 1 : 0,
                     runtimeLogPath.toUtf8().constData());
#else
        std::fprintf(stderr,
                     "[preview-worker] enter runStaticTest debug=%s "
                     "runtime_log_path=%s\n",
                     debugMode.toUtf8().constData(),
                     runtimeLogPath.toUtf8().constData());
#endif
        std::fflush(stderr);
    }

    // Also write a fatal-channel line — fatal-channel writes are
    // synchronous and bypass the AsyncLogWriter, so even if the queued
    // runtime channel writes aren't reaching disk, this one will.
#ifdef Q_OS_WIN
    const DPI_AWARENESS_CONTEXT awareCtx = ::GetThreadDpiAwarenessContext();
    const bool isPerMonitorV2 = ::AreDpiAwarenessContextsEqual(
        awareCtx, DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    miacode::debug_log::appendFatalMessage(
        QStringLiteral("preview/worker_startup"),
        QStringLiteral("pid=%1 runtime_log_path=%2 debug=%3 per_monitor_v2=%4")
            .arg(QCoreApplication::applicationPid())
            .arg(qEnvironmentVariable("MIACODE_RUNTIME_LOG_PATH"))
            .arg(miacode::debug_options::debugModeEnabled() ? 1 : 0)
            .arg(isPerMonitorV2 ? 1 : 0));
#else
    miacode::debug_log::appendFatalMessage(
        QStringLiteral("preview/worker_startup"),
        QStringLiteral("pid=%1 runtime_log_path=%2 debug=%3")
            .arg(QCoreApplication::applicationPid())
            .arg(qEnvironmentVariable("MIACODE_RUNTIME_LOG_PATH"))
            .arg(miacode::debug_options::debugModeEnabled() ? 1 : 0));
#endif

    ipc::emitWorkerReadyEvent();

    // Spawn the stdin reader on its own thread so the main thread's Qt
    // event loop runs unobstructed (timers, queued signals, all dispatch
    // normally). Without this, blocking fgetc() in the main thread
    // starved testFrameTimer_ — frame counts never advanced past 1 and
    // the Phase 5 crash injector never reached its threshold.
    stdinReaderThread_ = new QThread(this);
    stdinReader_ = new StdinCommandReader();
    stdinReader_->moveToThread(stdinReaderThread_);
    QObject::connect(stdinReaderThread_, &QThread::started,
                     stdinReader_, &StdinCommandReader::runReadLoop);
    QObject::connect(stdinReader_, &StdinCommandReader::commandLineReceived,
                     this, &PreviewWorkerSession::onStdinCommandLine,
                     Qt::QueuedConnection);
    QObject::connect(stdinReader_, &StdinCommandReader::stdinClosed,
                     this, &PreviewWorkerSession::onStdinClosed,
                     Qt::QueuedConnection);
    stdinReaderThread_->start();

    const int exitCode = QCoreApplication::exec();

    if (stdinReader_ != nullptr) {
        stdinReader_->requestStop();
    }
    if (stdinReaderThread_ != nullptr) {
        stdinReaderThread_->quit();
        // 200 ms is enough for the reader to notice stop_ and unwind;
        // the blocking fgetc may still be waiting on a closed stdin —
        // we accept that the thread might leak its TLS in that case.
        if (!stdinReaderThread_->wait(200)) {
            stdinReaderThread_->terminate();
            stdinReaderThread_->wait(200);
        }
    }
    if (stdinReader_ != nullptr) {
        stdinReader_->deleteLater();
        stdinReader_ = nullptr;
    }
    stdinReaderThread_ = nullptr;

    teardown();
    return exitCode;
}

int PreviewWorkerSession::run()
{
    MC_OP("PreviewWorkerSession::run");
    // Phase 1+ entry — same control protocol but consumes the snapshot
    // ring buffer for real frame rendering. Until that path lands, the
    // production entry point is the static test loop. Calling site can
    // already wire the env flag; the actual render integration happens
    // when PreviewSnapshotRingBuffer + PreviewWorkerSession's
    // production render loop arrive.
    return runStaticTest();
}

}  // namespace miacode::preview::worker
