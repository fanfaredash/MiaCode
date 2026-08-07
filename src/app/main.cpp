#include "AppVersion.h"
#include "quick_shell/QuickShellBootstrap.h"
#include "qml_ui/QmlUiBootstrap.h"
#include "mainwindow/MainWindow.h"
#include "tools/video_export/VideoExportSnapshot.h"
#include "UiText.h"
#include "UiTheme.h"
#include "UiNativeWindowTheme.h"
#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/UiHangWatchdog.h"
#include "common/DebugOptions.h"
#include "common/WaveformCache.h"
#include "SimaiNativeParser.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include <QTimer>
#include <QStringList>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QDir>
#include <QRegularExpression>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QSGRendererInterface>

#include <cmath>
#include <cstdio>
#include <exception>
#include <new>

#ifdef Q_OS_WIN
#include <windows.h>
#include <timeapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <psapi.h>
#include <cstdio>
#include <cstring>
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "version.lib")
#endif

#include "MainEntrypoints.h"

namespace {

bool wantsCliVideoExport(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--export-video"));
}

bool wantsCliVideoExportWorker(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--export-video-worker"));
}

bool wantsQuickShellBeta(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--quick-shell-beta"));
}

enum class UiSkin {
    QmlUiV2,
    QuickShellV1,
};

// Default: v2. Opt into QuickShell with --ui=v1 or MIACODE_UI_SKIN=v1.
UiSkin resolveUiSkin(const QStringList& arguments)
{
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index).trimmed();
        if (argument == QStringLiteral("--ui=v1")) {
            return UiSkin::QuickShellV1;
        }
        if (argument.startsWith(QStringLiteral("--ui="))
            && argument.mid(5).trimmed().compare(QStringLiteral("v1"), Qt::CaseInsensitive) == 0) {
            return UiSkin::QuickShellV1;
        }
    }
    if (qEnvironmentVariable("MIACODE_UI_SKIN").trimmed().compare(
            QStringLiteral("v1"), Qt::CaseInsensitive) == 0) {
        return UiSkin::QuickShellV1;
    }
    return UiSkin::QmlUiV2;
}

// Force-show the first-run welcome / initial-config dialog even when
// preferences already exist. Handy for debugging the onboarding flow.
bool wantsWelcomeDialog(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--welcome"));
}

QString startupOpenTargetFromArguments(const QStringList& arguments)
{
    for (int index = 1; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index).trimmed();
        if (argument.isEmpty()) {
            continue;
        }
        if (argument == QStringLiteral("--")) {
            if (index + 1 < arguments.size()) {
                return arguments.at(index + 1);
            }
            return QString();
        }
        if (argument == QStringLiteral("--rhi")) {
            ++index;
            continue;
        }
        if (argument.startsWith('-')) {
            continue;
        }
        return arguments.at(index);
    }
    return QString();
}

void appendAppShutdownRuntimeLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown"),
        text
    );
}

QString summarizeTopLevelWidgets()
{
    const auto widgets = QApplication::topLevelWidgets();
    QStringList items;
    items.reserve(widgets.size());
    for (QWidget* widget : widgets) {
        if (widget == nullptr) {
            continue;
        }
        items.append(
            QStringLiteral("%1(vis=%2 hidden=%3 active=%4 title=%5)")
                .arg(widget->metaObject() != nullptr ? widget->metaObject()->className() : QStringLiteral("unknown"))
                .arg(widget->isVisible() ? 1 : 0)
                .arg(widget->isHidden() ? 1 : 0)
                .arg(widget->isActiveWindow() ? 1 : 0)
                .arg(widget->windowTitle().trimmed().isEmpty() ? QStringLiteral("(empty)") : widget->windowTitle().trimmed())
        );
    }
    return items.join(QStringLiteral("; "));
}

QString summarizeTopLevelWindows()
{
    const auto windows = QGuiApplication::topLevelWindows();
    QStringList items;
    items.reserve(windows.size());
    for (QWindow* window : windows) {
        if (window == nullptr) {
            continue;
        }
        items.append(
            QStringLiteral("%1(vis=%2 title=%3)")
                .arg(window->metaObject() != nullptr ? window->metaObject()->className() : QStringLiteral("unknown"))
                .arg(window->isVisible() ? 1 : 0)
                .arg(window->title().trimmed().isEmpty() ? QStringLiteral("(empty)") : window->title().trimmed())
        );
    }
    return items.join(QStringLiteral("; "));
}

}  // namespace

using namespace miacode::app::entry;

int main(int argc, char* argv[])
{
    // Phase 6 — startup beacon. Heap-free, pure-Win32. Lands a tiny
    // miacode_startup_beacon_<pid>.txt next to the eventual op-chain
    // shadow log (MIACODE_LOG_DIR or %TEMP%). Its presence proves the
    // process made it to line 1 of main() — its absence means the
    // process died in static init / DLL load (outside our reach), which
    // is a fundamentally different debug path than a crash inside our
    // code. Written before MC_OP, before crash_recovery::install, and
    // before QApplication construction so a crash in any of those still
    // leaves the breadcrumb behind.
    miacode::oplog::writeStartupBeacon(MIACODE_DISPLAY_VERSION_STRING);

#ifdef Q_OS_WIN
    // Vectored exception handler — installed as early as possible because
    // it cannot be displaced by later SetUnhandledExceptionFilter calls
    // from Qt / AMD UMD / Defender. Catches the fast-fail / __fastfail
    // codes (0xC0000409 etc.) that bypass the regular SEH filter chain
    // and would otherwise leave NO breadcrumb on hard process death.
    ::AddVectoredExceptionHandler(/*first=*/1, &vectoredHandler);
    miacode::oplog::appendStartupBeaconLine("phase=veh_installed");

    // Experimental beta42 startup diagnostic. Probes OS version, VC++
    // runtime DLL availability, and D3D11 device creation BEFORE any
    // Qt code touches the process — narrows "silent crash on Win10
    // after the beacon" to one of three hypotheses (A=VC runtime,
    // B=GPU driver, C=Win10 build too old) on a single run, no --debug
    // required. All output goes to the same beacon file via append.
    runStartupDiagnostic();
    miacode::oplog::appendStartupBeaconLine("phase=pre_mc_op");
#endif

    MC_OP("main");

    // Negative HS (`<HS*-N>`) is ON by default. The opt-out escape hatch
    // MIACODE_PREVIEW_REJECT_NEGATIVE_HS restores the strict reject-hs<=0
    // stance. Read once here so BOTH the GUI process and the CLI export-worker
    // subprocess (which inherits the environment) agree on what parses.
    SimaiNativeParser::setAllowNegativeHsEnabled(!miacode::debug_options::previewRejectNegativeHsEnabled());

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_crash_recovery_install");
#endif
    // Install crash-time autosave handlers BEFORE anything else can fail.
    // This way an early-startup segfault (e.g. graphics driver bug during
    // QApplication construction) still gets a chance to flush the
    // last-known document text — though in practice early crashes happen
    // before any document is loaded so the snapshot is empty / safe.
    miacode::crash_recovery::install();
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_crash_recovery_install");
#endif

#ifdef Q_OS_WIN
    // Force PER_MONITOR_AWARE_V2 DPI awareness for BOTH the editor and
    // the worker process so MoveWindow's pixel interpretation is
    // identical across the IPC boundary. Qt6's QGuiApplication
    // normally sets this in its constructor, but doing it ourselves
    // here — before any Qt code runs — guarantees the awareness is
    // locked in before any other library code (Defender, third-party
    // DLLs in startup-load) might inadvertently set a different
    // value, which can no longer be overridden once locked.
    const BOOL dpiAwareOk =
        ::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    {
        char buf[128];
        std::snprintf(buf, sizeof(buf),
            "phase=dpi_aware_set ok=%d err=%lu",
            dpiAwareOk ? 1 : 0,
            dpiAwareOk ? 0UL : static_cast<unsigned long>(::GetLastError()));
        miacode::oplog::appendStartupBeaconLine(buf);
    }
#endif

    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int index = 0; index < argc; ++index) {
        rawArgs.append(QString::fromLocal8Bit(argv[index]));
    }
    const bool cliVideoExportRequested = wantsCliVideoExport(rawArgs);
    const bool cliVideoExportWorkerRequested = wantsCliVideoExportWorker(rawArgs);
    const bool forceOpenGlGraphicsApi = cliVideoExportRequested || cliVideoExportWorkerRequested;
    const QString startupOpenTarget =
        !cliVideoExportRequested && !cliVideoExportWorkerRequested
            ? startupOpenTargetFromArguments(rawArgs)
            : QString();

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_args_parsed");
#endif
    miacode::debug_options::setDebugModeEnabled(miacode::debug_options::hasDebugArg(rawArgs));
#ifdef Q_OS_WIN
    {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "phase=debug_mode_set debug=%d",
            miacode::debug_options::debugModeEnabled() ? 1 : 0);
        miacode::oplog::appendStartupBeaconLine(buf);
    }
#endif
    if (miacode::debug_options::debugModeEnabled()) {
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=before_trim_debug_logs");
#endif
        miacode::debug_log::trimDebugSessionLogsForStartup();
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=after_trim_debug_logs");
#endif
    }
    if (miacode::debug_options::startupTimingEnabled()) {
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=before_init_startup_timing");
#endif
        miacode::debug_log::initializeStartupTimingLogSession();
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=after_init_startup_timing");
#endif
    }

    // (libmpv probe removed in beta20 — the "Phase 4 video source built on
    // top of this" never landed; chart-preview video backgrounds use Qt's
    // QMediaPlayer + QVideoSink stack via PreviewStageMediaHost. Shipping
    // libmpv-2.dll cost ~113 MB to log a single startup version line.)

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_env_var_parse");
#endif
    const bool qsgFullDisable = miacode::debug_options::previewQsgFullDisableEnabled();
    const bool forceBasicRenderLoop = qsgFullDisable
        || miacode::debug_options::envFlagEnabled(
            "MIACODE_PREVIEW_FORCE_BASIC_RENDER_LOOP"
        );
    const bool disableDontCreateNativeWidgetSiblings = miacode::debug_options::envFlagEnabled(
        "MIACODE_PREVIEW_DISABLE_DONT_CREATE_NATIVE_WIDGET_SIBLINGS"
    );
    const bool dontCreateNativeWidgetSiblingsEnabled = !disableDontCreateNativeWidgetSiblings;
    const QString requestedRenderLoop = qEnvironmentVariable("QSG_RENDER_LOOP").trimmed();
    if (forceBasicRenderLoop && requestedRenderLoop.isEmpty()) {
        qputenv("QSG_RENDER_LOOP", QByteArrayLiteral("basic"));
    }
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_set_qt_attributes");
#endif
    if (dontCreateNativeWidgetSiblingsEnabled) {
        QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    }
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_set_qt_attributes");
#endif

    // Diagnostic: capture Qt's scene-graph render timings into our runtime log
    // when the user opts in. Has to happen before QApplication construction
    // because Qt reads QSG_RENDER_TIMING / QT_LOGGING_RULES once at startup
    // and the categories are gated on those env vars. We append our category
    // override to whatever the user already has rather than overwriting, so
    // existing rule overrides keep working.
    if (miacode::debug_options::previewQsgRenderTimingEnabled()) {
        qputenv("QSG_RENDER_TIMING", QByteArrayLiteral("1"));
        const QByteArray existingRules = qgetenv("QT_LOGGING_RULES");
        QByteArray nextRules = existingRules;
        if (!nextRules.isEmpty() && !nextRules.endsWith(';')) {
            nextRules.append(';');
        }
        nextRules.append("qt.scenegraph.time.*=true");
        qputenv("QT_LOGGING_RULES", nextRules);

        // Route the resulting qt.scenegraph.time.* messages to our log so they
        // sit alongside renderer_perf / frame_pacing in the same file with
        // matching timestamps. Pass everything else through to the prior
        // handler (whatever Qt had installed before us) so we don't silence
        // ordinary Qt warnings/criticals.
        static QtMessageHandler s_priorMessageHandler = nullptr;
        s_priorMessageHandler = qInstallMessageHandler(
            [](QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
                const char* category = ctx.category != nullptr ? ctx.category : "";
                if (qstrncmp(category, "qt.scenegraph.time.", 19) == 0) {
                    miacode::debug_log::appendLine(
                        miacode::debug_log::Channel::Runtime,
                        QStringLiteral("preview/qsg_timing"),
                        QString("category=%1 msg=%2")
                            .arg(QString::fromLatin1(category))
                            .arg(msg));
                    return;
                }
                if (s_priorMessageHandler != nullptr) {
                    s_priorMessageHandler(type, ctx, msg);
                }
            });
    }
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=before_first_runtime_log_append");
#endif
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("startup/qt_config"),
            QString(
                "qsg_full_disable=%1 force_basic_render_loop=%2 qsg_render_loop=%3 "
                "dont_create_native_widget_siblings_default=1 "
                "disable_dont_create_native_widget_siblings=%4 "
                "effective_dont_create_native_widget_siblings=%5"
            )
                .arg(qsgFullDisable ? 1 : 0)
                .arg(forceBasicRenderLoop ? 1 : 0)
                .arg(qEnvironmentVariable("QSG_RENDER_LOOP").trimmed().isEmpty()
                         ? QStringLiteral("(default)")
                         : qEnvironmentVariable("QSG_RENDER_LOOP").trimmed())
                .arg(disableDontCreateNativeWidgetSiblings ? 1 : 0)
                .arg(dontCreateNativeWidgetSiblingsEnabled ? 1 : 0)
        );
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine("phase=after_first_runtime_log_append");
#endif
    }

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_startup_timer_init");
#endif
    QElapsedTimer startupTimer;
    startupTimer.start();
    qint64 lastStageMs = 0;
    const auto logStartupStage = [&](const QString& stage) {
        const qint64 nowMs = startupTimer.elapsed();
        const qint64 deltaMs = nowMs - lastStageMs;
        lastStageMs = nowMs;
        miacode::debug_log::appendStartupTimingStage(stage, nowMs, deltaMs);
    };
    logStartupStage("process_entry");

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_arm64_probe");
#endif

    // Detect Apple Silicon Windows VM (Windows-on-ARM running x86/x64
    // emulation). When detected, previewUseDCompEnabled() auto-falls-back
    // to false — the legacy QSG-only render path (beta19-equivalent),
    // which doesn't create any popup HWNDs that would otherwise crash
    // under that emulation.
    // Logged once at startup so support can confirm the fallback fired.
    if (miacode::debug_options::runningOnArm64WindowsEmulation()) {
        miacode::debug_log::appendStartupTimingStage(
            QStringLiteral("arm64_emulation_detected"),
            startupTimer.elapsed(), 0);
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("startup/preview_path"),
            QStringLiteral("reason=arm64_windows_emulation "
                          "dcomp=false "
                          "fallback=qsg_only_legacy"));
    }
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_arm64_probe");
#endif

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    // Triple-buffer + vsync. With DoubleBuffer the swap chain caps the GPU at 1 frame in
    // flight, so a fast Render submit (~5 ms) immediately stalls on the next Present until
    // the previous frame swaps — surfacing as a bimodal render_submit_ms (median ~5 ms,
    // p90 ~24 ms ≈ one vsync of forced wait). TripleBuffer keeps two frames in flight,
    // letting the CPU stay one frame ahead of the GPU and removing the back-pressure stall.
    // Cost: one extra frame (~16.7 ms) of latency from input to display, which is
    // imperceptible for a chart preview where the playhead is audio-authoritative anyway.
    format.setSwapBehavior(QSurfaceFormat::TripleBuffer);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);
    logStartupStage("surface_format_ready");

#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=before_qapplication_construct");
#endif
    QApplication app(argc, argv);
    miacode::hang_watchdog::installGuiHeartbeat(&app);
#ifdef Q_OS_WIN
    miacode::oplog::appendStartupBeaconLine("phase=after_qapplication_construct");
#endif

    // Backend selection. CLI export / export worker default to Direct3D11 for
    // the P5 D3D11/QRhi export session; MIACODE_EXPORT_RENDER_BACKEND=opengl
    // keeps the stable OpenGL FBO/PBO path as an explicit rollback
    // (Windows only — the session itself falls back to OpenGL if init fails, see
    // VideoExportPreparedTask). Otherwise: honour user's --rhi=<name> if present (and
    // persist for next launch), else fall back to the persisted choice from the prior
    // run, else Qt's platform default.
    QString appliedGraphicsBackend;
    QString graphicsBackendSource;
    if (forceOpenGlGraphicsApi) {
#ifdef Q_OS_WIN
        const miacode::debug_options::ExportRenderBackendRequest exportBackendRequest =
            miacode::debug_options::exportRenderBackendRequest();
        if (exportBackendRequest != miacode::debug_options::ExportRenderBackendRequest::OpenGl) {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
            appliedGraphicsBackend = QStringLiteral("d3d11");
            graphicsBackendSource = miacode::debug_options::envValue("MIACODE_EXPORT_RENDER_BACKEND").isEmpty()
                ? QStringLiteral("cli_video_export_default_d3d11_qrhi")
                : QStringLiteral("cli_video_export_env_d3d11_qrhi");
        } else
#endif
        {
            QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
            appliedGraphicsBackend = QStringLiteral("opengl");
            graphicsBackendSource = QStringLiteral("cli_video_export_force");
        }
    } else if (qsgFullDisable) {
        // Diagnostic mode: completely exclude Qt Quick's native render
        // path. Force software backend regardless of CLI / persisted
        // setting; do NOT persist, so a normal next launch reverts.
        appliedGraphicsBackend =
            applyGraphicsBackendChoice(QStringLiteral("software"));
        graphicsBackendSource = QStringLiteral("qsg_full_disable_forced");
    } else {
        const GraphicsBackendChoice choice = resolveGraphicsBackendChoice(rawArgs);
        if (choice.fromCommandLine) {
            // Persist (or clear) so the next launch defaults to the same backend without
            // requiring the flag again.
            writePersistedGraphicsBackend(choice.name);
        }
        appliedGraphicsBackend = applyGraphicsBackendChoice(choice.name);
        graphicsBackendSource = choice.fromCommandLine
            ? (choice.clearedByCommand
                   ? QStringLiteral("cli_cleared")
                   : QStringLiteral("cli_override"))
            : (choice.name.isEmpty() ? QStringLiteral("platform_default")
                                     : QStringLiteral("persisted"));
    }
    logStartupStage("qapplication_constructed");
#ifdef Q_OS_WIN
    {
        const QByteArray appliedUtf8 = appliedGraphicsBackend.isEmpty()
            ? QByteArrayLiteral("(qt_default)")
            : appliedGraphicsBackend.toUtf8();
        const QByteArray sourceUtf8 = graphicsBackendSource.toUtf8();
        char buf[256];
        std::snprintf(buf, sizeof(buf),
            "phase=graphics_api_applied backend=%.64s source=%.64s",
            appliedUtf8.constData(), sourceUtf8.constData());
        miacode::oplog::appendStartupBeaconLine(buf);
    }
#endif
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("startup/graphics_backend"),
            QString("applied=%1 source=%2 persisted_path=%3")
                .arg(appliedGraphicsBackend.isEmpty() ? QStringLiteral("(qt_default)")
                                                      : appliedGraphicsBackend)
                .arg(graphicsBackendSource)
                .arg(persistedGraphicsBackendFilePath())
        );
    }
    if (miacode::debug_options::runtimeDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("startup/qt_config"),
            QString("graphics_api=%1 dont_create_native_widget_siblings=%2 cli_export=%3 cli_export_worker=%4")
                .arg(forceOpenGlGraphicsApi ? appliedGraphicsBackend : QStringLiteral("PlatformDefault"))
                .arg(QApplication::testAttribute(Qt::AA_DontCreateNativeWidgetSiblings) ? 1 : 0)
                .arg(cliVideoExportRequested ? 1 : 0)
                .arg(cliVideoExportWorkerRequested ? 1 : 0)
        );
    }

    // P0/P2/P3 — process identity + GPU hint + resolved GPU policy, emitted for
    // every role (gui / cli_export / export_worker). CLI export + worker return
    // early just below, so this has to run before that dispatch. Gated on
    // --debug inside the call; re-emitted after the log dir rebinds to a chart
    // (see logProcessStartupDiagnostics) so the collected project log has them.
    logProcessStartupDiagnostics(QStringLiteral("boot"));

#ifdef Q_OS_WIN
    setWindowsAppUserModelId();
#endif
    app.setApplicationName("MiaCode");
    app.setApplicationVersion(MIACODE_DISPLAY_VERSION_STRING);
    // First-run detection. The preferences file is auto-created the first
    // time a UiText/UiTheme preference is read (the UiTheme::applyApplicationTheme
    // call just below is the first such read), so we must probe its existence
    // *here* — after the application name is set (AppConfigLocation depends on
    // it) but before anything reads a preference. "No preferences on this
    // machine" == show the welcome / initial-config dialog. The --welcome flag
    // force-shows it for debugging even when preferences already exist.
    const bool miacodePreferencesExistedAtStartup = QFile::exists(UiText::preferencesFilePath());
    // A preferences-schema bump (UiText kPreferencesSchema) intentionally
    // re-runs onboarding: an existing user whose stored preferences predate the
    // current schema gets the welcome dialog once more so new first-run choices
    // (e.g. the Chinese-input mode) are surfaced and re-saved under the new
    // schema. Probe the RAW on-disk schema HERE — storedPreferencesSchema()
    // does not normalize, and this must run before applyApplicationTheme() below
    // auto-creates/rewrites the file with the current schema.
    const bool miacodePreferencesSchemaOutdated =
        UiText::storedPreferencesSchema() != UiText::currentPreferencesSchema();
    const bool shouldShowWelcomeDialog =
        !miacodePreferencesExistedAtStartup
        || miacodePreferencesSchemaOutdated
        || wantsWelcomeDialog(rawArgs);
    const QIcon appIcon(QStringLiteral(":/icons/app.png"));
#ifndef Q_OS_MACOS
    app.setWindowIcon(appIcon);
#endif
    app.setStyle(QStyleFactory::create("Fusion"));
    UiTheme::applyApplicationTheme(app);
    // Keep the tooltip fade effect, but disable the slide/scroll animation (on
    // Windows these mirror the OS tooltip fade/animate effects).
    QApplication::setEffectEnabled(Qt::UI_FadeTooltip, true);
    QApplication::setEffectEnabled(Qt::UI_AnimateTooltip, false);
    logStartupStage("app_style_ready");

    {
        QStringList cjkUiFamilies;
        const QString uiLanguageToken = UiText::resolvedLanguageToken();
        if (uiLanguageToken.startsWith(QStringLiteral("zh"))) {
            cjkUiFamilies = QStringList{"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC"};
        } else if (uiLanguageToken.startsWith(QStringLiteral("ja"))) {
            cjkUiFamilies = QStringList{"Yu Gothic UI", "Meiryo UI", "Meiryo", "Noto Sans CJK JP"};
        }
        if (!cjkUiFamilies.isEmpty()) {
            QFont cjkUiFont;
            for (const QString& family : cjkUiFamilies) {
                cjkUiFont.setFamily(family);
                if (cjkUiFont.family().compare(family, Qt::CaseInsensitive) == 0) {
                    break;
                }
            }
            cjkUiFont.setStyleStrategy(QFont::PreferAntialias);
            cjkUiFont.setHintingPreference(QFont::PreferNoHinting);
            app.setFont(cjkUiFont);
        }
    }
    logStartupStage("ui_font_ready");

    if (cliVideoExportWorkerRequested) {
        QString cliError;
        const int exitCode = runCliVideoExportWorker(app, &cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "Worker error: " << cliError << "\n";
        }
        return exitCode;
    }

    if (cliVideoExportRequested) {
        QString cliError;
        const int exitCode = runCliVideoExport(app, &cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "CLI argument error: " << cliError << "\n";
        }
        return exitCode;
    }

    // GUI session from here on. Arm the abnormal-exit session marker —
    // CLI export / worker runs above must never touch it (they open
    // charts through the same MainWindow code paths).
    miacode::crash_recovery::setSessionMarkerEnabled(true);

    // Auto-theme native title bars of every QWidget top-level (dialogs,
    // message boxes) as they show, including tools-layer dialogs that can't
    // reach MainWindow::WindowSection. QML root windows are themed by
    // QuickShellBootstrap. GUI-only: CLI runs above never show windows.
    UiNativeWindowTheme::installAutoApplyFilter();

    // Phase 3a of the v2-refactor — `--quick-shell-beta` becomes the
    // canonical opt-in for the new DComp pipeline. Setting the env vars
    // here (before any code that reads them via envFlagEnabled) makes
    // the flag self-contained: users running with --quick-shell-beta no
    // longer need to also set MIACODE_PREVIEW_USE_DCOMP=1 in their
    // shell, and the same release build covers both legacy QSG and
    // DComp paths via this single argv check.
    //
    // We use qputenv(..., "1") only when the env var is currently
    // *unset*, so an explicit MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND=0 in
    // the launching shell still wins (lets the user A/B without
    // rebuilding). previewUseDCompEnabled defers to envFlagEnabled
    // which checks the live env on every call, so setting it here is
    // sufficient.
    const bool quickShellBetaRequested = wantsQuickShellBeta(app.arguments());
    if (quickShellBetaRequested) {
        if (qEnvironmentVariableIsEmpty("MIACODE_PREVIEW_USE_DCOMP")) {
            qputenv("MIACODE_PREVIEW_USE_DCOMP", QByteArrayLiteral("1"));
        }
    }
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QElapsedTimer appExecElapsed;
    QElapsedTimer postLastWindowClosedElapsed;
    bool lastWindowClosedSeen = false;
    int shutdownHeartbeatCount = 0;
    QTimer shutdownHeartbeatTimer;
    shutdownHeartbeatTimer.setInterval(250);
    shutdownHeartbeatTimer.setSingleShot(false);

    QObject::connect(&app, &QGuiApplication::lastWindowClosed, &app, [&]() {
        lastWindowClosedSeen = true;
        shutdownHeartbeatCount = 0;
        postLastWindowClosedElapsed.restart();
        appendAppShutdownRuntimeLog(
            QStringLiteral("last_window_closed"),
            QStringLiteral("quit_on_last_window_closed=%1 top_level_widgets=%2 widgets={%3} top_level_windows=%4 windows={%5}")
                .arg(app.quitOnLastWindowClosed() ? 1 : 0)
                .arg(QApplication::topLevelWidgets().size())
                .arg(summarizeTopLevelWidgets())
                .arg(QGuiApplication::topLevelWindows().size())
                .arg(summarizeTopLevelWindows())
        );
        shutdownHeartbeatTimer.start();
    });
    QObject::connect(&shutdownHeartbeatTimer, &QTimer::timeout, &app, [&]() {
        if (!lastWindowClosedSeen) {
            return;
        }
        ++shutdownHeartbeatCount;
        const qint64 elapsedMs = postLastWindowClosedElapsed.isValid() ? postLastWindowClosedElapsed.elapsed() : -1;
        appendAppShutdownRuntimeLog(
            QStringLiteral("last_window_closed_heartbeat"),
            QStringLiteral("count=%1 elapsed_ms=%2 closing_down=%3 top_level_widgets=%4 widgets={%5} top_level_windows=%6 windows={%7}")
                .arg(shutdownHeartbeatCount)
                .arg(elapsedMs)
                .arg(QCoreApplication::closingDown() ? 1 : 0)
                .arg(QApplication::topLevelWidgets().size())
                .arg(summarizeTopLevelWidgets())
                .arg(QGuiApplication::topLevelWindows().size())
                .arg(summarizeTopLevelWindows())
        );
    });
    QObject::connect(&app, &QCoreApplication::aboutToQuit, &app, [&]() {
        shutdownHeartbeatTimer.stop();
        appendAppShutdownRuntimeLog(
            QStringLiteral("about_to_quit"),
            QStringLiteral("after_last_window_closed_ms=%1 top_level_widgets=%2 widgets={%3} top_level_windows=%4 windows={%5}")
                .arg(postLastWindowClosedElapsed.isValid() ? postLastWindowClosedElapsed.elapsed() : -1)
                .arg(QApplication::topLevelWidgets().size())
                .arg(summarizeTopLevelWidgets())
                .arg(QGuiApplication::topLevelWindows().size())
                .arg(summarizeTopLevelWindows())
        );
    });

    int exitCode = 1;
    QElapsedTimer postExecObjectTeardownElapsed;
    {
        const UiSkin uiSkin = resolveUiSkin(app.arguments());
#ifdef Q_OS_WIN
        miacode::oplog::appendStartupBeaconLine(
            uiSkin == UiSkin::QmlUiV2
                ? "phase=before_qml_ui_bootstrap_start"
                : "phase=before_quick_shell_bootstrap_start");
#endif
        if (uiSkin == UiSkin::QmlUiV2) {
            QmlUiBootstrap qmlUiBootstrap(appIcon);
            qmlUiBootstrap.setShowWelcomeDialogOnStartup(shouldShowWelcomeDialog);
            if (!qmlUiBootstrap.start(startupOpenTarget)) {
#ifdef Q_OS_WIN
                miacode::oplog::appendStartupBeaconLine("phase=qml_ui_bootstrap_failed");
#endif
                QTextStream(stderr) << "Failed to start QML UI (v2).\n";
                return 1;
            }
            logStartupStage("qml_ui_bootstrap_started");
#ifdef Q_OS_WIN
            miacode::oplog::appendStartupBeaconLine("phase=qml_ui_bootstrap_started");
#endif
            QTimer::singleShot(0, &app, [&logStartupStage]() {
                logStartupStage("event_loop_first_tick");
#ifdef Q_OS_WIN
                miacode::oplog::appendStartupBeaconLine("phase=event_loop_first_tick");
#endif
            });
            appExecElapsed.start();
#ifdef Q_OS_WIN
            miacode::oplog::appendStartupBeaconLine("phase=entering_event_loop");
#endif
            exitCode = app.exec();
        } else {
            QuickShellBootstrap quickShellBootstrap(appIcon);
            quickShellBootstrap.setShowWelcomeDialogOnStartup(shouldShowWelcomeDialog);
            if (!quickShellBootstrap.start(startupOpenTarget)) {
#ifdef Q_OS_WIN
                miacode::oplog::appendStartupBeaconLine("phase=quick_shell_bootstrap_failed");
#endif
                QTextStream(stderr) << "Failed to start Quick Shell Beta.\n";
                return 1;
            }
            logStartupStage("quick_shell_bootstrap_started");
#ifdef Q_OS_WIN
            miacode::oplog::appendStartupBeaconLine("phase=quick_shell_bootstrap_started");
#endif
            QTimer::singleShot(0, &app, [&logStartupStage]() {
                logStartupStage("event_loop_first_tick");
#ifdef Q_OS_WIN
                miacode::oplog::appendStartupBeaconLine("phase=event_loop_first_tick");
#endif
            });
            appExecElapsed.start();
#ifdef Q_OS_WIN
            miacode::oplog::appendStartupBeaconLine("phase=entering_event_loop");
#endif
            exitCode = app.exec();
        }
        miacode::debug_log::appendTimingLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("app_shutdown"),
            QStringLiteral("event_loop_exit"),
            appExecElapsed.elapsed(),
            QStringLiteral("exit_code=%1 after_last_window_closed_ms=%2")
                .arg(exitCode)
                .arg(postLastWindowClosedElapsed.isValid() ? postLastWindowClosedElapsed.elapsed() : -1)
        );
        postExecObjectTeardownElapsed.start();
    }
    miacode::debug_log::appendTimingLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown"),
        QStringLiteral("post_exec_object_teardown"),
        postExecObjectTeardownElapsed.isValid() ? postExecObjectTeardownElapsed.elapsed() : -1,
        QStringLiteral("top_level_widgets=%1 top_level_windows=%2")
            .arg(QApplication::topLevelWidgets().size())
            .arg(QGuiApplication::topLevelWindows().size())
    );
    // Permanently shut down the async log writer last so any teardown logs above are
    // drained to disk and the worker thread is joined before the process exits.
    miacode::debug_log::shutdownAsyncLogWriter();
    return exitCode;
}
