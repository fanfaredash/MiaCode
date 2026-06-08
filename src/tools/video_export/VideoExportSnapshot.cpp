#include "VideoExportSnapshot.h"

#include "SimaiDocument.h"
#include "SimaiNativeParser.h"
#include "common/ChartClockCount.h"
#include "common/ChartAssetPaths.h"
#include "tools/muri/MuriAnalyzer.h"

#include <QDir>
#include <QFileInfo>
#include <QtMath>

namespace {

QString backgroundScaleModeToken(PreviewBackgroundScaleMode mode)
{
    switch (mode) {
    case PreviewBackgroundScaleMode::FitContain:
        return QStringLiteral("fit");
    case PreviewBackgroundScaleMode::SquareFitContain:
        return QStringLiteral("square_fit");
    case PreviewBackgroundScaleMode::FillCrop:
    default:
        return QStringLiteral("fill");
    }
}

QString outlineVariantToken(PreviewOutlineVariant variant)
{
    switch (variant) {
    case PreviewOutlineVariant::Point:
        return QStringLiteral("point");
    case PreviewOutlineVariant::JudgeArea:
        return QStringLiteral("judge_area");
    case PreviewOutlineVariant::JudgeAreaLabeled:
        return QStringLiteral("judge_area_labeled");
    case PreviewOutlineVariant::Line:
    default:
        return QStringLiteral("line");
    }
}

PreviewOutlineVariant outlineVariantFromToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QLatin1String("point")) {
        return PreviewOutlineVariant::Point;
    }
    if (normalized == QLatin1String("judge_area")
        || normalized == QLatin1String("judgearea")
        || normalized == QLatin1String("area")) {
        return PreviewOutlineVariant::JudgeArea;
    }
    if (normalized == QLatin1String("judge_area_labeled")
        || normalized == QLatin1String("judgearea_labeled")
        || normalized == QLatin1String("judge_area_numbered")
        || normalized == QLatin1String("numbered_area")
        || normalized == QLatin1String("area_labeled")) {
        return PreviewOutlineVariant::JudgeAreaLabeled;
    }
    return PreviewOutlineVariant::Line;
}

QString videoExportPresetToken(VideoExportPreset preset)
{
    switch (preset) {
    case VideoExportPreset::HighQuality:
        return QStringLiteral("high_quality");
    case VideoExportPreset::Fast:
    default:
        return QStringLiteral("fast");
    }
}

VideoExportPreset videoExportPresetFromToken(const QString& token)
{
    if (token.compare(QStringLiteral("high_quality"), Qt::CaseInsensitive) == 0) {
        return VideoExportPreset::HighQuality;
    }
    if (token.compare(QStringLiteral("high_compression"), Qt::CaseInsensitive) == 0) {
        return VideoExportPreset::HighQuality;
    }
    if (token.compare(QStringLiteral("fast"), Qt::CaseInsensitive) == 0) {
        return VideoExportPreset::Fast;
    }
    // Unknown/missing token -> the safe default (synchronous readback, no
    // tearing), matching the dialog's default export-quality choice.
    return VideoExportPreset::HighQuality;
}

PreviewBackgroundScaleMode backgroundScaleModeFromToken(const QString& token)
{
    const QString normalized = token.trimmed().toLower();
    if (normalized == QLatin1String("fit") || normalized == QLatin1String("contain")) {
        return PreviewBackgroundScaleMode::FitContain;
    }
    if (normalized == QLatin1String("square_fit")
        || normalized == QLatin1String("square-fit")
        || normalized == QLatin1String("square_fill")
        || normalized == QLatin1String("square-fill")
        || normalized == QLatin1String("square")) {
        return PreviewBackgroundScaleMode::SquareFitContain;
    }
    return PreviewBackgroundScaleMode::FillCrop;
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
    const double value = trimmed.isEmpty() ? 0.0 : trimmed.toDouble(&ok);
    return (trimmed.isEmpty() || ok) ? value : 0.0;
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
    resources.insert(QStringLiteral("outline_image_path"), outlineImagePath);
    root.insert(QStringLiteral("resources"), resources);

    QJsonObject render;
    render.insert(QStringLiteral("background_brightness_outer"), backgroundBrightnessOuter);
    render.insert(QStringLiteral("background_brightness_inner"), backgroundBrightnessInner);
    render.insert(QStringLiteral("layout_square_scale"), layoutSquareScale);
    render.insert(QStringLiteral("smooth_brightness"), smoothBrightness);
    render.insert(QStringLiteral("outline_variant"), outlineVariantToken(outlineVariant));
    render.insert(QStringLiteral("background_scale_mode"), backgroundScaleModeToken(backgroundScaleMode));
    render.insert(QStringLiteral("tap_flow_speed"), tapFlowSpeed);
    render.insert(QStringLiteral("touch_flow_speed"), touchFlowSpeed);
    render.insert(QStringLiteral("slide_earlier_second_and_text_on_top"), slideEarlierSecondAndTextOnTop);
    render.insert(QStringLiteral("render_mode"), renderModeToken(muriRenderOptions.renderMode));
    render.insert(QStringLiteral("show_slide_tracks"), muriRenderOptions.showSlideTracks);
    render.insert(QStringLiteral("show_judge_markers"), muriRenderOptions.showJudgeMarkers);
    render.insert(QStringLiteral("show_touch_trail"), muriRenderOptions.showTouchTrail);
    render.insert(
        QStringLiteral("show_chart_review_slide_judge_overlay"),
        muriRenderOptions.showChartReviewSlideJudgeOverlay
    );
    render.insert(
        QStringLiteral("show_chart_review_tap_judge_overlay"),
        muriRenderOptions.showChartReviewTapJudgeOverlay
    );
    render.insert(
        QStringLiteral("show_chart_review_touch_judge_overlay"),
        muriRenderOptions.showChartReviewTouchJudgeOverlay
    );
    render.insert(QStringLiteral("wifi_need_c"), muriRenderOptions.wifiNeedC);
    render.insert(QStringLiteral("static_tap_on_slide_threshold_seconds"), staticTapOnSlideThresholdSeconds);
    render.insert(QStringLiteral("show_timestamp"), showTimestamp);
    render.insert(QStringLiteral("show_object_stats_hud"), showObjectStatsHud);
    render.insert(QStringLiteral("show_chart_info_hud"), showChartInfoHud);
    render.insert(
        QStringLiteral("center_display_mode"),
        QString::fromLatin1(miacode::preview_gameplay::centerDisplayModeToken(centerDisplayMode))
    );
    render.insert(QStringLiteral("skin_wait_ms"), skinLoadWaitMs);
    root.insert(QStringLiteral("render"), render);

    root.insert(QStringLiteral("audio"), audioSettings.toJson());
    root.insert(QStringLiteral("timing"), timingSettings.toJson());

    QJsonObject exportObject;
    exportObject.insert(QStringLiteral("start_seconds"), exportStartSeconds);
    exportObject.insert(QStringLiteral("duration_seconds"), contentDurationSeconds);
    exportObject.insert(QStringLiteral("output_width"), outputWidth);
    exportObject.insert(QStringLiteral("output_height"), outputHeight);
    exportObject.insert(QStringLiteral("fps"), fps);
    exportObject.insert(QStringLiteral("preset"), videoExportPresetToken(preset));
    exportObject.insert(QStringLiteral("full_range_export"), fullRangeExport);
    exportObject.insert(QStringLiteral("output_path"), outputPath);
    root.insert(QStringLiteral("export"), exportObject);

    QJsonObject introObject;
    introObject.insert(QStringLiteral("enabled"), intro.enabled);
    introObject.insert(QStringLiteral("title"), intro.title);
    introObject.insert(QStringLiteral("artist"), intro.artist);
    introObject.insert(QStringLiteral("designer"), intro.designer);
    introObject.insert(QStringLiteral("level"), intro.level);
    introObject.insert(QStringLiteral("difficulty"), intro.difficulty);
    introObject.insert(QStringLiteral("bpm"), intro.bpm);
    introObject.insert(QStringLiteral("mode"), intro.mode);
    introObject.insert(QStringLiteral("lv_render_mode"), intro.lvRenderMode);
    introObject.insert(QStringLiteral("jacket_path"), intro.jacketPath);
    root.insert(QStringLiteral("intro"), introObject);
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
    parsed.outlineImagePath = jsonString(resources, "outline_image_path");

    const QJsonObject render = object.value(QStringLiteral("render")).toObject();
    parsed.backgroundBrightnessOuter =
        render.value(QStringLiteral("background_brightness_outer")).toDouble(parsed.backgroundBrightnessOuter);
    parsed.backgroundBrightnessInner =
        render.value(QStringLiteral("background_brightness_inner")).toDouble(parsed.backgroundBrightnessInner);
    parsed.layoutSquareScale =
        render.value(QStringLiteral("layout_square_scale")).toDouble(parsed.layoutSquareScale);
    parsed.smoothBrightness =
        render.value(QStringLiteral("smooth_brightness")).toBool(parsed.smoothBrightness);
    parsed.outlineVariant =
        outlineVariantFromToken(render.value(QStringLiteral("outline_variant")).toString());
    parsed.backgroundScaleMode =
        backgroundScaleModeFromToken(render.value(QStringLiteral("background_scale_mode")).toString());
    const double legacyFlowSpeed = render.value(QStringLiteral("note_flow_speed")).toDouble(
        miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed
    );
    parsed.tapFlowSpeed = render.value(QStringLiteral("tap_flow_speed")).toDouble(
        render.contains(QStringLiteral("note_flow_speed")) ? legacyFlowSpeed : parsed.tapFlowSpeed
    );
    parsed.touchFlowSpeed = render.value(QStringLiteral("touch_flow_speed")).toDouble(
        render.contains(QStringLiteral("note_flow_speed")) ? legacyFlowSpeed : parsed.touchFlowSpeed
    );
    parsed.slideEarlierSecondAndTextOnTop =
        render.value(QStringLiteral("slide_earlier_second_and_text_on_top"))
            .toBool(parsed.slideEarlierSecondAndTextOnTop);
    parsed.muriRenderOptions.renderMode =
        renderModeFromToken(render.value(QStringLiteral("render_mode")).toString());
    parsed.muriRenderOptions.showSlideTracks =
        render.value(QStringLiteral("show_slide_tracks")).toBool(parsed.muriRenderOptions.showSlideTracks);
    parsed.muriRenderOptions.showJudgeMarkers =
        render.value(QStringLiteral("show_judge_markers")).toBool(parsed.muriRenderOptions.showJudgeMarkers);
    parsed.muriRenderOptions.showTouchTrail =
        render.value(QStringLiteral("show_touch_trail")).toBool(parsed.muriRenderOptions.showTouchTrail);
    parsed.muriRenderOptions.showChartReviewSlideJudgeOverlay =
        render.value(QStringLiteral("show_chart_review_slide_judge_overlay"))
            .toBool(parsed.muriRenderOptions.showChartReviewSlideJudgeOverlay);
    parsed.muriRenderOptions.showChartReviewTapJudgeOverlay =
        render.value(QStringLiteral("show_chart_review_tap_judge_overlay"))
            .toBool(parsed.muriRenderOptions.showChartReviewTapJudgeOverlay);
    parsed.muriRenderOptions.showChartReviewTouchJudgeOverlay =
        render.value(QStringLiteral("show_chart_review_touch_judge_overlay"))
            .toBool(parsed.muriRenderOptions.showChartReviewTouchJudgeOverlay);
    parsed.muriRenderOptions.wifiNeedC =
        render.value(QStringLiteral("wifi_need_c")).toBool(parsed.muriRenderOptions.wifiNeedC);
    parsed.muriRenderOptions.excludeTouchFromMultiTouch = true;
    parsed.staticTapOnSlideThresholdSeconds =
        render.value(QStringLiteral("static_tap_on_slide_threshold_seconds"))
            .toDouble(parsed.staticTapOnSlideThresholdSeconds);
    parsed.showTimestamp =
        render.value(QStringLiteral("show_timestamp")).toBool(parsed.showTimestamp);
    parsed.showObjectStatsHud =
        render.value(QStringLiteral("show_object_stats_hud")).toBool(parsed.showObjectStatsHud);
    parsed.showChartInfoHud =
        render.value(QStringLiteral("show_chart_info_hud")).toBool(parsed.showChartInfoHud);
    parsed.centerDisplayMode = miacode::preview_gameplay::centerDisplayModeFromToken(
        render.value(QStringLiteral("center_display_mode")).toString(
            QString::fromLatin1(miacode::preview_gameplay::centerDisplayModeToken(parsed.centerDisplayMode))));
    parsed.skinLoadWaitMs =
        render.value(QStringLiteral("skin_wait_ms")).toInt(parsed.skinLoadWaitMs);

    parsed.audioSettings = PreviewAudioSettings::fromJson(object.value(QStringLiteral("audio")).toObject());
    parsed.audioSettings.normalize();
    parsed.timingSettings = PreviewTimingSettings::fromJson(object.value(QStringLiteral("timing")).toObject());
    parsed.timingSettings.normalize();

    const QJsonObject exportObject = object.value(QStringLiteral("export")).toObject();
    parsed.exportStartSeconds = exportObject.value(QStringLiteral("start_seconds")).toDouble(parsed.exportStartSeconds);
    parsed.contentDurationSeconds = exportObject.value(QStringLiteral("duration_seconds")).toDouble(parsed.contentDurationSeconds);
    parsed.outputWidth = exportObject.value(QStringLiteral("output_width")).toInt(parsed.outputWidth);
    parsed.outputHeight = exportObject.value(QStringLiteral("output_height")).toInt(parsed.outputHeight);
    parsed.fps = exportObject.value(QStringLiteral("fps")).toInt(parsed.fps);
    parsed.preset = videoExportPresetFromToken(exportObject.value(QStringLiteral("preset")).toString());
    parsed.fullRangeExport = exportObject.value(QStringLiteral("full_range_export")).toBool(parsed.fullRangeExport);
    parsed.outputPath = exportObject.value(QStringLiteral("output_path")).toString();

    const QJsonObject introObject = object.value(QStringLiteral("intro")).toObject();
    parsed.intro.enabled = introObject.value(QStringLiteral("enabled")).toBool(parsed.intro.enabled);
    parsed.intro.title = introObject.value(QStringLiteral("title")).toString();
    parsed.intro.artist = introObject.value(QStringLiteral("artist")).toString();
    parsed.intro.designer = introObject.value(QStringLiteral("designer")).toString();
    parsed.intro.level = introObject.value(QStringLiteral("level")).toString();
    parsed.intro.difficulty = introObject.value(QStringLiteral("difficulty")).toString(parsed.intro.difficulty);
    parsed.intro.bpm = introObject.value(QStringLiteral("bpm")).toString();
    parsed.intro.mode = introObject.value(QStringLiteral("mode")).toString(parsed.intro.mode);
    parsed.intro.lvRenderMode = introObject.value(QStringLiteral("lv_render_mode")).toString(parsed.intro.lvRenderMode);
    parsed.intro.jacketPath = introObject.value(QStringLiteral("jacket_path")).toString();

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

    const miacode::simai::SimaiTimingMetadata timingMetadata = miacode::simai::buildTimingMetadata(document);
    const SimaiNativeParseResult nativeResult = SimaiNativeParser::parseForTimeline(
        difficulty->chart,
        timingMetadata);
    const double firstSeconds = parsedFirstSeconds(document.first);

    VideoExportTask built;
    built.outputPath = snapshot.outputPath;
    built.chartPath = snapshot.originalChartPath;
    built.backgroundMediaPath = miacode::chart_assets::resolvePreferredBackgroundMediaPath(
        snapshot.originalChartPath,
        snapshot.backgroundMediaPath
    );
    built.trackPath = snapshot.trackPath;
    built.skinDirectory = snapshot.skinDirectory;
    built.noteMarkers = shiftedNoteMarkers(nativeResult.noteMarkers, firstSeconds);
    built.audioSettings = snapshot.audioSettings;
    built.audioSettings.normalize();
    built.timingSettings = snapshot.timingSettings;
    built.timingSettings.normalize();
    built.backgroundBrightnessOuter = snapshot.backgroundBrightnessOuter;
    built.backgroundBrightnessInner = snapshot.backgroundBrightnessInner;
    built.layoutSquareScale = snapshot.layoutSquareScale;
    built.smoothBrightness = snapshot.smoothBrightness;
    built.outlineVariant = snapshot.outlineVariant;
    built.outlineImagePath = snapshot.outlineImagePath;
    built.backgroundScaleMode = snapshot.backgroundScaleMode;
    built.tapFlowSpeed = snapshot.tapFlowSpeed;
    built.touchFlowSpeed = snapshot.touchFlowSpeed;
    built.slideEarlierSecondAndTextOnTop = snapshot.slideEarlierSecondAndTextOnTop;
    built.muriRenderOptions = snapshot.muriRenderOptions;
    built.staticTapOnSlideThresholdSeconds = snapshot.staticTapOnSlideThresholdSeconds;
    built.exportStartSeconds = qMax(0.0, snapshot.exportStartSeconds);
    built.contentDurationSeconds = qMax(0.0, snapshot.contentDurationSeconds);
    built.outputWidth = snapshot.outputWidth;
    built.outputHeight = snapshot.outputHeight;
    built.fps = snapshot.fps;
    built.preset = snapshot.preset;
    built.fullRangeExport = snapshot.fullRangeExport;
    built.showTimestamp = snapshot.showTimestamp;
    built.showObjectStatsHud = snapshot.showObjectStatsHud;
    built.showChartInfoHud = snapshot.showChartInfoHud;
    // Reconstruct chart metadata from the parsed SimaiDocument so the
    // out-of-process worker can populate the chart info HUD without
    // shipping the strings separately. Per-difficulty designer takes
    // precedence over the document-level &des field; the difficulty
    // label is composed from the short name + per-difficulty level.
    built.chartTitle = document.title;
    built.chartArtist = document.artist;
    built.chartDesigner = !difficulty->designer.trimmed().isEmpty()
        ? difficulty->designer
        : document.designer;
    const QString diffShortName = SimaiDocument::difficultyShortName(snapshot.difficultyId);
    const QString diffLevel = difficulty->level.trimmed();
    if (!diffShortName.isEmpty() || !diffLevel.isEmpty()) {
        built.chartDifficultyLabel = QStringLiteral("%1 %2")
            .arg(diffShortName, diffLevel)
            .trimmed();
    }
    built.intro = snapshot.intro;
    built.centerDisplayMode = snapshot.centerDisplayMode;
    built.skinLoadWaitMs = qBound(0, snapshot.skinLoadWaitMs, 20000);
    built.clockCount = miacode::chart_clock::clockCountFromDocument(document);
    built.clockBpm = miacode::chart_clock::clockBpmForChart(document, difficulty->chart);
    built.muriAnalysisReport = MuriAnalyzer::analyze(
        built.noteMarkers,
        built.muriRenderOptions,
        built.staticTapOnSlideThresholdSeconds);

    if (built.noteMarkers.isEmpty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("snapshot produced no parsed note markers");
        }
        return false;
    }

    *task = built;
    return true;
}
