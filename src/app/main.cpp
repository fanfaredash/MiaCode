#include "AppVersion.h"
#include "quick_shell/QuickShellBootstrap.h"
#include "mainwindow/MainWindow.h"
#include "tools/video_export/VideoExportSnapshot.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/WaveformCache.h"
#include "core/video/MpvProbe.h"

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
#endif

namespace {

#ifdef Q_OS_WIN
void setWindowsAppUserModelId()
{
    static HMODULE shell32Module = ::LoadLibraryW(L"shell32.dll");
    if (shell32Module == nullptr) {
        return;
    }
    using SetAppIdFn = HRESULT(WINAPI*)(PCWSTR);
    static auto setAppId = reinterpret_cast<SetAppIdFn>(
        ::GetProcAddress(shell32Module, "SetCurrentProcessExplicitAppUserModelID")
    );
    if (setAppId != nullptr) {
        setAppId(L"fanfaredash.MiaCode");
    }
}
#endif

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

// ===== Graphics backend selector =====
//
// User-driven backend selection without driver probing (which would touch GPU vendor DLLs
// and trip Windows Defender heuristics). Strategy:
//   1. CLI flag `--rhi=<name>` overrides everything for this run AND persists the choice
//      so the same backend is used on the next launch.
//   2. With no flag, the persisted choice from a small JSON file beside the executable is
//      applied. If neither exists, we leave Qt on its platform default (D3D11 on Windows).
//   3. The persistence file is plain JSON, written only on explicit user choice — the
//      app never tries backends behind the user's back. If a chosen backend fails to
//      initialise the user can recover by relaunching with `--rhi=auto` (clears the file)
//      or `--rhi=d3d11` (forces the safe Windows default).
//
// Recognised values: "auto" / "default" (no override), "d3d11", "d3d12", "opengl",
// "vulkan", "metal", "software". Anything else is rejected and we fall through to auto.

struct GraphicsBackendChoice {
    QString name;            // canonical lowercase name, or empty for "auto"
    bool fromCommandLine;    // true if this came from --rhi= rather than the saved file
    bool clearedByCommand;   // true if the user explicitly asked to clear via --rhi=auto
};

QString persistedGraphicsBackendFilePath()
{
    // Beside the executable so the file is portable with the install. Hidden (leading dot)
    // to keep the directory uncluttered.
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(QStringLiteral(".miacode_graphics.json"));
}

QString readPersistedGraphicsBackend()
{
    const QString path = persistedGraphicsBackendFilePath();
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    const QByteArray bytes = file.readAll();
    file.close();
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(bytes, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        return QString();
    }
    return doc.object().value(QStringLiteral("backend")).toString().trimmed().toLower();
}

bool writePersistedGraphicsBackend(const QString& backend)
{
    const QString path = persistedGraphicsBackendFilePath();
    if (backend.isEmpty()) {
        // "auto" / clear: just remove the file.
        QFile::remove(path);
        return true;
    }
    QJsonObject obj;
    obj.insert(QStringLiteral("backend"), backend);
    obj.insert(QStringLiteral("schema"), QStringLiteral("miacode_graphics_v1"));
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        return false;
    }
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    const qint64 written = file.write(bytes);
    file.close();
    return written == bytes.size();
}

QString canonicalRhiName(const QString& raw)
{
    const QString name = raw.trimmed().toLower();
    if (name == QStringLiteral("auto") || name == QStringLiteral("default")
        || name == QStringLiteral("platform") || name == QStringLiteral("none")) {
        return QString();
    }
    if (name == QStringLiteral("d3d11") || name == QStringLiteral("direct3d11")
        || name == QStringLiteral("dx11")) {
        return QStringLiteral("d3d11");
    }
    if (name == QStringLiteral("d3d12") || name == QStringLiteral("direct3d12")
        || name == QStringLiteral("dx12")) {
        return QStringLiteral("d3d12");
    }
    if (name == QStringLiteral("opengl") || name == QStringLiteral("gl")) {
        return QStringLiteral("opengl");
    }
    if (name == QStringLiteral("vulkan") || name == QStringLiteral("vk")) {
        return QStringLiteral("vulkan");
    }
    if (name == QStringLiteral("metal")) {
        return QStringLiteral("metal");
    }
    if (name == QStringLiteral("software") || name == QStringLiteral("sw")
        || name == QStringLiteral("null")) {
        return QStringLiteral("software");
    }
    return QString();  // unknown — caller treats as "no override".
}

// Pulls "--rhi=<value>" or "--rhi <value>" out of the raw argv. We do NOT use
// QCommandLineParser here because it requires a constructed QApplication, and we want to
// pick the backend before that.
QString parseRhiCommandLineArg(const QStringList& args)
{
    static const QString kFlag = QStringLiteral("--rhi");
    static const QString kFlagEq = QStringLiteral("--rhi=");
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        if (a.startsWith(kFlagEq)) {
            return a.mid(kFlagEq.size());
        }
        if (a == kFlag && (i + 1) < args.size()) {
            return args.at(i + 1);
        }
    }
    return QString();
}

GraphicsBackendChoice resolveGraphicsBackendChoice(const QStringList& args)
{
    GraphicsBackendChoice choice{};
    const QString cliRaw = parseRhiCommandLineArg(args);
    if (!cliRaw.isEmpty()) {
        choice.fromCommandLine = true;
        const QString canonical = canonicalRhiName(cliRaw);
        choice.name = canonical;
        choice.clearedByCommand = canonical.isEmpty()
            && (cliRaw.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0
                || cliRaw.compare(QStringLiteral("default"), Qt::CaseInsensitive) == 0
                || cliRaw.compare(QStringLiteral("none"), Qt::CaseInsensitive) == 0);
        return choice;
    }
    choice.name = readPersistedGraphicsBackend();
    return choice;
}

// Apply the chosen backend to QQuickWindow. Must be called AFTER QApplication construction
// (because setGraphicsApi consults Qt's per-process state) but BEFORE any QQuickWindow
// is realised. Returns the canonical name actually applied (empty for "auto/default").
QString applyGraphicsBackendChoice(const QString& backend)
{
    if (backend == QStringLiteral("d3d11")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D11);
    } else if (backend == QStringLiteral("d3d12")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D12);
    } else if (backend == QStringLiteral("opengl")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    } else if (backend == QStringLiteral("vulkan")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Vulkan);
    } else if (backend == QStringLiteral("metal")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Metal);
    } else if (backend == QStringLiteral("software")) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    } else {
        return QString();
    }
    return backend;
}

void addSharedCliDebugOption(QCommandLineParser& parser)
{
    // main() already enables debug mode before CLI dispatch. We still declare
    // this option here so subcommand parsers accept forwarded "--debug".
    parser.addOption(QCommandLineOption(
        QStringLiteral("debug"),
        QStringLiteral("Enable debug mode and debug-only log output.")
    ));
}

QString currentExceptionDetail()
{
    try {
        throw;
    } catch (const std::bad_alloc&) {
        return QStringLiteral("std::bad_alloc");
    } catch (const std::exception& ex) {
        const QString what = QString::fromUtf8(ex.what()).trimmed();
        return what.isEmpty() ? QStringLiteral("std::exception") : what;
    } catch (...) {
        return QStringLiteral("unknown non-std exception");
    }
}

QString videoExportWorkerProjectLogDirectory(const VideoExportSnapshot& snapshot)
{
    const QString chartPath = snapshot.originalChartPath.trimmed();
    if (!chartPath.isEmpty()) {
        const QString projectDataDirectoryPath = miacode::waveform::projectDataDirectoryPathForFile(chartPath);
        if (!projectDataDirectoryPath.isEmpty()) {
            return QDir(projectDataDirectoryPath).filePath(QStringLiteral("logs"));
        }
    }

    const QString projectDir = snapshot.projectDir.trimmed();
    if (!projectDir.isEmpty()) {
        return QDir(QDir::cleanPath(projectDir)).filePath(QStringLiteral(".miacode/logs"));
    }

    return QString();
}

void writeWorkerJsonLine(const QJsonObject& object)
{
    QTextStream out(stdout);
    out << QJsonDocument(object).toJson(QJsonDocument::Compact) << '\n';
    out.flush();
}

bool parseCliResolutionToken(const QString& token, int* outputWidth, int* outputHeight)
{
    if (outputWidth == nullptr || outputHeight == nullptr) {
        return false;
    }
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    static const QRegularExpression re(
        QStringLiteral("^\\s*(\\d+)\\s*(?:[xX]\\s*(\\d+)\\s*)?$")
    );
    const QRegularExpressionMatch match = re.match(trimmed);
    if (!match.hasMatch()) {
        return false;
    }
    bool widthOk = false;
    const int parsedWidth = match.captured(1).toInt(&widthOk);
    if (!widthOk || parsedWidth <= 0) {
        return false;
    }
    const QString heightText = match.captured(2).trimmed();
    if (heightText.isEmpty()) {
        *outputWidth = parsedWidth;
        *outputHeight = parsedWidth;
        return true;
    }
    bool heightOk = false;
    const int parsedHeight = heightText.toInt(&heightOk);
    if (!heightOk || parsedHeight <= 0) {
        return false;
    }
    *outputWidth = parsedWidth;
    *outputHeight = parsedHeight;
    return true;
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

int runCliVideoExport(QApplication& app, QString* errorMessage)
{
    try {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MiaCode CLI video export"));
    parser.addHelpOption();
    parser.addVersionOption();
    addSharedCliDebugOption(parser);
    parser.addOption(QCommandLineOption(
        QStringLiteral("export-video"),
        QStringLiteral("Run single-pass video export and exit.")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("chart"), QStringLiteral("chart-path")},
        QStringLiteral("Chart file path or chart directory path."),
        QStringLiteral("path")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("d"), QStringLiteral("difficulty")},
        QStringLiteral("Difficulty short name or id (ESY/BAS/ADV/EXP/MAS/REM/UTG or 1..7)."),
        QStringLiteral("difficulty"),
        QStringLiteral("MAS")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("r"), QStringLiteral("resolution")},
        QStringLiteral("Output resolution. Accepts N (square) or WxH (e.g. 1280x720)."),
        QStringLiteral("size"),
        QStringLiteral("1024")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("f"), QStringLiteral("fps")},
        QStringLiteral("Output frame rate."),
        QStringLiteral("fps"),
        QStringLiteral("60")
    ));
    parser.addOption(QCommandLineOption(
        QStringList{QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Output .mp4 file path or output directory."),
        QStringLiteral("path")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("start"),
        QStringLiteral("Export start second."),
        QStringLiteral("seconds"),
        QStringLiteral("0")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("duration"),
        QStringLiteral("Export content duration in seconds. Omit to export until timeline end."),
        QStringLiteral("seconds")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("hide-timestamp"),
        QStringLiteral("Hide timestamp overlay in output video.")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("show-object-stats"),
        QStringLiteral("Show object stats HUD in output video.")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("smooth-brightness"),
        QStringLiteral("Enable smooth brightness in output video.")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("brightness-outer"),
        QStringLiteral("Outer background brightness (0.0-1.0)."),
        QStringLiteral("value"),
        QString::number(miacode::preview_video::kBackgroundBrightnessDefault, 'f', 2)
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("brightness-inner"),
        QStringLiteral("Inner background brightness (0.0-1.0)."),
        QStringLiteral("value"),
        QString::number(miacode::preview_video::kBackgroundBrightnessInnerDefault, 'f', 2)
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("layout-square-scale"),
        QStringLiteral("Judge line size scale."),
        QStringLiteral("value"),
        QString::number(miacode::preview_video::kLayoutSquareScaleDefault, 'f', 2)
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("background-scale"),
        QStringLiteral("Background scale mode: fill or fit."),
        QStringLiteral("mode"),
        QStringLiteral("fill")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("flow-speed"),
        QStringLiteral("Note flow speed."),
        QStringLiteral("value"),
        QString::number(miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed, 'f', 2)
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("skin-wait-ms"),
        QStringLiteral("Max wait milliseconds for async skin loading before export."),
        QStringLiteral("milliseconds"),
        QStringLiteral("2000")
    ));

    if (!parser.parse(app.arguments())) {
        if (errorMessage != nullptr) {
            *errorMessage = parser.errorText();
        }
        return 2;
    }
    if (!parser.isSet(QStringLiteral("export-video"))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("internal CLI dispatch error: --export-video not set");
        }
        return 2;
    }

    const QString chartInput = parser.value(QStringLiteral("chart")).trimmed();
    if (chartInput.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--chart is required in --export-video mode");
        }
        return 2;
    }

    int outputWidth = 0;
    int outputHeight = 0;
    const bool resolutionOk = parseCliResolutionToken(
        parser.value(QStringLiteral("resolution")),
        &outputWidth,
        &outputHeight
    );
    bool fpsOk = false;
    const int fps = parser.value(QStringLiteral("fps")).toInt(&fpsOk);
    bool startOk = false;
    const double startSeconds = parser.value(QStringLiteral("start")).toDouble(&startOk);
    bool outerBrightnessOk = false;
    const double outerBrightness = parser.value(QStringLiteral("brightness-outer")).toDouble(&outerBrightnessOk);
    bool innerBrightnessOk = false;
    const double innerBrightness = parser.value(QStringLiteral("brightness-inner")).toDouble(&innerBrightnessOk);
    bool layoutScaleOk = false;
    const double layoutSquareScale = parser.value(QStringLiteral("layout-square-scale")).toDouble(&layoutScaleOk);
    const QString backgroundScaleToken = parser.value(QStringLiteral("background-scale")).trimmed().toLower();
    bool flowSpeedOk = false;
    const double flowSpeed = parser.value(QStringLiteral("flow-speed")).toDouble(&flowSpeedOk);
    bool skinWaitOk = false;
    const int skinWaitMs = parser.value(QStringLiteral("skin-wait-ms")).toInt(&skinWaitOk);

    if (!resolutionOk || outputWidth <= 0 || outputHeight <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--resolution must be N or WxH (positive integers)");
        }
        return 2;
    }
    if (outputWidth < outputHeight) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--resolution currently requires width >= height");
        }
        return 2;
    }
    if (!fpsOk || fps <= 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--fps must be a positive integer");
        }
        return 2;
    }
    if (!startOk || !std::isfinite(startSeconds) || startSeconds < 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--start must be a non-negative number");
        }
        return 2;
    }
    if (!skinWaitOk || skinWaitMs < 0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--skin-wait-ms must be a non-negative integer");
        }
        return 2;
    }
    if (!outerBrightnessOk || !std::isfinite(outerBrightness) || outerBrightness < 0.0 || outerBrightness > 1.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--brightness-outer must be between 0.0 and 1.0");
        }
        return 2;
    }
    if (!innerBrightnessOk || !std::isfinite(innerBrightness) || innerBrightness < 0.0 || innerBrightness > 1.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--brightness-inner must be between 0.0 and 1.0");
        }
        return 2;
    }
    if (!layoutScaleOk || !std::isfinite(layoutSquareScale) || layoutSquareScale <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--layout-square-scale must be a positive number");
        }
        return 2;
    }
    if (backgroundScaleToken != QStringLiteral("fill") && backgroundScaleToken != QStringLiteral("fit")) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--background-scale must be either fill or fit");
        }
        return 2;
    }
    if (!flowSpeedOk || !std::isfinite(flowSpeed) || flowSpeed <= 0.0) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--flow-speed must be a positive number");
        }
        return 2;
    }

    double durationSeconds = -1.0;
    if (parser.isSet(QStringLiteral("duration"))) {
        bool durationOk = false;
        durationSeconds = parser.value(QStringLiteral("duration")).toDouble(&durationOk);
        if (!durationOk || !std::isfinite(durationSeconds) || durationSeconds <= 0.0) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("--duration must be a positive number");
            }
            return 2;
        }
    }

    MainWindow::CliVideoExportRequest request;
    request.chartPathOrDirectory = chartInput;
    request.difficulty = parser.value(QStringLiteral("difficulty")).trimmed();
    request.outputPath = parser.value(QStringLiteral("output")).trimmed();
    request.outputWidth = outputWidth;
    request.outputHeight = outputHeight;
    request.fps = fps;
    request.exportStartSeconds = startSeconds;
    request.contentDurationSeconds = durationSeconds;
    request.showTimestamp = !parser.isSet(QStringLiteral("hide-timestamp"));
    request.showObjectStatsHud = parser.isSet(QStringLiteral("show-object-stats"));
    request.smoothBrightness = parser.isSet(QStringLiteral("smooth-brightness"));
    request.backgroundBrightnessOuter = outerBrightness;
    request.backgroundBrightnessInner = innerBrightness;
    request.layoutSquareScale = layoutSquareScale;
    request.backgroundScaleMode = backgroundScaleToken == QStringLiteral("fit")
        ? PreviewBackgroundScaleMode::FitContain
        : PreviewBackgroundScaleMode::FillCrop;
    request.noteFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
    request.touchFlowSpeed = request.noteFlowSpeed;
    request.skinLoadWaitMs = skinWaitMs;

    MainWindow window;
    QString resolvedOutputPath;
    QString exportError;
    QString exportDetails;
    if (!window.exportPreviewVideoFromCli(request, &resolvedOutputPath, &exportError, &exportDetails)) {
        QTextStream(stderr) << "Video export failed: " << exportError << "\n";
        if (!exportDetails.trimmed().isEmpty()) {
            QTextStream(stderr) << exportDetails << "\n";
        }
        return 1;
    }

    QTextStream(stdout) << "Video export success: " << QDir::toNativeSeparators(resolvedOutputPath) << "\n";
    if (!exportDetails.trimmed().isEmpty()) {
        QTextStream(stdout) << exportDetails << "\n";
    }
    return 0;
    } catch (...) {
        const QString detail = currentExceptionDetail();
        const QString message = QStringLiteral("Unhandled CLI export exception: %1").arg(detail);
        miacode::debug_log::appendFatalMessage(QStringLiteral("export/cli_exception"), message);
        if (errorMessage != nullptr) {
            *errorMessage = message;
        }
        return 1;
    }
}

int runCliVideoExportWorker(QApplication& app, QString* errorMessage)
{
    QString workerJobId;
    try {
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("MiaCode export worker"));
    parser.addHelpOption();
    parser.addVersionOption();
    addSharedCliDebugOption(parser);
    parser.addOption(QCommandLineOption(
        QStringLiteral("export-video-worker"),
        QStringLiteral("Run background export worker and exit.")
    ));

    if (!parser.parse(app.arguments())) {
        if (errorMessage != nullptr) {
            *errorMessage = parser.errorText();
        }
        return 2;
    }
    if (!parser.isSet(QStringLiteral("export-video-worker"))) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("internal CLI dispatch error: --export-video-worker not set");
        }
        return 2;
    }

    writeWorkerJsonLine(QJsonObject{
        {QStringLiteral("event"), QStringLiteral("worker_ready")},
        {QStringLiteral("protocol"), 1},
    });

    QFile stdinFile;
    if (!stdinFile.open(stdin, QIODevice::ReadOnly)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("failed to open stdin for export worker");
        }
        return 1;
    }

    const QList<QByteArray> inputLines = stdinFile.readAll().split('\n');
    QByteArray rawCommand;
    for (const QByteArray& line : inputLines) {
        if (!line.trimmed().isEmpty()) {
            rawCommand = line.trimmed();
            break;
        }
    }
    if (rawCommand.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export worker received empty command payload");
        }
        return 1;
    }

    QJsonParseError parseError;
    const QJsonDocument commandDocument = QJsonDocument::fromJson(rawCommand, &parseError);
    if (parseError.error != QJsonParseError::NoError || !commandDocument.isObject()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("export worker failed to parse command JSON");
        }
        return 1;
    }

    const QJsonObject commandObject = commandDocument.object();
    if (commandObject.value(QStringLiteral("cmd")).toString() != QLatin1String("start_export")) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unsupported export worker command");
        }
        return 1;
    }

    VideoExportSnapshot snapshot;
    QString snapshotError;
    if (!VideoExportSnapshot::fromJson(commandObject.value(QStringLiteral("snapshot")).toObject(), &snapshot, &snapshotError)) {
        if (errorMessage != nullptr) {
            *errorMessage = snapshotError;
        }
        return 1;
    }
    workerJobId = snapshot.jobId;
    miacode::debug_log::setSessionProjectLogDirectory(videoExportWorkerProjectLogDirectory(snapshot));

    writeWorkerJsonLine(QJsonObject{
        {QStringLiteral("event"), QStringLiteral("accepted")},
        {QStringLiteral("job_id"), snapshot.jobId},
    });

    VideoExportTask task;
    QString taskError;
    if (!buildVideoExportTaskFromSnapshot(snapshot, &task, &taskError)) {
        writeWorkerJsonLine(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("finished")},
            {QStringLiteral("job_id"), snapshot.jobId},
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), QStringLiteral("Failed to prepare export task.")},
            {QStringLiteral("details"), taskError},
        });
        return 1;
    }

    const auto progressCallback = [&snapshot](int percent, const QString& text) {
        if (percent < 0 && text.isEmpty()) {
            return false;
        }
        writeWorkerJsonLine(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("progress")},
            {QStringLiteral("job_id"), snapshot.jobId},
            {QStringLiteral("stage"), QStringLiteral("render")},
            {QStringLiteral("percent"), percent},
            {QStringLiteral("message"), text},
        });
        QCoreApplication::processEvents();
        return false;
    };

    const VideoExportResult result = VideoExportController::exportPreparedTask(task, progressCallback);

    QJsonObject finishedObject{
        {QStringLiteral("event"), QStringLiteral("finished")},
        {QStringLiteral("job_id"), snapshot.jobId},
        {QStringLiteral("success"), result.success},
    };
    if (result.success) {
        finishedObject.insert(QStringLiteral("output_path"), task.outputPath);
    } else {
        finishedObject.insert(QStringLiteral("error"), result.message);
        finishedObject.insert(QStringLiteral("details"), result.details);
    }
    writeWorkerJsonLine(finishedObject);
    return result.success ? 0 : 1;
    } catch (...) {
        const QString detail = currentExceptionDetail();
        const QString error = QStringLiteral("Unhandled export worker exception.");
        miacode::debug_log::appendFatalMessage(
            QStringLiteral("export/worker_exception"),
            QStringLiteral("%1 details=%2").arg(error, detail)
        );
        writeWorkerJsonLine(QJsonObject{
            {QStringLiteral("event"), QStringLiteral("finished")},
            {QStringLiteral("job_id"), workerJobId},
            {QStringLiteral("success"), false},
            {QStringLiteral("error"), error},
            {QStringLiteral("details"), detail},
        });
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("%1 %2").arg(error, detail);
        }
        return 1;
    }
}

}  // namespace

int main(int argc, char* argv[])
{
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int index = 0; index < argc; ++index) {
        rawArgs.append(QString::fromLocal8Bit(argv[index]));
    }
    const bool cliVideoExportRequested = wantsCliVideoExport(rawArgs);
    const bool cliVideoExportWorkerRequested = wantsCliVideoExportWorker(rawArgs);
    const bool forceOpenGlGraphicsApi = cliVideoExportRequested || cliVideoExportWorkerRequested;
    miacode::debug_options::setDebugModeEnabled(miacode::debug_options::hasDebugArg(rawArgs));
    if (miacode::debug_options::debugModeEnabled()) {
        miacode::debug_log::trimDebugSessionLogsForStartup();
    }
    if (miacode::debug_options::startupTimingEnabled()) {
        miacode::debug_log::initializeStartupTimingLogSession();
    }

    // v2-refactor Phase 0 — confirm libmpv is loaded and log its API
    // version. No functional change; Phase 4 builds the actual video
    // source on top of this.
    miacode::preview::video::probeLibmpvAtStartup();

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
    if (dontCreateNativeWidgetSiblingsEnabled) {
        QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
    }

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
    }

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

    QApplication app(argc, argv);

    // Backend selection. CLI export always forces OpenGL (legacy export pipeline relies on
    // it). Otherwise: honour user's --rhi=<name> if present (and persist for next launch),
    // else fall back to the persisted choice from the prior run, else Qt's platform default.
    QString appliedGraphicsBackend;
    QString graphicsBackendSource;
    if (forceOpenGlGraphicsApi) {
        QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
        appliedGraphicsBackend = QStringLiteral("opengl");
        graphicsBackendSource = QStringLiteral("cli_video_export_force");
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
                .arg(forceOpenGlGraphicsApi ? QStringLiteral("OpenGL") : QStringLiteral("PlatformDefault"))
                .arg(QApplication::testAttribute(Qt::AA_DontCreateNativeWidgetSiblings) ? 1 : 0)
                .arg(cliVideoExportRequested ? 1 : 0)
                .arg(cliVideoExportWorkerRequested ? 1 : 0)
        );
    }
#ifdef Q_OS_WIN
    setWindowsAppUserModelId();
#endif
    app.setApplicationName("MiaCode");
    app.setApplicationVersion(MIACODE_DISPLAY_VERSION_STRING);
    const QIcon appIcon(QStringLiteral(":/icons/app.png"));
    app.setWindowIcon(appIcon);
    app.setStyle(QStyleFactory::create("Fusion"));
    UiTheme::applyApplicationTheme(app);
    logStartupStage("app_style_ready");

    if (UiText::isChineseUi()) {
        QFont zhUiFont;
        for (const QString& family : QStringList{"Microsoft YaHei UI", "Microsoft YaHei", "PingFang SC", "Noto Sans CJK SC"}) {
            zhUiFont.setFamily(family);
            if (zhUiFont.family().compare(family, Qt::CaseInsensitive) == 0) {
                break;
            }
        }
        zhUiFont.setStyleStrategy(QFont::PreferAntialias);
        zhUiFont.setHintingPreference(QFont::PreferNoHinting);
        app.setFont(zhUiFont);
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

    const bool quickShellBetaRequested = wantsQuickShellBeta(app.arguments());
    Q_UNUSED(quickShellBetaRequested);
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
        QuickShellBootstrap quickShellBootstrap(appIcon);
        if (!quickShellBootstrap.start()) {
            QTextStream(stderr) << "Failed to start Quick Shell Beta.\n";
            return 1;
        }
        logStartupStage("quick_shell_bootstrap_started");
        QTimer::singleShot(0, &app, [&logStartupStage]() {
            logStartupStage("event_loop_first_tick");
        });
        appExecElapsed.start();
        exitCode = app.exec();
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
