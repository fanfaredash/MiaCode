#include "AppVersion.h"
#include "mainwindow/MainWindow.h"
#include "tools/video_export/VideoExportSnapshot.h"
#include "UiText.h"
#include "UiTheme.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"

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
#include <QSGRendererInterface>

#include <cmath>
#include <cstdio>

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

void addSharedCliDebugOption(QCommandLineParser& parser)
{
    // main() already enables debug mode before CLI dispatch. We still declare
    // this option here so subcommand parsers accept forwarded "--debug".
    parser.addOption(QCommandLineOption(
        QStringLiteral("debug"),
        QStringLiteral("Enable debug mode and debug-only log output.")
    ));
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

int runCliVideoExport(QApplication& app, QString* errorMessage)
{
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
}

int runCliVideoExportWorker(QApplication& app, QString* errorMessage)
{
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

    const VideoExportResult result = VideoExportController::exportPreparedTask(task, nullptr, progressCallback);

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
}

}  // namespace

int main(int argc, char* argv[])
{
    QStringList rawArgs;
    rawArgs.reserve(argc);
    for (int index = 0; index < argc; ++index) {
        rawArgs.append(QString::fromLocal8Bit(argv[index]));
    }
    miacode::debug_options::setDebugModeEnabled(miacode::debug_options::hasDebugArg(rawArgs));
    if (miacode::debug_options::debugModeEnabled()) {
        miacode::debug_log::clearDebugSessionLogs();
    }
    if (miacode::debug_options::startupTimingEnabled()) {
        miacode::debug_log::initializeStartupTimingLogSession();
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
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);
    logStartupStage("surface_format_ready");

    QApplication app(argc, argv);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    logStartupStage("qapplication_constructed");
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

    if (wantsCliVideoExportWorker(app.arguments())) {
        QString cliError;
        const int exitCode = runCliVideoExportWorker(app, &cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "Worker error: " << cliError << "\n";
        }
        return exitCode;
    }

    if (wantsCliVideoExport(app.arguments())) {
        QString cliError;
        const int exitCode = runCliVideoExport(app, &cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "CLI argument error: " << cliError << "\n";
        }
        return exitCode;
    }

    MainWindow window;
    if (!appIcon.isNull()) {
        window.setWindowIcon(appIcon);
    }
    logStartupStage("mainwindow_constructed");
    window.show();
    logStartupStage("mainwindow_show_called");
    QTimer::singleShot(0, &app, [&logStartupStage]() {
        logStartupStage("event_loop_first_tick");
    });
    return app.exec();
}
