#pragma once

#include <QImage>
#include <QRectF>
#include <QVector>
#include <QtGlobal>
#include <QtMath>

#include <limits>
#include <memory>

#include "common/PreviewGameplayConfig.h"
#include "common/PreviewVideoGeometryConfig.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "core/video/PreviewRenderSettings.h"
#include "timeline/TimelineData.h"

#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#endif

namespace miacode::preview::scene {

class PreviewProgressStatsCache;

struct PreviewSkinAssets {
    QImage tapImage;
    QImage tapEachImage;
    QImage tapBreakImage;
    QImage tapExImage;
    QImage holdImage;
    QImage holdOnImage;
    QImage holdOffImage;
    QImage holdEachImage;
    QImage holdEachOnImage;
    QImage holdBreakImage;
    QImage holdBreakOnImage;
    QImage holdExImage;
    QImage starImage;
    QImage starEachImage;
    QImage starBreakImage;
    QImage starBreakDoubleImage;
    QImage starDoubleImage;
    QImage starEachDoubleImage;
    QImage starExImage;
    QImage starExDoubleImage;
    QImage slideTrackImage;
    QImage slideTrackEachImage;
    QImage slideTrackBreakImage;
    QVector<QImage> wifiImages;
    QVector<QImage> wifiEachImages;
    QVector<QImage> wifiBreakImages;
    QImage noteGuideNormalImage;
    QImage noteGuideBreakImage;
    QImage noteGuideEachImage;
    QImage noteGuideEachLine1Image;
    QImage noteGuideEachLine2Image;
    QImage noteGuideEachLine3Image;
    QImage noteGuideEachLine4Image;
    QImage noteGuideHoldEndImage;
    QImage noteGuideHoldEachEndImage;
    QImage noteGuideHoldBreakEndImage;
    QImage noteGuideSlideImage;
    QImage touchCornerImage;
    QImage touchCornerEachImage;
    QImage touchCornerBreakImage;
    QImage touchBorder2Image;
    QImage touchBorder2EachImage;
    QImage touchBorder2BreakImage;
    QImage touchBorder3Image;
    QImage touchBorder3EachImage;
    QImage touchBorder3BreakImage;
    QImage touchPointImage;
    QImage touchPointEachImage;
    QImage touchPointBreakImage;
    QImage touchHold0Image;
    QImage touchHold1Image;
    QImage touchHold2Image;
    QImage touchHold3Image;
    QImage touchHoldBorderImage;
    QImage touchHoldBreak0Image;
    QImage touchHoldBreak1Image;
    QImage touchHoldBreak2Image;
    QImage touchHoldBreak3Image;
    QImage touchHoldBreakBorderImage;
    QImage touchHoldOffImage;
};

struct PreviewJudgeTextSprite {
    QImage image;
    QRectF sourceRect;
};

struct PreviewJudgeDirectionalSpriteSet {
    QImage straightLeftImage;
    QImage straightRightImage;
    QImage circleLeftImage;
    QImage circleRightImage;
    QImage wifiUpImage;
    QImage wifiDownImage;
};

struct PreviewJudgeSimpleTextAssets {
    PreviewJudgeTextSprite normal;
    PreviewJudgeTextSprite breakText;
    PreviewJudgeTextSprite good;
    PreviewJudgeTextSprite great;
    PreviewJudgeTextSprite perfect;
    PreviewJudgeTextSprite cPerfect;
    PreviewJudgeTextSprite miss;
    PreviewJudgeTextSprite fast;
    PreviewJudgeTextSprite late;
};

struct PreviewJudgeOverlayAssets {
    PreviewJudgeSimpleTextAssets simpleText;
    PreviewJudgeDirectionalSpriteSet neutral;
    PreviewJudgeDirectionalSpriteSet fastGood;
    PreviewJudgeDirectionalSpriteSet fastGreat;
    PreviewJudgeDirectionalSpriteSet lateGood;
    PreviewJudgeDirectionalSpriteSet lateGreat;
    PreviewJudgeDirectionalSpriteSet miss;
};

struct PreviewJudgeEffectAssets {
    QImage tapImage;
    QRectF tapSourceRect;
    QImage tapBreakImage;
    QRectF tapBreakSourceRect;
    QImage holdSustainCircleImage;
    QImage touchCircleImage;
    QImage touchPart01Image;
    QImage touchPart02Image;
    QImage fireworkColorBallImage;
    QRectF fireworkColorBallSourceRect;
};

enum class PreviewStageMediaPresentationMode {
    InternalLayer = 0,
    ExternalQuickMediaItem = 1,
};

enum class PreviewExternalStageMediaType {
    None = 0,
    Image = 1,
    Video = 2,
};

struct PreviewMediaFrameState {
    QImage mediaFrame;
    QImage retainedVideoFallbackFrame;
    QImage resolvedStageImage;
    quint64 stageMediaSerial = 0;
    bool resolvedStageImageCacheable = false;
    double resolvedStageImageToImageMs = 0.0;
#ifdef HAVE_QT_MULTIMEDIA
    QVideoFrame videoFrame;
#endif
    bool stageMediaAvailable = false;
    PreviewStageMediaPresentationMode presentationMode = PreviewStageMediaPresentationMode::InternalLayer;
    PreviewExternalStageMediaType externalMediaType = PreviewExternalStageMediaType::None;
    bool externalVideoPlaybackActive = false;
    double externalPlaybackSecond = 0.0;
    double externalClockDeltaSeconds = 0.0;
    qint64 externalVideoFrameAgeMs = -1;
    qint64 externalVideoFrameCountTotal = 0;
    double externalVideoFrameRate = 0.0;
    double externalVideoFrameIntervalAvgMs = 0.0;
    double externalVideoFrameIntervalMaxMs = 0.0;
    qint64 externalVideoFrameStallCount = 0;
    bool externalVideoFrameStalled = false;
};

struct PreviewAssetState {
    QImage outlineImage;
    double layoutRingDiameterRatio = 0.0;
};

struct PreviewRenderState {
    double backgroundBrightnessOuter = miacode::preview_video::kBackgroundBrightnessDefault;
    double backgroundBrightnessInner = miacode::preview_video::kBackgroundBrightnessInnerDefault;
    double layoutSquareScale = miacode::preview_video::kLayoutSquareScaleDefault;
    bool smoothBrightness = miacode::preview_video::kSmoothBrightnessDefault;
    PreviewBackgroundScaleMode backgroundScaleMode = PreviewBackgroundScaleMode::FillCrop;
    double tapFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    double touchFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    bool slideEarlierSecondAndTextOnTop = miacode::preview_gameplay::kPreviewSlideEarlierSecondAndTextOnTop;
    bool showDebugInfo = false;
    bool showTimestamp = true;
    bool showObjectStatsHud = false;
    bool showChartInfoHud = false;
    miacode::preview_gameplay::CenterDisplayMode centerDisplayMode =
        miacode::preview_gameplay::kDefaultCenterDisplayMode;
};

struct PreviewFrameState {
    QVector<TimelineNoteMarker> noteMarkers;
    std::shared_ptr<const PreviewProgressStatsCache> progressStatsCache;
    MuriAnalysisReport muriAnalysisReport;
    MuriRenderOptions muriRenderOptions;
    PreviewMediaFrameState media;
    PreviewAssetState assets;
    PreviewSkinAssets skin;
    PreviewJudgeOverlayAssets judgeOverlay;
    PreviewJudgeEffectAssets judgeEffect;
    PreviewRenderState render;
    // Source chart metadata for the optional "show chart info" HUD shown
    // top-left during export / export-preview. Lines render in order:
    //   chartTitle / chartArtist / chartDifficultyLabel / chartDesigner
    // Empty fields are skipped. chartDifficultyLabel is pre-formatted as
    // e.g. "MAS 13+" by the producer so the painter doesn't need the
    // raw difficulty id + level lookups.
    QString chartTitle;
    QString chartArtist;
    QString chartDifficultyLabel;
    QString chartDesigner;
    double playheadSeconds = 0.0;
    // Optional HUD-only override. When finite, the HUD timestamp/stats use
    // this value instead of `playheadSeconds`; the scene graph and note
    // layers always read `playheadSeconds`. Used by the full-range video
    // export so the HUD can count down through the lead-in (negative
    // seconds) while the scene stays clamped at chart time 0.
    double hudPlayheadSecondsOverride = std::numeric_limits<double>::quiet_NaN();
    quint64 sceneContentRevision = 0;
    double fpsDisplay = 0.0;
    double tickFpsDisplay = 0.0;
    double updateRequestFpsDisplay = 0.0;
    // Smoothness-perception metrics (window-scoped, same window as the FPS
    // averages). Average FPS hides spiky frames; these surface them.
    //  - *MaxMsDisplay   : worst single inter-event interval in the window.
    //  - *StutterCountDisplay : count of intervals in the window that
    //                           exceeded `kStutterMultiplier × target_ms`.
    // Read by PreviewQuickHudLayer; populated by PreviewRuntime after each
    // recordIntervalSample() call.
    double presentMaxMsDisplay = 0.0;
    double tickMaxMsDisplay = 0.0;
    double updateRequestMaxMsDisplay = 0.0;
    int presentStutterCountDisplay = 0;
    int tickStutterCountDisplay = 0;
    int updateRequestStutterCountDisplay = 0;
    double framePacingTargetFps = 0.0;
    double displayRefreshRate = 0.0;
    qint64 tickCount = 0;
    qint64 updateRequestCount = 0;
    qint64 presentedFrameCount = 0;
    bool framePacingUsesDisplayRefresh = false;
    int cpuFallbackCount = 0;
    bool usedGpuRendererThisFrame = false;
};

}  // namespace miacode::preview::scene
