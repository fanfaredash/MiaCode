#include "MainEntrypoints.h"

#include "runtime/Session.h"
#include "app/v2/ApplicationServices.h"
#include "tools/video_export/VideoExportSnapshot.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <cmath>

namespace {

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

}  // namespace

namespace miacode::app::entry {

int runCliVideoExport(QApplication& app, QString* errorMessage)
{
    MC_OP("runCliVideoExport");
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
        QStringLiteral("show-chart-info"),
        QStringLiteral("Show top-left chart info HUD (title + designer) in output video.")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("intro"),
        QStringLiteral("Prepend the maimai track-start intro (full-range exports only).")
    ));
    parser.addOption(QCommandLineOption(
        QStringLiteral("preview-seconds"),
        QStringLiteral("Dev: render only the first N seconds of output (e.g. to preview "
                       "the intro without rendering the whole chart). 0 = full."),
        QStringLiteral("seconds"),
        QStringLiteral("0")
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
        QStringLiteral("Background scale mode: fill, fit, square_fit, or inner_circle_fit_outer_fill."),
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
    const bool squareFitScaleToken =
        backgroundScaleToken == QStringLiteral("square_fit")
        || backgroundScaleToken == QStringLiteral("square-fit")
        || backgroundScaleToken == QStringLiteral("square_fill")
        || backgroundScaleToken == QStringLiteral("square-fill")
        || backgroundScaleToken == QStringLiteral("square");
    const bool innerCircleFitOuterFillScaleToken =
        backgroundScaleToken == QStringLiteral("inner_circle_fit_outer_fill")
        || backgroundScaleToken == QStringLiteral("inner-circle-fit-outer-fill")
        || backgroundScaleToken == QStringLiteral("inner_fit_outer_fill")
        || backgroundScaleToken == QStringLiteral("inner-fit-outer-fill")
        || backgroundScaleToken == QStringLiteral("circle_fit_outer_fill")
        || backgroundScaleToken == QStringLiteral("circle-fit-outer-fill")
        || backgroundScaleToken == QStringLiteral("inner_circle_fit")
        || backgroundScaleToken == QStringLiteral("inner-circle-fit");
    if (backgroundScaleToken != QStringLiteral("fill")
        && backgroundScaleToken != QStringLiteral("fit")
        && !squareFitScaleToken
        && !innerCircleFitOuterFillScaleToken) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("--background-scale must be fill, fit, square_fit, or inner_circle_fit_outer_fill");
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

    Session::CliVideoExportRequest request;
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
    request.showChartInfoHud = parser.isSet(QStringLiteral("show-chart-info"));
    request.addIntro = parser.isSet(QStringLiteral("intro"));
    {
        bool previewOk = false;
        const double previewSeconds = parser.value(QStringLiteral("preview-seconds")).toDouble(&previewOk);
        request.previewMaxOutputSeconds = (previewOk && previewSeconds > 0.0) ? previewSeconds : 0.0;
    }
    request.smoothBrightness = parser.isSet(QStringLiteral("smooth-brightness"));
    request.backgroundBrightnessOuter = outerBrightness;
    request.backgroundBrightnessInner = innerBrightness;
    request.layoutSquareScale = layoutSquareScale;
    request.backgroundScaleMode = innerCircleFitOuterFillScaleToken
        ? PreviewBackgroundScaleMode::InnerCircleFitOuterFill
        : (squareFitScaleToken
               ? PreviewBackgroundScaleMode::SquareFitContain
               : (backgroundScaleToken == QStringLiteral("fit")
                      ? PreviewBackgroundScaleMode::FitContain
                      : PreviewBackgroundScaleMode::FillCrop));
    request.noteFlowSpeed = miacode::preview_gameplay::normalizePreviewTimingFlowSpeed(flowSpeed);
    request.touchFlowSpeed = request.noteFlowSpeed;
    request.skinLoadWaitMs = skinWaitMs;

    // The CLI export path builds the same application services the shell does:
    // they own the document domain and the job/UI boundaries, and the window
    // only borrows them (stage 3.5 item 1).
    miacode::v2::ApplicationServices applicationServices;
    Session window(applicationServices);
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

}  // namespace miacode::app::entry
