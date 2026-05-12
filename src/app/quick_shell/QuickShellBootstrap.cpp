#include "QuickShellBootstrap.h"

#include "QuickShellNativeSurfaceHost.h"
#include "QuickShellController.h"
#include "QuickShellStyleBridge.h"
#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugOptions.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "mainwindow/MainWindow.h"
#include "render/backend_d3d11/PreviewDCompSurface.h"
#include "render/backend_d3d11/PreviewPopupHwndTracker.h"
#include "preview/ipc/PreviewFrameStateProjector.h"
#include "preview/ipc/PreviewFrameStateSerial.h"
#include "preview/ipc/PreviewWorkerSupervisor.h"
// Phase 4c — needed for the host pointer type in the bootstrap
// wiring lambda; the lambda passes it straight to setStageMediaHost.
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/quick_scene/PreviewQuickHudLayer.h"
#include "preview/quick_scene/PreviewQuickSceneRoot.h"
#include "preview/runtime/PreviewRuntime.h"
#include "timeline/quick/TimelineQuickItem.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QScreen>
#include <QIcon>
#include <QKeyEvent>
#include <QLineEdit>
#include <QQmlError>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTextEdit>
#include <QTextStream>
#include <QTimer>
#include <QWidget>
#include <QtQml>

#ifdef Q_OS_WIN
#include <QtCore/qabstractnativeeventfilter.h>
#include <windows.h>
#endif

namespace {

QString pointerHex(const void* pointer)
{
    return QStringLiteral("0x%1").arg(reinterpret_cast<quintptr>(pointer), 0, 16);
}

void appendQuickShellRuntimeLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell"),
        text
    );
}

void appendQuickShellFocusLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell/focus"),
        text
    );
}

QString focusPolicyName(Qt::FocusPolicy policy)
{
    switch (policy) {
    case Qt::NoFocus:
        return QStringLiteral("NoFocus");
    case Qt::TabFocus:
        return QStringLiteral("TabFocus");
    case Qt::ClickFocus:
        return QStringLiteral("ClickFocus");
    case Qt::StrongFocus:
        return QStringLiteral("StrongFocus");
    case Qt::WheelFocus:
        return QStringLiteral("WheelFocus");
    }
    return QStringLiteral("FocusPolicy(%1)").arg(static_cast<int>(policy));
}

QString applicationStateName(Qt::ApplicationState state)
{
    switch (state) {
    case Qt::ApplicationSuspended:
        return QStringLiteral("ApplicationSuspended");
    case Qt::ApplicationHidden:
        return QStringLiteral("ApplicationHidden");
    case Qt::ApplicationInactive:
        return QStringLiteral("ApplicationInactive");
    case Qt::ApplicationActive:
        return QStringLiteral("ApplicationActive");
    }
    return QStringLiteral("ApplicationState(%1)").arg(static_cast<int>(state));
}

#ifdef Q_OS_WIN
class QuickShellNativeCloseEventFilter final : public QAbstractNativeEventFilter
{
public:
    explicit QuickShellNativeCloseEventFilter(QuickShellBootstrap* bootstrap)
        : bootstrap_(bootstrap)
    {}

    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    QuickShellBootstrap* bootstrap_ = nullptr;
};

constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
constexpr DWORD kDwmwaBorderColor = 34;
constexpr DWORD kDwmwaCaptionColor = 35;
constexpr DWORD kDwmwaTextColor = 36;
constexpr DWORD kDwmwaSystemBackdropType = 38;
constexpr DWORD kDwmwaMicaEffect = 1029;
constexpr int kDwmsbtNone = 1;
constexpr int kDwmsbtMainWindow = 2;
constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;

bool setDwmWindowAttribute(HWND hwnd, DWORD attribute, const void* value, DWORD size)
{
    if (hwnd == nullptr || value == nullptr || size == 0) {
        return false;
    }
    static HMODULE dwmapiModule = ::LoadLibraryW(L"dwmapi.dll");
    if (dwmapiModule == nullptr) {
        return false;
    }
    using DwmSetWindowAttributeFn = HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD);
    static auto setWindowAttribute = reinterpret_cast<DwmSetWindowAttributeFn>(
        ::GetProcAddress(dwmapiModule, "DwmSetWindowAttribute")
    );
    if (setWindowAttribute == nullptr) {
        return false;
    }
    return SUCCEEDED(setWindowAttribute(hwnd, attribute, value, size));
}

COLORREF colorRefForDwm(const QColor& color)
{
    return RGB(color.red(), color.green(), color.blue());
}

void applySystemBackdropToQuickWindow(QQuickWindow* window)
{
    if (window == nullptr) {
        return;
    }
    const HWND hwnd = reinterpret_cast<HWND>(window->winId());
    if (hwnd == nullptr) {
        return;
    }

    const BOOL darkMode = UiTheme::isDarkTheme() ? TRUE : FALSE;
    setDwmWindowAttribute(hwnd, kDwmwaUseImmersiveDarkMode, &darkMode, sizeof(darkMode));

    if (UiText::preferredTheme() == UiText::ThemePreference::System) {
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &kDwmColorDefault, sizeof(kDwmColorDefault));
    } else {
        const UiTheme::Colors& themeColors = UiTheme::colors();
        const bool active = window->isActive();
        const COLORREF borderColor = colorRefForDwm(active ? themeColors.borderStrong : themeColors.borderSoft);
        const COLORREF captionColor = colorRefForDwm(active ? themeColors.toolbarBg : themeColors.windowAltBg);
        const COLORREF textColor = colorRefForDwm(active ? themeColors.textPrimary : themeColors.textSecondary);
        setDwmWindowAttribute(hwnd, kDwmwaBorderColor, &borderColor, sizeof(borderColor));
        setDwmWindowAttribute(hwnd, kDwmwaCaptionColor, &captionColor, sizeof(captionColor));
        setDwmWindowAttribute(hwnd, kDwmwaTextColor, &textColor, sizeof(textColor));
    }

    const int backdropType = kDwmsbtMainWindow;
    if (!setDwmWindowAttribute(hwnd, kDwmwaSystemBackdropType, &backdropType, sizeof(backdropType))) {
        const BOOL micaEnabled = TRUE;
        setDwmWindowAttribute(hwnd, kDwmwaMicaEffect, &micaEnabled, sizeof(micaEnabled));
    }
}
#endif

void ensurePreviewQuickTypesRegisteredForQuickShell()
{
    static bool registered = false;
    if (registered) {
        return;
    }
    qmlRegisterType<PreviewQuickSceneRoot>("MiaCode.Preview", 1, 0, "PreviewQuickSceneRoot");
    qmlRegisterType<PreviewQuickHudLayer>("MiaCode.Preview", 1, 0, "PreviewQuickHudLayer");
    qmlRegisterType<TimelineQuickItem>("MiaCode.Timeline", 1, 0, "TimelineQuickItem");
    registered = true;
}

}  // namespace

QuickShellBootstrap::QuickShellBootstrap(const QIcon& appIcon, QObject* parent)
    : QObject(parent)
    , appIcon_(appIcon)
{
}

QuickShellBootstrap::~QuickShellBootstrap()
{
    QElapsedTimer totalTimer;
    totalTimer.start();
    if (!rootWindow_.isNull()) {
        rootWindow_->removeEventFilter(this);
    }
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }
#ifdef Q_OS_WIN
    if (QCoreApplication* app = QCoreApplication::instance(); app != nullptr) {
        if (nativeCloseEventFilter_ != nullptr) {
            app->removeNativeEventFilter(nativeCloseEventFilter_.get());
        }
    }
#endif
    if (backend_ != nullptr) {
        backend_->preparePreviewForShutdown();
        backend_->close();
    }
    const auto logResetTiming = [](const QString& step, auto& pointer) {
        if (!pointer) {
            return;
        }
        QElapsedTimer timer;
        timer.start();
        pointer.reset();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("app_shutdown/quick_shell_bootstrap"),
            step,
            timer.elapsed()
        );
    };
    logResetTiming(QStringLiteral("destroy_engine"), engine_);
    logResetTiming(QStringLiteral("destroy_style_bridge"), styleBridge_);
    logResetTiming(QStringLiteral("destroy_controller"), controller_);
    logResetTiming(QStringLiteral("destroy_surface_host"), surfaceHost_);
    logResetTiming(QStringLiteral("destroy_backend"), backend_);
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/quick_shell_bootstrap"),
        QStringLiteral("destructor_total"),
        totalTimer.elapsed()
    );
}

bool QuickShellBootstrap::start(const QString& startupOpenTarget)
{
    MC_OP("QuickShellBootstrap::start");
    appendQuickShellRuntimeLog(QStringLiteral("start_enter"));
    backend_ = std::make_unique<MainWindow>(true);
    backend_->setQuickShellBackendActive(true);
    backend_->hide();
    backend_->setVisible(false);
    appendQuickShellRuntimeLog(QStringLiteral("backend_ready"));
    surfaceHost_ = std::make_unique<QuickShellNativeSurfaceHost>(backend_.get(), backend_.get(), this);
    backend_->hide();
    backend_->setVisible(false);
    appendQuickShellRuntimeLog(QStringLiteral("surface_host_ready"));
    controller_ = std::make_unique<QuickShellController>(backend_.get(), backend_.get(), surfaceHost_.get(), this);
    appendQuickShellRuntimeLog(QStringLiteral("controller_ready"));
    QObject::connect(
        controller_.get(),
        &QuickShellController::rootCloseAccepted,
        this,
        [this](const QString& source) {
            beginAcceptedRootWindowShutdown(source);
        }
    );
    styleBridge_ = std::make_unique<QuickShellStyleBridge>(backend_.get(), surfaceHost_.get(), this);
    appendQuickShellRuntimeLog(QStringLiteral("style_bridge_ready"));
    if (surfaceHost_ != nullptr && styleBridge_ != nullptr) {
        QObject::connect(
            styleBridge_.get(),
            &QuickShellStyleBridge::appearanceChanged,
            surfaceHost_.get(),
            &QuickShellNativeSurfaceHost::refreshSurfaceStyles
        );
    }
    engine_ = std::make_unique<QQmlApplicationEngine>(this);
    appendQuickShellRuntimeLog(QStringLiteral("engine_ready"));
    if (qApp != nullptr) {
        qApp->installEventFilter(this);
    }
#ifdef Q_OS_WIN
    if (QCoreApplication* app = QCoreApplication::instance(); app != nullptr) {
        nativeCloseEventFilter_ = std::make_unique<QuickShellNativeCloseEventFilter>(this);
        app->installNativeEventFilter(nativeCloseEventFilter_.get());
    }
#endif
    if (QApplication* app = qobject_cast<QApplication*>(qApp); app != nullptr) {
        QObject::connect(app, &QApplication::focusChanged, this, [this](QWidget* old, QWidget* now) {
            if (!shouldTraceFocusObject(old) && !shouldTraceFocusObject(now)) {
                return;
            }
            logFocusEvent(
                QStringLiteral("focus_changed_signal"),
                now,
                nullptr,
                QStringLiteral("old={%1} now={%2}")
                    .arg(describeFocusObject(old))
                    .arg(describeFocusObject(now))
            );
        });
    }
    QObject::connect(qApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        logFocusEvent(
            QStringLiteral("application_state_changed"),
            qApp != nullptr ? qApp->focusWindow() : nullptr,
            nullptr,
            QStringLiteral("state=%1").arg(applicationStateName(state))
        );
    });
    engine_->addImportPath(QCoreApplication::applicationDirPath() + QStringLiteral("/qml"));
    ensurePreviewQuickTypesRegisteredForQuickShell();

    QObject::connect(
        engine_.get(),
        &QQmlApplicationEngine::warnings,
        this,
        [](const QList<QQmlError>& warnings) {
            for (const QQmlError& warning : warnings) {
                appendQuickShellRuntimeLog(QStringLiteral("qml_warning"), warning.toString());
                QTextStream(stderr) << "[QuickShell QML] " << warning.toString() << '\n';
            }
            QTextStream(stderr).flush();
        }
    );
    QObject::connect(
        engine_.get(),
        &QQmlApplicationEngine::objectCreationFailed,
        this,
        [](const QUrl& url) {
            appendQuickShellRuntimeLog(QStringLiteral("qml_object_creation_failed"), url.toString());
            QTextStream(stderr) << "[QuickShell QML] object creation failed for " << url.toString() << '\n';
            QTextStream(stderr).flush();
        }
    );

    qmlRegisterUncreatableType<QuickShellController>(
        "MiaCode.QuickShell",
        1,
        0,
        "QuickShellController",
        "Quick shell controller is provided by bootstrap."
    );
    qmlRegisterUncreatableType<QuickShellStyleBridge>(
        "MiaCode.QuickShell",
        1,
        0,
        "QuickShellStyleBridge",
        "Quick shell style bridge is provided by bootstrap."
    );

    engine_->rootContext()->setContextProperty(QStringLiteral("controller"), controller_.get());
    engine_->rootContext()->setContextProperty(QStringLiteral("styleBridge"), styleBridge_.get());
    const QUrl mainUrl(QStringLiteral("qrc:/quick_shell/qml/QuickShellMain.qml"));
    appendQuickShellRuntimeLog(QStringLiteral("load_begin"), mainUrl.toString());
    engine_->load(mainUrl);
    appendQuickShellRuntimeLog(
        QStringLiteral("load_end"),
        QString("root_objects=%1").arg(engine_->rootObjects().size())
    );
    if (engine_->rootObjects().isEmpty()) {
        appendQuickShellRuntimeLog(QStringLiteral("load_failed"), mainUrl.toString());
        QTextStream(stderr) << "[QuickShell QML] no root object created for " << mainUrl.toString() << '\n';
        QTextStream(stderr).flush();
        engine_.reset();
        styleBridge_.reset();
        controller_.reset();
        surfaceHost_.reset();
        backend_.reset();
        return false;
    }

    if (QQuickWindow* window = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst()); window != nullptr) {
        rootWindow_ = window;
#ifdef Q_OS_WIN
        rootWindowNativeHwnd_ = static_cast<quintptr>(window->winId());
#endif
        window->installEventFilter(this);

        // Phase 1 of the DComp preview path. Opt-in via
        // MIACODE_PREVIEW_USE_DCOMP=1. Renders a red test rectangle in the
        // top-left of the window to verify the DComp visual tree attaches
        // and resizes correctly. Attached lazily on first sceneGraphInitialized
        // (handled inside attachToWindow). Phase 4+ replaces the fixed
        // top-left placement with placeholder-driven geometry.
        //
        // Phase 4 — when the out-of-process worker is enabled AND its
        // QSG render path is on, the worker is the sole chart renderer:
        // suppress the in-process DComp surface so the two popups don't
        // double-render and compete for the same screen rect. This is
        // also THE point of Tier 2 process isolation —
        // DXGI_ERROR_DEVICE_REMOVED on the in-process surface used to
        // cascade into the editor process; with the in-process surface
        // suppressed, the chart popup's D3D11 lifetime lives entirely
        // inside the respawnable worker and crashes are contained.
        const bool workerOwnsChartRender =
            miacode::debug_options::previewOutOfProcessEnabled()
            && miacode::debug_options::previewWorkerQsgRenderEnabled();
        if (workerOwnsChartRender) {
            appendQuickShellRuntimeLog(
                QStringLiteral("dcomp_surface_suppressed"),
                QStringLiteral("reason=worker_qsg_owns_chart"));
        }
        if (miacode::debug_options::previewUseDCompEnabled() && !workerOwnsChartRender) {
            createInProcessPreviewSurface(window, QStringLiteral("env_flag"));
        }

#ifdef Q_OS_WIN
        // Out-of-process preview worker. Opt-in via
        // MIACODE_PREVIEW_OUT_OF_PROCESS=1. Phase 0 of the plan keeps the
        // in-process surface running alongside the worker so the user can
        // visually verify the cross-process popup appears, follows the
        // editor, and survives crash/respawn. Once Phase 4+ proves parity,
        // a follow-up commit gates the in-process surface off when this
        // flag is set.
        if (miacode::debug_options::previewOutOfProcessEnabled()) {
            previewWorkerSupervisor_ = std::make_unique<miacode::preview::ipc::PreviewWorkerSupervisor>(this);
            // The QQuickWindow's HWND is available now because attachToWindow
            // above already nudged sceneGraphInitialized handlers if needed.
            // window->winId() forces native realisation and returns the HWND.
            const quint64 editorHwnd = static_cast<quint64>(window->winId());
            const bool useRealPublisher =
                miacode::debug_options::previewWorkerRealPublisherEnabled();
            QString errorMessage;
            // Two modes share this entry:
            //   default       — Phase 1 synthetic publisher (60 Hz fake data)
            //   real publisher — Phase 4 prototype: connects PreviewRuntime::frameStateChanged
            //                    and projects the live state into the ring buffer
            const bool spawnedOk = useRealPublisher
                ? previewWorkerSupervisor_->spawnWithExternalPublisher(editorHwnd, &errorMessage)
                : previewWorkerSupervisor_->spawnWithSyntheticPublisher(editorHwnd, &errorMessage);
            appendQuickShellRuntimeLog(
                QStringLiteral("preview_worker_spawn"),
                QStringLiteral("ok=%1 editor_hwnd=0x%2 error=%3 phase=%4")
                    .arg(spawnedOk ? 1 : 0)
                    .arg(editorHwnd, 0, 16)
                    .arg(errorMessage)
                    .arg(useRealPublisher
                             ? QStringLiteral("phase0+phase4_prototype_real_publisher")
                             : QStringLiteral("phase0+phase1_synthetic")));

            // Compute the worker popup's screen-space rect from the
            // tracked preview QQuickItem (same source of truth the
            // in-process PreviewDCompSurface uses). The signal-based
            // listeners below cover the common cases (window move,
            // resize, anchored item geometry change), but DPI changes
            // and zoom adjustments don't always fire those signals — so
            // the real-publisher path also re-pushes on every
            // PreviewRuntime::frameStateChanged tick. The supervisor
            // dedupes against `lastVt*` cache so per-frame calls cost
            // one inequality check + a stdin write only when geometry
            // actually changed.
            auto pushPopupGeometry = [this, window]() {
                if (previewWorkerSupervisor_ == nullptr || !previewWorkerSupervisor_->isAttached()) {
                    return;
                }
                QQuickItem* item = window->findChild<QQuickItem*>(
                    QStringLiteral("preview_dcomp_track_target"));
                if (item == nullptr || item->width() <= 0.0 || item->height() <= 0.0) {
                    return;
                }
                const qreal dpr = window->effectiveDevicePixelRatio() > 0.0
                    ? window->effectiveDevicePixelRatio() : 1.0;
                const QPointF topLeftScene = item->mapToScene(QPointF(0, 0));
                const QPointF topLeftGlobal = window->mapToGlobal(topLeftScene);
                const int xPx = qRound(topLeftGlobal.x() * dpr);
                const int yPx = qRound(topLeftGlobal.y() * dpr);
                const int wPx = qRound(item->width() * dpr);
                const int hPx = qRound(item->height() * dpr);
                previewWorkerSupervisor_->setVisualTransform(xPx, yPx, wPx, hPx);
            };

            // Real publisher: hook PreviewRuntime::frameStateChanged →
            // project + publish. The connection is single-shot per spawn;
            // it survives the supervisor object's lifetime and is cleaned
            // up via the QObject parent-child relationship at shutdown.
            if (spawnedOk && useRealPublisher && controller_ != nullptr) {
                if (auto* runtime = qobject_cast<PreviewRuntime*>(
                        controller_->previewRuntime()); runtime != nullptr) {
                    QObject::connect(
                        runtime,
                        &PreviewRuntime::frameStateChanged,
                        this,
                        [this, runtime, pushPopupGeometry]() {
                            if (previewWorkerSupervisor_ == nullptr) {
                                return;
                            }
                            // Heap-allocate — POD is ~150 KB and the
                            // signal fires on every audio tick; stack
                            // pressure would compound through Qt's
                            // queued dispatch frames.
                            auto snapshotHeap = std::make_unique<miacode::preview::ipc::PreviewFrameStateSerial>();
                            miacode::preview::ipc::PreviewFrameStateSerial& snapshot = *snapshotHeap;
                            snapshot.layoutVersion = miacode::preview::ipc::kSerialLayoutVersion;
                            const auto& fs = runtime->frameState();
                            miacode::preview::ipc::projectScalarsToSerial(fs, snapshot);
                            miacode::preview::ipc::projectActiveSpritesToSerial(fs, snapshot);
                            miacode::preview::ipc::projectAssetPathsToSerial(
                                runtime->skinDirectory(), snapshot);
                            miacode::preview::ipc::projectMuriAnalysisReportToSerial(fs, snapshot);
                            // Chart media (PV / BG) paths — worker loads its
                            // own copies so the bg renders inside the worker
                            // popup. mediaKind / mediaVisible drive the
                            // worker's image vs video pipeline switch.
                            QString mediaImagePath;
                            QString mediaVideoPath;
                            int mediaKindValue = 0;
                            bool mediaVisible = false;
                            quint64 mediaSerial = 0;
                            if (backend_ != nullptr) {
                                if (auto* host = backend_->previewStageMediaHost();
                                    host != nullptr) {
                                    if (host->hasResolvedMedia()) {
                                        mediaVisible = host->mediaVisible();
                                        if (host->hasVideoMedia()) {
                                            mediaKindValue = 2;
                                            mediaVideoPath = host->resolvedVideoPath();
                                        } else {
                                            mediaKindValue = 1;
                                            mediaImagePath =
                                                host->imageSource().toLocalFile();
                                        }
                                    }
                                    // Lightweight serial: bump on any path change.
                                    mediaSerial = static_cast<quint64>(
                                        qHash(mediaImagePath)
                                        ^ (static_cast<quint64>(qHash(mediaVideoPath)) << 1)
                                        ^ (static_cast<quint64>(mediaKindValue) << 32)
                                        ^ (mediaVisible ? (1ULL << 48) : 0ULL));
                                }
                            }
                            miacode::preview::ipc::projectMediaPathsToSerial(
                                mediaImagePath, mediaVideoPath,
                                mediaKindValue, mediaVisible, mediaSerial,
                                snapshot);
                            previewWorkerSupervisor_->publishSnapshot(snapshot);
                            // Re-push popup geometry every tick so DPI /
                            // zoom changes propagate even when their
                            // dedicated Qt signals don't fire. Cheap —
                            // supervisor dedupes via its cache.
                            pushPopupGeometry();
                        });
                    appendQuickShellRuntimeLog(
                        QStringLiteral("preview_worker_real_publisher_connected"),
                        QStringLiteral("frame_state_changed=connected"));
                }
            }
            if (spawnedOk) {
                // Bind to the preview item's geometry signals when the item
                // becomes available. The QML scene tree may not be built
                // yet at this point, so we also push from a couple of
                // window-level signals + the worker_attached event.
                auto bindToTrackedItem = [this, window, pushPopupGeometry]() {
                    QQuickItem* item = window->findChild<QQuickItem*>(
                        QStringLiteral("preview_dcomp_track_target"));
                    if (item == nullptr) {
                        return;
                    }
                    auto once = [this, item, pushPopupGeometry](
                                    auto signal) {
                        QObject::connect(item, signal, this, pushPopupGeometry);
                    };
                    once(&QQuickItem::xChanged);
                    once(&QQuickItem::yChanged);
                    once(&QQuickItem::widthChanged);
                    once(&QQuickItem::heightChanged);
                    once(&QQuickItem::visibleChanged);
                    pushPopupGeometry();
                };

                QObject::connect(window, &QQuickWindow::sceneGraphInitialized, this, bindToTrackedItem);

                // Authoritative source of truth for the worker's popup
                // position is the in-process PreviewDCompSurface itself.
                // Whenever it MoveWindows its own popup, it emits
                // popupGeometryApplied with the same coords. Mirroring
                // those exactly to the worker eliminates EVERY drift
                // class:
                //   * fast drag / minimize / maximize transients
                //   * cross-monitor DPI transitions
                //   * post-swap-chain-resize y-adjusts (the in-process
                //     surface re-applies internally without any public
                //     Qt signal exposing the new position — this was
                //     the cause of the 716ms drift in the latest log)
                // Coordinates are virtual-desktop physical pixels; pass
                // straight through. The supervisor's setVisualTransform
                // dedup absorbs idempotent re-emits.
                if (previewDCompSurface_ != nullptr) {
                    QObject::connect(
                        previewDCompSurface_.get(),
                        &miacode::preview::dcomp::PreviewDCompSurface::popupGeometryApplied,
                        this,
                        [this](int xPx, int yPx, int wPx, int hPx) {
                            if (previewWorkerSupervisor_ == nullptr) {
                                return;
                            }
                            previewWorkerSupervisor_->setVisualTransform(xPx, yPx, wPx, hPx);
                        });
                }

                // We deliberately do NOT connect to QQuickWindow's xChanged,
                // yChanged, widthChanged, heightChanged, or screenChanged
                // signals here — and we do NOT register with
                // PreviewPopupHwndTracker. Those fire BEFORE Qt has
                // propagated DPR / layout changes through the
                // PreviewQuickSceneRoot QQuickItem's geometry. Calling
                // pushPopupGeometry at that moment produces stale physical-
                // pixel coords (old item width × new DPR, or new window
                // position × old item layout), and the worker popup lands
                // ~50 px away from where the in-process popup ends up
                // ~14-40 ms later. The in-process PreviewDCompSurface
                // avoids this by only listening to the QQuickItem's own
                // signals, which fire AFTER layout has settled.
                //
                // The wiring we do keep:
                //   - PreviewDCompSurface::popupGeometryApplied (above) —
                //     the cleanest mirror; fires for every in-process
                //     MoveWindow including internal re-applies
                //   - item geometry signals (set up in bindToTrackedItem
                //     during sceneGraphInitialized) — defence-in-depth
                //     for the case where the in-process surface hasn't
                //     attached yet
                //   - PreviewRuntime::frameStateChanged (audio-tick rate)
                //     — catch-up for occasional missed events
                //   - workerAttached signal — initial position on respawn

                // Push initial geometry on worker attach, AND retry once
                // the QML tree is up. The supervisor caches the last VT
                // and replays it on respawn, so a single successful push
                // is durable.
                QObject::connect(
                    previewWorkerSupervisor_.get(),
                    &miacode::preview::ipc::PreviewWorkerSupervisor::workerAttached,
                    this,
                    [pushPopupGeometry, bindToTrackedItem](quint64 /*popupHwnd*/) {
                        bindToTrackedItem();
                        pushPopupGeometry();
                    });

                // Runtime fallback — supervisor exhausted its respawn
                // budget (5 retries within 30 s healthy window). The
                // worker is gone for this session; bring up the
                // in-process surface so the editor has SOME chart
                // preview instead of a black region.
                QObject::connect(
                    previewWorkerSupervisor_.get(),
                    &miacode::preview::ipc::PreviewWorkerSupervisor::workerCrashLoopGivenUp,
                    this,
                    [this, window]() {
                        fallBackToInProcessPreviewSurface(
                            window,
                            QStringLiteral("worker_crash_loop_given_up"));
                    });
            } else if (workerOwnsChartRender) {
                // Startup fallback — supervisor failed to spawn
                // synchronously (executable missing, OS denied process
                // creation, env-flag misconfiguration). Without the
                // fallback the editor would have no chart preview at
                // all because the in-process surface was suppressed at
                // line 416 above. Recreate it now.
                fallBackToInProcessPreviewSurface(
                    window,
                    QStringLiteral("worker_spawn_failed"));
            }
        }
#endif

        if (surfaceHost_ != nullptr) {
            surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
        }
        appendQuickShellRuntimeLog(
            QStringLiteral("root_window_ready"),
            QString("visible=%1 width=%2 height=%3 title=%4 icon_null=%5")
                .arg(window->isVisible() ? 1 : 0)
                .arg(window->width())
                .arg(window->height())
                .arg(window->title())
                .arg(appIcon_.isNull() ? 1 : 0)
        );
        if (window->minimumWidth() > 0 && window->width() < window->minimumWidth()) {
            const int deltaWidth = window->minimumWidth() - window->width();
            window->setX(window->x() - deltaWidth / 2);
            window->setWidth(window->minimumWidth());
        }
        if (window->minimumHeight() > 0 && window->height() < window->minimumHeight()) {
            const int deltaHeight = window->minimumHeight() - window->height();
            window->setY(window->y() - deltaHeight / 2);
            window->setHeight(window->minimumHeight());
        }
        if (!appIcon_.isNull()) {
            window->setIcon(appIcon_);
        }
        QObject::connect(window, &QQuickWindow::visibleChanged, this, [this, window]() {
            appendQuickShellRuntimeLog(
                QStringLiteral("root_window_visible_changed"),
                QStringLiteral("visible=%1 shutdown_started=%2")
                    .arg(window->isVisible() ? 1 : 0)
                    .arg(acceptedRootWindowShutdownStarted_ ? 1 : 0)
            );
            if (!window->isVisible()) {
                beginAcceptedRootWindowShutdown(QStringLiteral("root_window_hidden"));
            }
        });
#ifdef Q_OS_WIN
        QObject::connect(window, &QQuickWindow::activeChanged, this, [this, window]() {
            logFocusEvent(
                QStringLiteral("root_window_active_changed"),
                window,
                nullptr,
                QStringLiteral("active=%1").arg(window->isActive() ? 1 : 0)
            );
        });
        QObject::connect(window, &QQuickWindow::xChanged, this, [this, window]() {
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
        });
        QObject::connect(window, &QQuickWindow::yChanged, this, [this, window]() {
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
        });
        QObject::connect(window, &QQuickWindow::widthChanged, this, [this, window]() {
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
        });
        QObject::connect(window, &QQuickWindow::heightChanged, this, [this, window]() {
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
        });
#endif
        QPointer<QQuickWindow> windowGuard(window);
        QTimer::singleShot(0, this, [this, windowGuard]() {
            if (windowGuard.isNull()) {
                return;
            }
            auto* window = windowGuard.data();
            if (surfaceHost_ != nullptr) {
                surfaceHost_->updateRootWindowFrameGeometry(window->frameGeometry());
            }
#ifdef Q_OS_WIN
            applySystemBackdropToQuickWindow(window);
            QObject::connect(window, &QQuickWindow::activeChanged, this, [window]() {
                applySystemBackdropToQuickWindow(window);
            });
            if (styleBridge_ != nullptr) {
                QObject::connect(styleBridge_.get(), &QuickShellStyleBridge::appearanceChanged, this, [window]() {
                    applySystemBackdropToQuickWindow(window);
                });
            }
            QObject::connect(qApp, &QGuiApplication::applicationStateChanged, this, [window](Qt::ApplicationState) {
                applySystemBackdropToQuickWindow(window);
            });
#endif
            appendQuickShellRuntimeLog(
                QStringLiteral("root_window_post_show"),
                QString("visible=%1 exposed=%2 active=%3 width=%4 height=%5")
                    .arg(window->isVisible() ? 1 : 0)
                    .arg(window->isExposed() ? 1 : 0)
                    .arg(window->isActive() ? 1 : 0)
                    .arg(window->width())
                    .arg(window->height())
            );
            if (surfaceHost_ != nullptr) {
                surfaceHost_->noteQuickShellUiReady();
            }
        });
    }
    if (!startupOpenTarget.trimmed().isEmpty() && backend_ != nullptr) {
        QTimer::singleShot(0, backend_.get(), [backend = QPointer<MainWindow>(backend_.get()), startupOpenTarget]() {
            if (backend.isNull()) {
                return;
            }
            backend->openStartupTarget(startupOpenTarget);
        });
    }
    return true;
}

bool QuickShellBootstrap::createInProcessPreviewSurface(QQuickWindow* window, const QString& reason)
{
    MC_OP("QuickShellBootstrap::createInProcessPreviewSurface");
    _mc_op_.note(QStringLiteral("reason=%1").arg(reason));
    if (window == nullptr) {
        _mc_op_.fail(QStringLiteral("null window"));
        return false;
    }
    // Idempotent — caller may invoke from multiple paths (normal
    // startup, spawn-failure fallback, crash-loop-given-up fallback).
    // Once attached, further calls are a no-op so we don't create a
    // second surface or re-wire signals already in place.
    if (previewDCompSurface_ != nullptr) {
        appendQuickShellRuntimeLog(
            QStringLiteral("dcomp_surface_attach_skipped"),
            QStringLiteral("reason=already_attached requested=%1").arg(reason));
        return true;
    }
    previewDCompSurface_ =
        std::make_unique<miacode::preview::dcomp::PreviewDCompSurface>(this);
    previewDCompSurface_->attachToWindow(window);
    // Phase 3.2: connect the surface to the PreviewRuntime so the
    // render thread can read playhead state. controller_ exposes
    // it as a generic QObject* (the QML-property accessor); cast
    // back to PreviewRuntime for the typed setRuntime call.
    if (controller_ != nullptr) {
        if (auto* runtime = qobject_cast<PreviewRuntime*>(
                controller_->previewRuntime()); runtime != nullptr) {
            previewDCompSurface_->setRuntime(runtime);
        }
    }
    // Phase 4c — wire the PreviewStageMediaHost into the surface so
    // StageBackgroundSource can pull the current QVideoFrame
    // (delivered by the host's QMediaPlayer + QVideoSink) on every
    // snapshot build. The host is created lazily inside MainWindow on
    // first chart-load, so we connect the signal AND attach the host
    // immediately if it already exists (race-free in either direction).
    if (backend_ != nullptr) {
        QObject::connect(
            backend_.get(),
            &MainWindow::previewStageMediaHostInitialized,
            this,
            [this](PreviewStageMediaHost* host) {
                if (previewDCompSurface_ != nullptr) {
                    previewDCompSurface_->setStageMediaHost(host);
                }
            });
        if (auto* existingHost = backend_->previewStageMediaHost();
            existingHost != nullptr) {
            previewDCompSurface_->setStageMediaHost(existingHost);
        }
    }
    // Issue #3 fix — propagate the user's "Preview Canvas Frame
    // Rate" option (60 / 120 / Display) to the new pipeline.
    // MainWindow emits previewCanvasPresentSyncIntervalChanged
    // whenever the user picks a different option in Render
    // Settings; we forward to the surface, which forwards to
    // the renderer's setPresentSyncInterval. The legacy QSG
    // qtPreviewTimer was already wired separately inside
    // MainWindow itself; this connection brings the new
    // pipeline to parity.
    if (backend_ != nullptr) {
        QObject::connect(
            backend_.get(),
            &MainWindow::previewCanvasPresentSyncIntervalChanged,
            this,
            [this](unsigned int syncInterval) {
                if (previewDCompSurface_ != nullptr) {
                    previewDCompSurface_->setRenderPresentSyncInterval(syncInterval);
                }
            });
        // Push the initial value once at attach time. The exact
        // SyncInterval depends on the display refresh rate, which
        // MainWindow knows; we replicate just enough of the
        // logic here to seed the renderer correctly. Defaults
        // are conservative (1 = display refresh) when we can't
        // determine the display rate at this point.
        unsigned int initialSyncInterval = 1U;
        if (auto* screen = QGuiApplication::primaryScreen();
            screen != nullptr && screen->refreshRate() > 1.0) {
            const double displayHz = screen->refreshRate();
            double targetHz = displayHz;
            switch (backend_->currentPreviewCanvasFrameRateMode()) {
            case MainWindow::PreviewCanvasFrameRateMode::Fps30:
                targetHz = 30.0;
                break;
            case MainWindow::PreviewCanvasFrameRateMode::Fps60:
                targetHz = 60.0;
                break;
            case MainWindow::PreviewCanvasFrameRateMode::Fps120:
                targetHz = 120.0;
                break;
            case MainWindow::PreviewCanvasFrameRateMode::DisplayRefresh:
            default:
                targetHz = displayHz;
                break;
            }
            const double interval = displayHz / qMax(1.0, targetHz);
            initialSyncInterval = static_cast<unsigned int>(
                qBound<double>(1.0, qRound(interval), 4.0));
        }
        previewDCompSurface_->setRenderPresentSyncInterval(initialSyncInterval);
    }
    appendQuickShellRuntimeLog(
        QStringLiteral("dcomp_surface_attached"),
        QStringLiteral("phase=3.2 reason=%1").arg(reason));
    return true;
}

void QuickShellBootstrap::fallBackToInProcessPreviewSurface(QQuickWindow* window, const QString& reason)
{
    MC_OP("QuickShellBootstrap::fallBackToInProcessPreviewSurface");
    _mc_op_.note(QStringLiteral("reason=%1").arg(reason));
    if (window == nullptr) {
        _mc_op_.fail(QStringLiteral("null window"));
        return;
    }
    // Tear down the supervisor first so it doesn't keep replaying
    // its respawn loop or hold any stale signal connections after we
    // commit to in-process rendering. Qt's parent-child auto-disconnect
    // covers everything we wired to the supervisor (workerAttached,
    // workerCrashLoopGivenUp, workerDeviceRemoved, etc.), and the
    // unique_ptr reset destroys the QProcess + ring buffer.
    if (previewWorkerSupervisor_ != nullptr) {
        shutdownPreviewWorkerSupervisor(QStringLiteral("fallback:%1").arg(reason));
        appendQuickShellRuntimeLog(
            QStringLiteral("preview_worker_supervisor_torn_down"),
            QStringLiteral("reason=%1").arg(reason));
    }
    // Surface up. Helper is idempotent — if the surface already exists
    // (e.g. crash-loop-given-up arriving while a previous fallback
    // already brought one up) the call short-circuits with a log line.
    createInProcessPreviewSurface(window, reason);
}

bool QuickShellBootstrap::eventFilter(QObject* watched, QEvent* event)
{
    if (controller_ == nullptr || event == nullptr) {
        return QObject::eventFilter(watched, event);
    }

    if (watched == rootWindow_ && event->type() == QEvent::Close) {
        if (rootWindowCloseRelayScheduled_
            || rootWindowCloseRelayActive_
            || UiDialogs::hasVisibleBlockingModalDialog()) {
            if (!rootWindowCloseRelayScheduled_
                && !rootWindowCloseRelayActive_
                && UiDialogs::hasVisibleBlockingModalDialog()) {
                scheduleRootWindowCloseRelay(QStringLiteral("qevent_close"));
            }
            event->ignore();
            return true;
        }
    }

    if (shouldTraceFocusObject(watched)) {
        switch (event->type()) {
        case QEvent::FocusIn:
        case QEvent::FocusOut:
        case QEvent::WindowActivate:
        case QEvent::WindowDeactivate:
        case QEvent::ActivationChange:
            logFocusEvent(QStringLiteral("event_filter"), watched, event);
            break;
        default:
            break;
        }
    }

    if (UiDialogs::hasVisibleProtectedPreviewDialog()
        && (event->type() == QEvent::ShortcutOverride
            || event->type() == QEvent::KeyPress
            || event->type() == QEvent::KeyRelease)) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (UiDialogs::isPreviewShortcutEvent(keyEvent)) {
            return QObject::eventFilter(watched, event);
        }
    }

    if (!controller_->previewFullscreen() && event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        const QPoint globalPos = mouseEvent != nullptr ? mouseEvent->globalPosition().toPoint() : QPoint();
        previewSeekArmed_ = previewSeekHotRectContainsGlobalPoint(globalPos);
    }

    if (!controller_->previewFullscreen()) {
        if ((event->type() == QEvent::ShortcutOverride
                || event->type() == QEvent::KeyPress
                || event->type() == QEvent::KeyRelease)
            && previewSeekArmed_) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            const bool isArrow =
                keyEvent != nullptr
                && keyEvent->modifiers() == Qt::NoModifier
                && (keyEvent->key() == Qt::Key_Left || keyEvent->key() == Qt::Key_Right);
            if (isArrow) {
                if (event->type() == QEvent::ShortcutOverride) {
                    keyEvent->accept();
                    return true;
                }
                const int direction = keyEvent->key() == Qt::Key_Left ? -1 : 1;
                if (event->type() == QEvent::KeyPress) {
                    keyEvent->accept();
                    if (keyEvent->isAutoRepeat()) {
                        return true;
                    }
                    controller_->beginPreviewHeldSeek(direction, keyEvent->key());
                    controller_->stepPreviewBySeconds(
                        direction * controller_->previewSeekSingleStepSeconds(),
                        true
                    );
                    return true;
                }
                if (event->type() == QEvent::KeyRelease) {
                    keyEvent->accept();
                    if (!keyEvent->isAutoRepeat()) {
                        controller_->stopPreviewHeldSeek(keyEvent->key());
                    }
                    return true;
                }
            }
        }
        if ((event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
            auto* keyEvent = static_cast<QKeyEvent*>(event);
            if (keyEvent != nullptr && !keyEvent->isAutoRepeat()) {
                const QKeySequence sequence(keyEvent->modifiers() | keyEvent->key());
                if (controller_->hasShortcut(sequence)) {
                    if (event->type() == QEvent::ShortcutOverride) {
                        keyEvent->accept();
                        return true;
                    }
                    if (controller_->triggerShortcut(sequence)) {
                        keyEvent->accept();
                        return true;
                    }
                }
            }
        }
        return QObject::eventFilter(watched, event);
    }

    if (event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent != nullptr
            && keyEvent->modifiers() == Qt::NoModifier
            && (keyEvent->key() == Qt::Key_Escape || keyEvent->key() == Qt::Key_Space)) {
            keyEvent->accept();
            return true;
        }
    }

    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent != nullptr && !keyEvent->isAutoRepeat() && keyEvent->modifiers() == Qt::NoModifier) {
            if (keyEvent->key() == Qt::Key_Escape) {
                appendQuickShellRuntimeLog(QStringLiteral("global_fullscreen_key"), QStringLiteral("key=Esc"));
                controller_->setPreviewFullscreen(false);
                keyEvent->accept();
                return true;
            }
            if (keyEvent->key() == Qt::Key_Space) {
                appendQuickShellRuntimeLog(QStringLiteral("global_fullscreen_key"), QStringLiteral("key=Space"));
                controller_->togglePreviewPlayback();
                keyEvent->accept();
                return true;
            }
        }
    }

    return QObject::eventFilter(watched, event);
}

#ifdef Q_OS_WIN
bool QuickShellNativeCloseEventFilter::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    return bootstrap_ != nullptr ? bootstrap_->handleNativeCloseEvent(eventType, message, result) : false;
}

bool QuickShellBootstrap::handleNativeCloseEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    if (message == nullptr
        || (eventType != "windows_generic_MSG" && eventType != "windows_dispatcher_MSG")) {
        return false;
    }

    auto* msg = static_cast<MSG*>(message);
    if (msg == nullptr || msg->hwnd == nullptr) {
        return false;
    }

    // Phase 4e — owner-followed popup tracking. WM_WINDOWPOSCHANGED is
    // the canonical "your window moved/resized/restored" hook, fired
    // once per DWM compositor tick during animations. Forwarding it to
    // the popup tracker lets both DComp popups (chart + timeline)
    // commit a batched DeferWindowPos in the same tick the editor's
    // own frame is rendered, eliminating the 1-3 frame inter-popup
    // shear during drag/maximize/restore. WebView2 Visual hosting and
    // Chromium Aura use this exact pattern — there is no OS-level
    // "follow my owner" auto-sync; the host has to drive it.
    //
    // Returning false unconditionally here — we observe but do not
    // consume the message; DefWindowProc still gets to do its own
    // post-processing.
    if (msg->message == WM_WINDOWPOSCHANGED) {
        // Throttled diagnostic — log at most once per second to confirm
        // the hook receives WM_WINDOWPOSCHANGED at all. Includes both
        // the message HWND and our recorded root HWND so a mismatch is
        // visible. force=true bypasses any channel filter.
        static QElapsedTimer s_lastSeenTimer;
        static bool s_seenStarted = false;
        if (!s_seenStarted) { s_lastSeenTimer.start(); s_seenStarted = true; }
        const bool isRoot = rootWindowNativeHwnd_ != 0
            && msg->hwnd == reinterpret_cast<HWND>(rootWindowNativeHwnd_);
        if (s_lastSeenTimer.elapsed() >= 1000) {
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Runtime,
                QStringLiteral("quick_shell"),
                QStringLiteral(
                    "action=wmpos_seen msg_hwnd=0x%1 root_hwnd=0x%2 is_root=%3")
                    .arg(reinterpret_cast<quintptr>(msg->hwnd), 0, 16)
                    .arg(rootWindowNativeHwnd_, 0, 16)
                    .arg(isRoot ? 1 : 0),
                /*force=*/true);
            s_lastSeenTimer.restart();
        }
        if (isRoot) {
            miacode::preview::dcomp::PreviewPopupHwndTracker::notifyOwnerWindowPosChanged();
        }
    }

    const bool isCloseMessage = msg->message == WM_CLOSE;
    const bool isCloseSysCommand = msg->message == WM_SYSCOMMAND && ((msg->wParam & 0xFFF0) == SC_CLOSE);
    if (!isCloseMessage && !isCloseSysCommand) {
        return false;
    }

    if (rootWindow_.isNull() || rootWindowNativeHwnd_ == 0) {
        return false;
    }

    const HWND rootHwnd = reinterpret_cast<HWND>(rootWindowNativeHwnd_);
    if (msg->hwnd != rootHwnd) {
        return false;
    }

    if (!rootWindowCloseRelayScheduled_
        && !rootWindowCloseRelayActive_
        && !UiDialogs::hasVisibleBlockingModalDialog()) {
        return false;
    }

    if (!rootWindowCloseRelayScheduled_
        && !rootWindowCloseRelayActive_
        && UiDialogs::hasVisibleBlockingModalDialog()) {
        scheduleRootWindowCloseRelay(
            isCloseSysCommand ? QStringLiteral("native_syscommand_close") : QStringLiteral("native_wm_close")
        );
    }
    if (result != nullptr) {
        *result = 0;
    }
    return true;
}
#endif

void QuickShellBootstrap::scheduleRootWindowCloseRelay(const QString& source)
{
    if (controller_ == nullptr || rootWindow_.isNull() || rootWindowCloseRelayScheduled_ || rootWindowCloseRelayActive_) {
        return;
    }
    rootWindowCloseRelayScheduled_ = true;
    appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_schedule"), QStringLiteral("source=%1").arg(source));
    QTimer::singleShot(0, this, [this]() {
        processRootWindowCloseRelay();
    });
}

void QuickShellBootstrap::processRootWindowCloseRelay()
{
    QElapsedTimer relayTimer;
    relayTimer.start();
    rootWindowCloseRelayScheduled_ = false;
    if (controller_ == nullptr || rootWindow_.isNull() || rootWindowCloseRelayActive_) {
        return;
    }

    rootWindowCloseRelayActive_ = true;
    appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_begin"));
    QElapsedTimer closeDialogsTimer;
    closeDialogsTimer.start();
    const int closedDialogCount = UiDialogs::closeVisibleBlockingModalDialogs();
    const bool dialogsRemainVisible = UiDialogs::hasVisibleBlockingModalDialog();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("close_blocking_modal_dialogs"),
        closeDialogsTimer.elapsed(),
        QStringLiteral("closed_count=%1 dialogs_remaining=%2")
            .arg(closedDialogCount)
            .arg(dialogsRemainVisible ? 1 : 0)
    );
    if (dialogsRemainVisible) {
        appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_retry"));
        rootWindowCloseRelayActive_ = false;
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("process_root_window_close_relay"),
            relayTimer.elapsed(),
            QStringLiteral("result=retry_dialogs_visible")
        );
        scheduleRootWindowCloseRelay(QStringLiteral("dialogs_still_visible"));
        return;
    }

    QElapsedTimer confirmCloseTimer;
    confirmCloseTimer.start();
    const bool confirmed = controller_->confirmClose();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("confirm_close"),
        confirmCloseTimer.elapsed(),
        QStringLiteral("confirmed=%1").arg(confirmed ? 1 : 0)
    );
    if (!confirmed) {
        appendQuickShellRuntimeLog(QStringLiteral("root_close_relay_cancelled"));
        rootWindowCloseRelayActive_ = false;
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("process_root_window_close_relay"),
            relayTimer.elapsed(),
            QStringLiteral("result=cancelled")
        );
        return;
    }

    rootWindowCloseRelayActive_ = false;
    controller_->markNextCloseConfirmedExternally();
    QElapsedTimer rootCloseTimer;
    rootCloseTimer.start();
    const bool rootWindowClosed = rootWindow_->close();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("root_window_close"),
        rootCloseTimer.elapsed(),
        QStringLiteral("accepted=%1").arg(rootWindowClosed ? 1 : 0)
    );
    if (!rootWindowClosed) {
        controller_->clearPendingExternalCloseConfirmation();
    } else if (backend_ != nullptr) {
        QElapsedTimer previewShutdownTimer;
        previewShutdownTimer.start();
        backend_->preparePreviewForShutdown();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("prepare_preview_for_shutdown"),
            previewShutdownTimer.elapsed()
        );
        beginAcceptedRootWindowShutdown(QStringLiteral("root_close_relay"));
    }
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("process_root_window_close_relay"),
        relayTimer.elapsed(),
        QStringLiteral("result=%1").arg(rootWindowClosed ? QStringLiteral("forwarded") : QStringLiteral("close_rejected"))
    );
}

void QuickShellBootstrap::beginAcceptedRootWindowShutdown(const QString& source)
{
    if (acceptedRootWindowShutdownStarted_) {
        appendQuickShellRuntimeLog(
            QStringLiteral("accepted_close_shutdown_skip"),
            QStringLiteral("source=%1 started=1").arg(source)
        );
        return;
    }
    acceptedRootWindowShutdownStarted_ = true;

    QElapsedTimer totalTimer;
    totalTimer.start();
    appendQuickShellRuntimeLog(QStringLiteral("accepted_close_shutdown_begin"), QStringLiteral("source=%1").arg(source));

    if (qApp != nullptr) {
        qApp->setQuitOnLastWindowClosed(false);
    }

#ifdef Q_OS_WIN
    if (QCoreApplication* app = QCoreApplication::instance(); app != nullptr) {
        if (nativeCloseEventFilter_ != nullptr) {
            app->removeNativeEventFilter(nativeCloseEventFilter_.get());
            nativeCloseEventFilter_.reset();
        }
    }
    rootWindowNativeHwnd_ = 0;
#endif

    if (!rootWindow_.isNull()) {
        rootWindow_->removeEventFilter(this);
    }
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }

    if (backend_ != nullptr) {
        QElapsedTimer previewShutdownTimer;
        previewShutdownTimer.start();
        backend_->preparePreviewForShutdown();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("accepted_close_prepare_preview_for_shutdown"),
            previewShutdownTimer.elapsed()
        );
    }
    shutdownPreviewWorkerSupervisor(QStringLiteral("accepted_close:%1").arg(source));
    if (backend_ != nullptr) {
        QElapsedTimer backendCloseTimer;
        backendCloseTimer.start();
        backend_->close();
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("accepted_close_backend_close"),
            backendCloseTimer.elapsed()
        );
    }

    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("accepted_close_shutdown_prepare_destroy"),
        totalTimer.elapsed(),
        QStringLiteral("source=%1").arg(source)
    );
    appendQuickShellRuntimeLog(
        QStringLiteral("accepted_close_shutdown_prepare_destroy"),
        QStringLiteral("source=%1 elapsed_ms=%2").arg(source).arg(totalTimer.elapsed())
    );
    scheduleAcceptedRootWindowDestroyAndQuit(source);
}

void QuickShellBootstrap::shutdownPreviewWorkerSupervisor(const QString& source)
{
    if (previewWorkerSupervisor_ == nullptr) {
        return;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("action=shutdown_preview_worker_enter source=%1").arg(source),
        true
    );
    QElapsedTimer timer;
    timer.start();
    previewWorkerSupervisor_->shutdown();
    previewWorkerSupervisor_.reset();
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("shutdown_preview_worker"),
        timer.elapsed(),
        QStringLiteral("source=%1").arg(source),
        /*force=*/true
    );
}

void QuickShellBootstrap::scheduleAcceptedRootWindowDestroyAndQuit(const QString& source)
{
    if (acceptedRootWindowDestroyScheduled_ || acceptedRootWindowDestroyStarted_) {
        appendQuickShellRuntimeLog(
            QStringLiteral("accepted_close_destroy_skip"),
            QStringLiteral("source=%1 scheduled=%2 started=%3")
                .arg(source)
                .arg(acceptedRootWindowDestroyScheduled_ ? 1 : 0)
                .arg(acceptedRootWindowDestroyStarted_ ? 1 : 0)
        );
        return;
    }

    acceptedRootWindowDestroyScheduled_ = true;
    appendQuickShellRuntimeLog(QStringLiteral("accepted_close_destroy_schedule"), QStringLiteral("source=%1").arg(source));
    QTimer::singleShot(0, this, [this, source]() {
        destroyAcceptedRootWindowResourcesAndQuit(source);
    });
}

void QuickShellBootstrap::destroyAcceptedRootWindowResourcesAndQuit(const QString& source)
{
    if (acceptedRootWindowDestroyStarted_) {
        return;
    }
    acceptedRootWindowDestroyScheduled_ = false;
    acceptedRootWindowDestroyStarted_ = true;

    QElapsedTimer totalTimer;
    totalTimer.start();
    appendQuickShellRuntimeLog(QStringLiteral("accepted_close_destroy_begin"), QStringLiteral("source=%1").arg(source));

    if (!rootWindow_.isNull()) {
        rootWindow_->removeEventFilter(this);
    }
    if (qApp != nullptr) {
        qApp->removeEventFilter(this);
    }

    const auto logResetTiming = [](const QString& step, auto& pointer) {
        if (!pointer) {
            return;
        }
        // Bracket-log around the destructor so that if .reset() crashes, we can tell which
        // pointer was being torn down from the last logged "enter" line.
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            QStringLiteral("action=%1_enter").arg(step),
            true
        );
        QElapsedTimer timer;
        timer.start();
        pointer.reset();
        // beta20-fix2 — force-log the elapsed-time line too. Without
        // --debug the prior `appendTimingLine` (force=false default)
        // would silently drop, leaving the runtime log with `_enter`
        // lines but no `_exit`/elapsed lines, making it impossible to
        // tell from a non-debug crash dump whether a particular
        // destructor finished or hung. Force-true matches the `_enter`
        // line's logging level so brackets are symmetric in any mode.
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("close_timing/quick_shell"),
            step,
            timer.elapsed(),
            QString(),
            /*force=*/true
        );
    };

    // Phase 8b — destroy the chart-side PreviewDCompSurface FIRST. It
    // owns the D3D11 render thread plus a `Qt::QueuedConnection` from
    // `PreviewRuntime::frameStateChanged` and a queued connection on
    // its renderer's `presented` signal. PreviewRuntime is parented to
    // MainWindow and dies during the QObject children-walk inside
    // `backend_.reset()`. If the render thread is still alive at that
    // point, an in-flight queued emit can race against the receiver's
    // disconnect-on-destroy and deadlock both threads on the per-
    // receiver Qt signal-slot lock pool — exactly the symptom: the
    // last log line is `accepted_close_destroy_backend_enter` and the
    // process never exits.
    //
    // ~PreviewDCompSurface's existing teardownCore() does the right
    // thing IF called early enough: stops the render thread, joins it,
    // then disconnects the queued connections. The fix is just to
    // sequence it before MainWindow goes away. Mirror of the same
    // ordering discipline applied for the timeline render view in
    // commits 8ec8c18 / 55107cf / 7ebab94.
    logResetTiming(QStringLiteral("accepted_close_destroy_preview_dcomp_surface"),
                   previewDCompSurface_);
    // Out-of-process worker — graceful shutdown sends "shutdown" via stdin
    // and waits ~500 ms for the worker to exit. Same ordering rationale
    // as the in-process surface: tear it down before MainWindow so any
    // in-flight queued signals don't reach a destroyed receiver.
    shutdownPreviewWorkerSupervisor(QStringLiteral("accepted_close_destroy:%1").arg(source));
    logResetTiming(QStringLiteral("accepted_close_destroy_engine"), engine_);
    rootWindow_ = nullptr;
#ifdef Q_OS_WIN
    rootWindowNativeHwnd_ = 0;
#endif
    logResetTiming(QStringLiteral("accepted_close_destroy_style_bridge"), styleBridge_);
    logResetTiming(QStringLiteral("accepted_close_destroy_controller"), controller_);
    logResetTiming(QStringLiteral("accepted_close_destroy_surface_host"), surfaceHost_);
    logResetTiming(QStringLiteral("accepted_close_destroy_backend"), backend_);

    // beta20-fix2 — force-log so non-debug crash dumps can tell whether
    // we got to the very end of the close sequence or hung partway. If
    // this line is absent in a future log but `accepted_close_destroy_backend_enter`
    // is present, the crash is in `~MainWindow()` (i.e. backend.reset()).
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("close_timing/quick_shell"),
        QStringLiteral("accepted_close_destroy_and_quit"),
        totalTimer.elapsed(),
        QStringLiteral("source=%1").arg(source),
        /*force=*/true
    );
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("quick_shell"),
        QStringLiteral("action=accepted_close_destroy_quit_request source=%1 elapsed_ms=%2")
            .arg(source).arg(totalTimer.elapsed()),
        /*force=*/true
    );
    QCoreApplication::quit();
}

bool QuickShellBootstrap::previewSeekHotRectContainsGlobalPoint(const QPoint& globalPoint) const
{
    if (engine_ == nullptr || engine_->rootObjects().isEmpty()) {
        return false;
    }
    auto* window = qobject_cast<QQuickWindow*>(engine_->rootObjects().constFirst());
    if (window == nullptr || !window->isVisible()) {
        return false;
    }
    const QVariant rectVariant = window->property("previewSeekHotRect");
    if (!rectVariant.isValid()) {
        return false;
    }
    const QRectF hotRect = rectVariant.toRectF();
    if (!hotRect.isValid() || hotRect.isEmpty()) {
        return false;
    }
    const QPoint localPos = window->mapFromGlobal(globalPoint);
    return hotRect.contains(QPointF(localPos));
}

QuickShellController* QuickShellBootstrap::controller() const
{
    return controller_.get();
}

QuickShellStyleBridge* QuickShellBootstrap::styleBridge() const
{
    return styleBridge_.get();
}

bool QuickShellBootstrap::shouldTraceFocusObject(QObject* watched) const
{
    if (watched == nullptr) {
        return false;
    }

    if (auto* widget = qobject_cast<QWidget*>(watched); widget != nullptr) {
        const auto matchesOrDescends = [widget](QWidget* root) {
            return root != nullptr && (widget == root || root->isAncestorOf(widget));
        };
        if (surfaceHost_ != nullptr
            && (matchesOrDescends(surfaceHost_->topChromeSurfaceWidget())
                || matchesOrDescends(surfaceHost_->sidebarSurfaceWidget())
                || matchesOrDescends(surfaceHost_->workspaceSurfaceWidget())
                || matchesOrDescends(surfaceHost_->statusSurfaceWidget()))) {
            return true;
        }
        return qobject_cast<QTextEdit*>(widget) != nullptr
            || qobject_cast<QLineEdit*>(widget) != nullptr;
    }

    if (auto* window = qobject_cast<QWindow*>(watched); window != nullptr) {
        if (engine_ != nullptr && !engine_->rootObjects().isEmpty() && engine_->rootObjects().constFirst() == window) {
            return true;
        }
        if (qApp != nullptr && qApp->focusWindow() == window) {
            return true;
        }
    }

    return false;
}

QString QuickShellBootstrap::describeFocusObject(QObject* object) const
{
    if (object == nullptr) {
        return QStringLiteral("null");
    }

    QStringList parts;
    parts.append(QStringLiteral("class=%1").arg(object->metaObject() != nullptr ? object->metaObject()->className() : "unknown"));
    parts.append(QStringLiteral("ptr=%1").arg(pointerHex(object)));
    if (!object->objectName().isEmpty()) {
        parts.append(QStringLiteral("name=%1").arg(object->objectName()));
    }

    if (auto* widget = qobject_cast<QWidget*>(object); widget != nullptr) {
        parts.append(QStringLiteral("widget=1"));
        parts.append(QStringLiteral("visible=%1").arg(widget->isVisible() ? 1 : 0));
        parts.append(QStringLiteral("enabled=%1").arg(widget->isEnabled() ? 1 : 0));
        parts.append(QStringLiteral("focus=%1").arg(widget->hasFocus() ? 1 : 0));
        parts.append(QStringLiteral("active_win=%1").arg(widget->isActiveWindow() ? 1 : 0));
        parts.append(QStringLiteral("policy=%1").arg(focusPolicyName(widget->focusPolicy())));
        if (QWidget* parent = widget->parentWidget(); parent != nullptr) {
            parts.append(QStringLiteral("parent=%1").arg(pointerHex(parent)));
        }
        if (surfaceHost_ != nullptr) {
            const auto matchesOrDescends = [widget](QWidget* root) {
                return root != nullptr && (widget == root || root->isAncestorOf(widget));
            };
            parts.append(QStringLiteral("in_top=%1").arg(matchesOrDescends(surfaceHost_->topChromeSurfaceWidget()) ? 1 : 0));
            parts.append(QStringLiteral("in_sidebar=%1").arg(matchesOrDescends(surfaceHost_->sidebarSurfaceWidget()) ? 1 : 0));
            parts.append(QStringLiteral("in_workspace=%1").arg(matchesOrDescends(surfaceHost_->workspaceSurfaceWidget()) ? 1 : 0));
            parts.append(QStringLiteral("in_status=%1").arg(matchesOrDescends(surfaceHost_->statusSurfaceWidget()) ? 1 : 0));
        }
        if (auto* textEdit = qobject_cast<QTextEdit*>(widget); textEdit != nullptr) {
            const QTextCursor cursor = textEdit->textCursor();
            parts.append(QStringLiteral("cursor_anchor=%1").arg(cursor.anchor()));
            parts.append(QStringLiteral("cursor_pos=%1").arg(cursor.position()));
        }
        if (auto* lineEdit = qobject_cast<QLineEdit*>(widget); lineEdit != nullptr) {
            parts.append(QStringLiteral("text_len=%1").arg(lineEdit->text().size()));
        }
    }

    if (auto* window = qobject_cast<QWindow*>(object); window != nullptr) {
        parts.append(QStringLiteral("window=1"));
        parts.append(QStringLiteral("visible=%1").arg(window->isVisible() ? 1 : 0));
        parts.append(QStringLiteral("active=%1").arg(window->isActive() ? 1 : 0));
        parts.append(QStringLiteral("focus_obj=%1").arg(pointerHex(window->focusObject())));
    }

    return parts.join(' ');
}

QString QuickShellBootstrap::focusReasonName(Qt::FocusReason reason) const
{
    switch (reason) {
    case Qt::MouseFocusReason:
        return QStringLiteral("MouseFocusReason");
    case Qt::TabFocusReason:
        return QStringLiteral("TabFocusReason");
    case Qt::BacktabFocusReason:
        return QStringLiteral("BacktabFocusReason");
    case Qt::ActiveWindowFocusReason:
        return QStringLiteral("ActiveWindowFocusReason");
    case Qt::PopupFocusReason:
        return QStringLiteral("PopupFocusReason");
    case Qt::ShortcutFocusReason:
        return QStringLiteral("ShortcutFocusReason");
    case Qt::MenuBarFocusReason:
        return QStringLiteral("MenuBarFocusReason");
    case Qt::OtherFocusReason:
        return QStringLiteral("OtherFocusReason");
    case Qt::NoFocusReason:
        return QStringLiteral("NoFocusReason");
    }
    return QStringLiteral("FocusReason(%1)").arg(static_cast<int>(reason));
}

void QuickShellBootstrap::logFocusEvent(const QString& action, QObject* watched, QEvent* event, const QString& detail) const
{
    if (!miacode::debug_options::runtimeDebugOutputEnabled()) {
        return;
    }

    QString payload;
    if (event != nullptr) {
        payload += QStringLiteral("event_type=%1").arg(static_cast<int>(event->type()));
        if (event->type() == QEvent::FocusIn || event->type() == QEvent::FocusOut) {
            auto* focusEvent = static_cast<QFocusEvent*>(event);
            payload += QStringLiteral(" focus_reason=%1").arg(focusReasonName(focusEvent != nullptr ? focusEvent->reason() : Qt::NoFocusReason));
        }
        payload += QStringLiteral(" spontaneous=%1").arg(event->spontaneous() ? 1 : 0);
    }
    payload += QStringLiteral(" watched={%1}").arg(describeFocusObject(watched));
    payload += QStringLiteral(" app_focus_widget={%1}").arg(describeFocusObject(qobject_cast<QObject*>(QApplication::focusWidget())));
    payload += QStringLiteral(" app_focus_window={%1}").arg(describeFocusObject(qobject_cast<QObject*>(qApp != nullptr ? qApp->focusWindow() : nullptr)));
    if (!detail.trimmed().isEmpty()) {
        payload += QStringLiteral(" detail=%1").arg(detail.trimmed());
    }
    appendQuickShellFocusLog(action, payload);
}
