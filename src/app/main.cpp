#include "AppVersion.h"
#include "mainwindow/MainWindow.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFile>
#include <QFont>
#include <QIcon>
#include <QTextStream>
#include <QTimer>
#include <QStringList>
#include <QSurfaceFormat>
#include <QStyleFactory>
#include <QDir>
#include <QRegularExpression>

#include <cmath>

namespace {

bool startupTimingEnabled()
{
    static const bool enabled = []() {
        const QString raw = qEnvironmentVariable(
            "MIACODE_ENABLE_STARTUP_TIMING",
            qEnvironmentVariable("MAIMURI_ENABLE_STARTUP_TIMING")
        ).trimmed();
        return raw == "1" || raw.compare("true", Qt::CaseInsensitive) == 0;
    }();
    return enabled;
}

bool wantsCliVideoExport(const QStringList& arguments)
{
    return arguments.contains(QStringLiteral("--export-video"));
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

}  // namespace

int main(int argc, char* argv[])
{
    QElapsedTimer startupTimer;
    startupTimer.start();
    qint64 lastStageMs = 0;
    const QString startupLogPath = QDir::toNativeSeparators(QDir::temp().filePath("miacode_startup_timing.log"));
    if (startupTimingEnabled()) {
        QFile logFile(startupLogPath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            QTextStream out(&logFile);
            out << "timestamp=" << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
            out << "pid=" << QCoreApplication::applicationPid() << "\n";
            out << "log_path=" << startupLogPath << "\n";
        }
    }
    const auto logStartupStage = [&](const QString& stage) {
        if (!startupTimingEnabled()) {
            return;
        }
        const qint64 nowMs = startupTimer.elapsed();
        const qint64 deltaMs = nowMs - lastStageMs;
        lastStageMs = nowMs;
        QFile logFile(startupLogPath);
        if (logFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
            QTextStream out(&logFile);
            out << "stage=" << stage << ", elapsed_ms=" << nowMs << ", delta_ms=" << deltaMs << "\n";
        }
    };
    logStartupStage("process_entry");

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    format.setSwapInterval(1);
    QSurfaceFormat::setDefaultFormat(format);
    logStartupStage("surface_format_ready");

    QApplication app(argc, argv);
    logStartupStage("qapplication_constructed");
    app.setApplicationName("MiaCode");
    app.setApplicationVersion(MIACODE_VERSION_STRING);
    app.setWindowIcon(QIcon(":/icons/app.png"));
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

    if (wantsCliVideoExport(app.arguments())) {
        QString cliError;
        const int exitCode = runCliVideoExport(app, &cliError);
        if (exitCode != 0 && !cliError.trimmed().isEmpty()) {
            QTextStream(stderr) << "CLI argument error: " << cliError << "\n";
        }
        return exitCode;
    }

    MainWindow window;
    logStartupStage("mainwindow_constructed");
    window.show();
    logStartupStage("mainwindow_show_called");
    QTimer::singleShot(0, &app, [&logStartupStage]() {
        logStartupStage("event_loop_first_tick");
    });
    return app.exec();
}

