#pragma once

#include <functional>
#include <QFileInfo>
#include <QString>
#include <QUrl>
#include <QVariantMap>
#include <QVector>

#include "PreviewRenderSettings.h"
#include "PreviewAudioSettings.h"
#include "common/PreviewTimingSettings.h"
#include "timeline/TimelineData.h"
#include "common/MuriConfig.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "tools/video_export/VideoExportRuntimePolicy.h"
enum class VideoExportPreset {
    Fast,
    HighQuality,
};

using VideoExportSizePreset = miacode::video_export::VideoExportSizePreset;

// Pre-roll "track-start" intro banner spec. Populated from the chart in
// buildVideoExportSnapshot; consumed by the export session when `enabled`.
// `enabled` is gated on a full-range export (partial/clip exports never get it).
struct IntroBannerSpec {
    bool enabled = false;
    QString title;
    QString artist;
    QString designer;
    QString level;
    // Atlas/sprite key: one of BASIC / ADVANCED / EXPERT / MASTER / ReMASTER.
    QString difficulty = QStringLiteral("MASTER");
    QString bpm;
    QString mode = QStringLiteral("DX");
    // LV render mode forwarded to MaimaiBannerCard.qml: "atlas" (pre-baked digit
    // sprites, [0-9]/'+' only) or "text" (raw level string in the bundled Heavy
    // font, arbitrary characters). In "atlas" mode the card auto-falls-back to
    // text when the level string carries an unsupported glyph.
    QString lvRenderMode = QStringLiteral("atlas");
    // Resolved background STILL image (never the bg video); empty -> the QML
    // falls back to the miacode logo.
    QString jacketPath;
    // ---- Intro backdrop + card styling (the export dialog's "片头" tab) ----
    // backgroundMode: "jacket" (the chart 曲绘 backdrop) or "custom"
    // (customBackgroundPath replaces the backdrop image; the card's jacket slot
    // still shows the 曲绘). A missing/empty custom path falls back to jacket.
    QString backgroundMode = QStringLiteral("jacket");
    QString customBackgroundPath;
    bool blurBackground = true;
    // Soft drop shadow behind the difficulty card. (The card itself is NOT
    // optional — an intro always carries it.)
    bool cardShadow = false;
    // Difficulty-card custom fonts (absolute paths into the portable font
    // library; empty == the bundled default). Overlaid onto the banner template's
    // `fonts` block on every intro render path (preview + export) via
    // FontLibrary::applyBannerFontOverride, so preview == export. A path absent on
    // another machine falls back to the default at render time.
    QString fontDisplayPath;
    QString fontBodyPath;
};

inline bool isAutoIntroBannerMode(const QString& mode)
{
    return mode.compare(QStringLiteral("auto"), Qt::CaseInsensitive) == 0;
}

inline QString normalizedIntroBannerMode(const QString& mode)
{
    return mode.compare(QStringLiteral("Standard"), Qt::CaseInsensitive) == 0
        ? QStringLiteral("Standard")
        : QStringLiteral("DX");
}

inline QString introBannerModeAbbreviation(const QString& mode)
{
    return normalizedIntroBannerMode(mode) == QStringLiteral("Standard")
        ? QStringLiteral("SD")
        : QStringLiteral("DX");
}

inline QString detectedIntroBannerMode(const QVector<TimelineNoteMarker>& noteMarkers)
{
    for (const TimelineNoteMarker& marker : noteMarkers) {
        const bool touchLike =
            marker.type == QStringLiteral("touch")
            || marker.type == QStringLiteral("touch_hold");
        if (touchLike) {
            return QStringLiteral("DX");
        }

        if (marker.type == QStringLiteral("hold") && marker.isBreak) {
            return QStringLiteral("DX");
        }

        const bool slideLike =
            marker.type == QStringLiteral("slide")
            || marker.type == QStringLiteral("wifi");
        if (slideLike && (marker.trackBreak || marker.slideSegmentKeys.size() > 1)) {
            return QStringLiteral("DX");
        }

        if (marker.isEx || marker.headEx) {
            return QStringLiteral("DX");
        }
    }
    return QStringLiteral("Standard");
}

// Spec -> IntroOverlay.qml contract, shared by the export overlay mount
// (VideoExportQuickRenderBackend::setupIntro) and the export dialog's live
// intro preview so the two can never diverge.
inline QVariantMap introBannerTrackMap(const IntroBannerSpec& intro)
{
    QVariantMap track;
    track.insert(QStringLiteral("title"), intro.title);
    track.insert(QStringLiteral("artist"), intro.artist);
    track.insert(QStringLiteral("designer"), intro.designer);
    track.insert(QStringLiteral("level"), intro.level);
    track.insert(QStringLiteral("difficulty"), intro.difficulty);
    track.insert(QStringLiteral("bpm"), intro.bpm);
    track.insert(QStringLiteral("mode"), intro.mode);
    track.insert(QStringLiteral("lvRenderMode"), intro.lvRenderMode);
    return track;
}

// Carry the dialog-controlled "片头" styling (everything EXCEPT the
// chart-derived payload and `enabled`) onto a freshly rebuilt spec. The export
// snapshot rebuilds the payload from the live document right before launch
// (buildVideoExportSnapshot), which would otherwise silently reset the
// dialog's choices to defaults.
// `mode=auto` is a dialog-only request token: keep the freshly detected mode
// already present on `to` instead of leaking "auto" across the worker snapshot.
inline void copyIntroStyling(const IntroBannerSpec& from, IntroBannerSpec* to)
{
    if (to == nullptr) {
        return;
    }
    if (!isAutoIntroBannerMode(from.mode)) {
        to->mode = normalizedIntroBannerMode(from.mode);
    }
    to->lvRenderMode = from.lvRenderMode;
    to->backgroundMode = from.backgroundMode;
    to->customBackgroundPath = from.customBackgroundPath;
    to->blurBackground = from.blurBackground;
    to->cardShadow = from.cardShadow;
    to->fontDisplayPath = from.fontDisplayPath;
    to->fontBodyPath = from.fontBodyPath;
}

// IntroOverlay root properties beyond the track: custom backdrop URL (empty ->
// the 曲绘 backdrop), blur toggle, card drop shadow.
inline QVariantMap introBannerStyleMap(const IntroBannerSpec& intro)
{
    QUrl backdrop;
    if (intro.backgroundMode == QStringLiteral("custom")
        && !intro.customBackgroundPath.trimmed().isEmpty()
        && QFileInfo::exists(intro.customBackgroundPath)) {
        backdrop = QUrl::fromLocalFile(intro.customBackgroundPath);
    }
    QVariantMap style;
    style.insert(QStringLiteral("backdropImage"), backdrop);
    style.insert(QStringLiteral("backdropBlurEnabled"), intro.blurBackground);
    style.insert(QStringLiteral("cardShadowEnabled"), intro.cardShadow);
    return style;
}

struct VideoExportTask {
    QString outputPath;
    QString chartPath;
    QString backgroundMediaPath;
    QString trackPath;
    QString skinDirectory;
    QVector<TimelineNoteMarker> noteMarkers;
    MuriAnalysisReport muriAnalysisReport;
    MuriRenderOptions muriRenderOptions;
    double staticTapOnSlideThresholdSeconds =
        static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdDefaultMs) / 1000.0;
    PreviewAudioSettings audioSettings;
    PreviewTimingSettings timingSettings;
    double backgroundBrightnessOuter = miacode::preview_video::kBackgroundBrightnessDefault;
    double backgroundBrightnessInner = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    double layoutSquareScale = miacode::preview_video::kLayoutSquareScaleDefault;
    bool smoothBrightness = miacode::preview_video::kSmoothBrightnessDefault;
    PreviewOutlineVariant outlineVariant = PreviewOutlineVariant::Line;
    QString outlineImagePath;
    PreviewBackgroundScaleMode backgroundScaleMode = PreviewBackgroundScaleMode::FillCrop;
    double tapFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    double touchFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    bool slideEarlierSecondAndTextOnTop = miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop;
    PreviewTapJudgeTextDistance tapJudgeTextDistance = PreviewTapJudgeTextDistance::Inner;
    PreviewJudgeEffectStyle judgeEffectStyle = PreviewJudgeEffectStyle::Standard;
    double exportStartSeconds = 0.0;
    double contentDurationSeconds = 0.0;
    int outputWidth = 1024;
    int outputHeight = 1024;
    int fps = 60;
    // Dev/iteration: cap the rendered output to the first N seconds (0 = full).
    // Only the leading frames are rendered and the mux is trimmed (-shortest), so
    // the intro can be previewed without rendering the whole chart. The intro/audio
    // plan is unchanged (still full-range), so timing is identical to a real export.
    // Wired from CLI --preview-seconds.
    double previewMaxOutputSeconds = 0.0;
    // AAC audio bitrate in kbps. Allowed values are surfaced in the
    // export dialog dropdown (128/160/192/256/320). The default of 192
    // is one step above the previous hard-coded 160k baseline — slight
    // quality bump for charts with stereo BGM, transparent for casual
    // phone playback. Routed into ffmpeg as `-b:a <kbps>k`.
    int audioBitrateKbps = 192;
    // Export-quality toggle. HighQuality uses compactness-oriented software
    // tuning; Fast uses pipelined readback and prefers hardware encoding.
    VideoExportPreset preset = VideoExportPreset::HighQuality;
    VideoExportSizePreset sizePreset = VideoExportSizePreset::Standard;
    bool fullRangeExport = true;
    bool showTimestamp = true;
    bool showObjectStatsHud = false;
    bool showChartInfoHud = false;
    bool fixHudTextLayout = false;
    // Pre-roll maimai track-start intro (full-range exports only). intro.enabled
    // is the dialog checkbox before the snapshot is built; the rest of the
    // banner payload is filled by buildVideoExportSnapshot from the chart and
    // round-trips back onto the task via buildVideoExportTaskFromSnapshot.
    IntroBannerSpec intro;
    QString introSoundFileName;
    double introSoundVolume = 1.0;
    // Chart metadata for the optional top-left chart info HUD. Populated
    // from the active SimaiDocument at task-construction time; the worker
    // re-derives these from the snapshot's chart text + difficulty id
    // when running out of process (see buildVideoExportTaskFromSnapshot).
    // chartDifficultyLabel is pre-formatted as e.g. "MAS 13+".
    QString chartTitle;
    QString chartArtist;
    QString chartDifficultyLabel;
    QString chartDesigner;
    miacode::preview_gameplay::CenterDisplayMode centerDisplayMode =
        miacode::preview_gameplay::kDefaultCenterDisplayMode;
    int skinLoadWaitMs = 2000;
    int clockCount = 0;
    // Count-in on/off, independent of the clock_count VALUE above. Default true so
    // CLI / batch / legacy snapshots keep emitting the count-in; the video dialog's
    // "Enable clock_count" checkbox toggles this without touching the value.
    bool clockCountEnabled = true;
    double clockBpm = 0.0;
};

struct VideoExportResult {
    bool success = false;
    QString message;
    QString details;
};

using VideoExportProgressCallback = std::function<bool(int percent, const QString& text)>;

class VideoExportController
{
public:
    static VideoExportResult exportFullPreview(
        const VideoExportTask& task
    );
    static VideoExportResult exportPreparedTask(
        const VideoExportTask& task,
        const VideoExportProgressCallback& progressCallback = {}
    );
};
