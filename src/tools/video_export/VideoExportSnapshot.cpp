#include "VideoExportSnapshot.h"

#include "SimaiDocument.h"
#include "SimaiNativeParser.h"
#include "tools/muri/MuriAnalyzer.h"

#include <QDir>
#include <QFileInfo>
#include <QtMath>

namespace {

QString backgroundScaleModeToken(PreviewBackgroundScaleMode mode)
{
    return mode == PreviewBackgroundScaleMode::FitContain
        ? QStringLiteral("fit")
        : QStringLiteral("fill");
}

PreviewBackgroundScaleMode backgroundScaleModeFromToken(const QString& token)
{
    return token.trimmed().compare(QStringLiteral("fit"), Qt::CaseInsensitive) == 0
        ? PreviewBackgroundScaleMode::FitContain
        : PreviewBackgroundScaleMode::FillCrop;
}

QString performanceProfileToken(VideoExportPerformanceProfile profile)
{
    switch (profile) {
    case VideoExportPerformanceProfile::Speed:
        return QStringLiteral("speed");
    case VideoExportPerformanceProfile::Balanced:
    default:
        return QStringLiteral("balanced");
    }
}

VideoExportPerformanceProfile performanceProfileFromToken(const QString& token)
{
    return token.trimmed().compare(QStringLiteral("speed"), Qt::CaseInsensitive) == 0
        ? VideoExportPerformanceProfile::Speed
        : VideoExportPerformanceProfile::Balanced;
}

QString renderModeToken(RenderMode mode)
{
    return mode == RenderMode::MaimuriDxStyle
        ? QStringLiteral("maimuri_dx_style")
        : QStringLiteral("native");
}

RenderMode renderModeFromToken(const QString& token)
{
    return token.trimmed().compare(QStringLiteral("maimuri_dx_style"), Qt::CaseInsensitive) == 0
        ? RenderMode::MaimuriDxStyle
        : RenderMode::Native;
}

double parsedFirstSeconds(const QString& rawValue)
{
    bool ok = false;
    const QString trimmed = rawValue.trimmed();
    if (trimmed.isEmpty()) {
        return 0.0;
    }
    const double value = trimmed.toDouble(&ok);
    return ok ? value : 0.0;
}

double shiftedTimelineSecond(double second, double offsetSeconds)
{
    return second + offsetSeconds;
}

QVector<TimelineNoteMarker> shiftedNoteMarkers(
    const QVector<TimelineNoteMarker>& noteMarkers,
    double offsetSeconds
)
{
    QVector<TimelineNoteMarker> shifted = noteMarkers;
    for (TimelineNoteMarker& marker : shifted) {
        marker.second = shiftedTimelineSecond(marker.second, offsetSeconds);
        if (marker.endSecond >= 0.0) {
            marker.endSecond = shiftedTimelineSecond(marker.endSecond, offsetSeconds);
        }
        if (marker.slideTraceSecond >= 0.0) {
            marker.slideTraceSecond = shiftedTimelineSecond(marker.slideTraceSecond, offsetSeconds);
        }
        if (marker.availableSecond >= 0.0) {
            marker.availableSecond = shiftedTimelineSecond(marker.availableSecond, offsetSeconds);
        }
        for (double& shootSecond : marker.slideSegmentShootSeconds) {
            shootSecond = shiftedTimelineSecond(shootSecond, offsetSeconds);
        }
    }
    return shifted;
}

QString jsonString(const QJsonObject& object, const char* key)
{
    return object.value(QLatin1String(key)).toString();
}

}  // namespace

QJsonObject VideoExportSnapshot::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("schema"), schema);
    root.insert(QStringLiteral("job_id"), jobId);
    root.insert(QStringLiteral("created_at_utc"), createdAtUtc);

    QJsonObject chart;
    chart.insert(QStringLiteral("text_utf8"), chartTextUtf8);
    chart.insert(QStringLiteral("difficulty_id"), difficultyId);
    chart.insert(QStringLiteral("difficulty_name"), difficultyName);
    chart.insert(QStringLiteral("original_chart_path"), originalChartPath);
    chart.insert(QStringLiteral("project_dir"), projectDir);
    root.insert(QStringLiteral("chart"), chart);

    QJsonObject resources;
    resources.insert(QStringLiteral("track_path"), trackPath);
    resources.insert(QStringLiteral("background_media_path"), backgroundMediaPath);
    resources.insert(QStringLiteral("skin_dir"), skinDirectory);
    root.insert(QStringLiteral("resources"), resources);

    QJsonObject render;
    render.insert(QStringLiteral("background_brightness_outer"), backgroundBrightnessOuter);
    render.insert(QStringLiteral("background_brightness_inner"), backgroundBrightnessInner);
    render.insert(QStringLiteral("layout_square_scale"), layoutSquareScale);
    render.insert(QStringLiteral("smooth_brightness"), smoothBrightness);
    render.insert(QStringLiteral("background_scale_mode"), backgroundScaleModeToken(backgroundScaleMode));
    render.insert(QStringLiteral("note_flow_speed"), noteFlowSpeed);
    render.insert(QStringLiteral("render_mode"), renderModeToken(muriRenderOptions.renderMode));
    render.insert(QStringLiteral("show_slide_tracks"), muriRenderOptions.showSlideTracks);
    render.insert(QStringLiteral("show_judge_markers"), muriRenderOptions.showJudgeMarkers);
    render.insert(QStringLiteral("show_touch_trail"), muriRenderOptions.showTouchTrail);
    render.insert(QStringLiteral("show_chart_review_judge_overlay"), muriRenderOptions.showChartReviewJudgeOverlay);
    render.insert(QStringLiteral("wifi_need_c"), muriRenderOptions.wifiNeedC);
    render.insert(QStringLiteral("show_timestamp"), showTimestamp);
    render.insert(QStringLiteral("show_object_stats_hud"), showObjectStatsHud);
    render.insert(QStringLiteral("skin_wait_ms"), skinLoadWaitMs);
    root.insert(QStringLiteral("render"), render);

    root.insert(QStringLiteral("audio"), audioSettings.toJson());

    QJsonObject exportObject;
    exportObject.insert(QStringLiteral("start_seconds"), exportStartSeconds);
    exportObject.insert(QStringLiteral("duration_seconds"), contentDurationSeconds);
    exportObject.insert(QStringLiteral("output_width"), outputWidth);
    exportObject.insert(QStringLiteral("output_height"), outputHeight);
    exportObject.insert(QStringLiteral("fps"), fps);
    exportObject.insert(QStringLiteral("performance_profile"), performanceProfileToken(performanceProfile));
    exportObject.insert(QStringLiteral("output_path"), outputPath);
    root.insert(QStringLiteral("export"), exportObject);
    return root;
}

bool VideoExportSnapshot::fromJson(
    const QJsonObject& object,
    VideoExportSnapshot* snapshot,
    QString* errorMessage
)
{
    if (snapshot == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("snapshot output is null");
        }
        return false;
    }

    VideoExportSnapshot parsed;
    parsed.schema = object.value(QStringLiteral("schema")).toString(parsed.schema);
    if (parsed.schema != QLatin1String("miacode_export_snapshot_v1")) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("unsupported export snapshot schema");
        }
        return false;
    }

    parsed.jobId = object.value(QStringLiteral("job_id")).toString();
    parsed.createdAtUtc = object.value(QStringLiteral("created_at_utc")).toString();

    const QJsonObject chart = object.value(QStringLiteral("chart")).toObject();
    parsed.chartTextUtf8 = chart.value(QStringLiteral("text_utf8")).toString();
    parsed.difficultyId = chart.value(QStringLiteral("difficulty_id")).toInt();
    parsed.difficultyName = chart.value(QStringLiteral("difficulty_name")).toString();
    parsed.originalChartPath = chart.value(QStringLiteral("original_chart_path")).toString();
    parsed.projectDir = chart.value(QStringLiteral("project_dir")).toString();

    const QJsonObject resources = object.value(QStringLiteral("resources")).toObject();
    parsed.trackPath = jsonString(resources, "track_path");
    parsed.backgroundMediaPath = jsonString(resources, "background_media_path");
    parsed.skinDirectory = jsonString(resources, "skin_dir");

    const QJsonObject render = object.value(QStringLiteral("render")).toObject();
    parsed.backgroundBrightnessOuter =
        render.value(QStringLiteral("background_brightness_outer")).toDouble(parsed.backgroundBrightnessOuter);
    parsed.backgroundBrightnessInner =
        render.value(QStringLiteral("background_brightness_inner")).toDouble(parsed.backgroundBrightnessInner);
    parsed.layoutSquareScale =
        render.value(QStringLiteral("layout_square_scale")).toDouble(parsed.layoutSquareScale);
    parsed.smoothBrightness =
        render.value(QStringLiteral("smooth_brightness")).toBool(parsed.smoothBrightness);
    parsed.backgroundScaleMode =
        backgroundScaleModeFromToken(render.value(QStringLiteral("background_scale_mode")).toString());
    parsed.noteFlowSpeed =
        render.value(QStringLiteral("note_flow_speed")).toDouble(parsed.noteFlowSpeed);
    parsed.muriRenderOptions.renderMode =
        renderModeFromToken(render.value(QStringLiteral("render_mode")).toString());
    parsed.muriRenderOptions.showSlideTracks =
        render.value(QStringLiteral("show_slide_tracks")).toBool(parsed.muriRenderOptions.showSlideTracks);
    parsed.muriRenderOptions.showJudgeMarkers =
        render.value(QStringLiteral("show_judge_markers")).toBool(parsed.muriRenderOptions.showJudgeMarkers);
    parsed.muriRenderOptions.showTouchTrail =
        render.value(QStringLiteral("show_touch_trail")).toBool(parsed.muriRenderOptions.showTouchTrail);
    parsed.muriRenderOptions.showChartReviewJudgeOverlay =
        render.value(QStringLiteral("show_chart_review_judge_overlay")).toBool(parsed.muriRenderOptions.showChartReviewJudgeOverlay);
    parsed.muriRenderOptions.wifiNeedC =
        render.value(QStringLiteral("wifi_need_c")).toBool(parsed.muriRenderOptions.wifiNeedC);
    parsed.showTimestamp =
        render.value(QStringLiteral("show_timestamp")).toBool(parsed.showTimestamp);
    parsed.showObjectStatsHud =
        render.value(QStringLiteral("show_object_stats_hud")).toBool(parsed.showObjectStatsHud);
    parsed.skinLoadWaitMs =
        render.value(QStringLiteral("skin_wait_ms")).toInt(parsed.skinLoadWaitMs);

    parsed.audioSettings = PreviewAudioSettings::fromJson(object.value(QStringLiteral("audio")).toObject());
    parsed.audioSettings.normalize();

    const QJsonObject exportObject = object.value(QStringLiteral("export")).toObject();
    parsed.exportStartSeconds = exportObject.value(QStringLiteral("start_seconds")).toDouble(parsed.exportStartSeconds);
    parsed.contentDurationSeconds = exportObject.value(QStringLiteral("duration_seconds")).toDouble(parsed.contentDurationSeconds);
    parsed.outputWidth = exportObject.value(QStringLiteral("output_width")).toInt(parsed.outputWidth);
    parsed.outputHeight = exportObject.value(QStringLiteral("output_height")).toInt(parsed.outputHeight);
    parsed.fps = exportObject.value(QStringLiteral("fps")).toInt(parsed.fps);
    parsed.performanceProfile = performanceProfileFromToken(exportObject.value(QStringLiteral("performance_profile")).toString());
    parsed.outputPath = exportObject.value(QStringLiteral("output_path")).toString();

    if (parsed.chartTextUtf8.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("chart snapshot text is empty");
        }
        return false;
    }
    if (!SimaiDocument::isDifficultyId(parsed.difficultyId)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("difficulty_id is invalid");
        }
        return false;
    }
    if (parsed.outputPath.trimmed().isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("output path is empty");
        }
        return false;
    }

    *snapshot = parsed;
    return true;
}

bool buildVideoExportTaskFromSnapshot(
    const VideoExportSnapshot& snapshot,
    VideoExportTask* task,
    QString* errorMessage
)
{
    if (task == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("task output is null");
        }
        return false;
    }

    const SimaiDocument document = SimaiDocument::fromText(snapshot.chartTextUtf8);
    const SimaiDifficultyData* difficulty = document.difficulty(snapshot.difficultyId);
    if (difficulty == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("snapshot difficulty is missing in chart text");
        }
        return false;
    }

    const SimaiNativeParseResult nativeResult = SimaiNativeParser::parseForTimeline(difficulty->chart);
    const double firstSeconds = parsedFirstSeconds(document.first);

    VideoExportTask built;
    built.outputPath = snapshot.outputPath;
    built.chartPath = snapshot.originalChartPath;
    built.backgroundMediaPath = snapshot.backgroundMediaPath;
    built.trackPath = snapshot.trackPath;
    built.skinDirectory = snapshot.skinDirectory;
    built.noteMarkers = shiftedNoteMarkers(nativeResult.noteMarkers, firstSeconds);
    built.audioSettings = snapshot.audioSettings;
    built.audioSettings.normalize();
    built.backgroundBrightnessOuter = snapshot.backgroundBrightnessOuter;
    built.backgroundBrightnessInner = snapshot.backgroundBrightnessInner;
    built.layoutSquareScale = snapshot.layoutSquareScale;
    built.smoothBrightness = snapshot.smoothBrightness;
    built.backgroundScaleMode = snapshot.backgroundScaleMode;
    built.noteFlowSpeed = snapshot.noteFlowSpeed;
    built.muriRenderOptions = snapshot.muriRenderOptions;
    built.exportStartSeconds = qMax(0.0, snapshot.exportStartSeconds);
    built.contentDurationSeconds = qMax(0.0, snapshot.contentDurationSeconds);
    built.outputWidth = snapshot.outputWidth;
    built.outputHeight = snapshot.outputHeight;
    built.fps = snapshot.fps;
    built.performanceProfile = snapshot.performanceProfile;
    built.showTimestamp = snapshot.showTimestamp;
    built.showObjectStatsHud = snapshot.showObjectStatsHud;
    built.skinLoadWaitMs = qBound(0, snapshot.skinLoadWaitMs, 20000);
    built.muriAnalysisReport = MuriAnalyzer::analyze(built.noteMarkers, built.muriRenderOptions);

    if (built.noteMarkers.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("snapshot produced no parsed note markers");
        }
        return false;
    }

    *task = built;
    return true;
}
