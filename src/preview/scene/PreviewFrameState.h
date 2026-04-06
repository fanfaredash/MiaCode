#pragma once

#include <QImage>
#include <QRectF>
#include <QVector>
#include <QtGlobal>

#include "common/PreviewGameplayConfig.h"
#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "preview/video/PreviewRenderSettings.h"
#include "timeline/TimelineData.h"

#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#endif

namespace miacode::preview::scene {

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
};

struct PreviewAssetState {
    QImage outlineImage;
    double layoutRingDiameterRatio = 0.0;
};

struct PreviewRenderState {
    double backgroundBrightnessOuter = 0.30;
    double backgroundBrightnessInner = 0.20;
    double layoutSquareScale = 0.95;
    bool smoothBrightness = false;
    PreviewBackgroundScaleMode backgroundScaleMode = PreviewBackgroundScaleMode::FillCrop;
    double noteFlowSpeed = miacode::preview_gameplay::kPreviewTimingDefaultFlowSpeed;
    bool showDebugInfo = false;
    bool showTimestamp = true;
    bool showObjectStatsHud = false;
};

struct PreviewFrameState {
    QVector<TimelineNoteMarker> noteMarkers;
    MuriAnalysisReport muriAnalysisReport;
    MuriRenderOptions muriRenderOptions;
    PreviewMediaFrameState media;
    PreviewAssetState assets;
    PreviewSkinAssets skin;
    PreviewJudgeOverlayAssets judgeOverlay;
    PreviewJudgeEffectAssets judgeEffect;
    PreviewRenderState render;
    double playheadSeconds = 0.0;
    double fpsDisplay = 0.0;
    int cpuFallbackCount = 0;
    bool usedGpuRendererThisFrame = false;
};

}  // namespace miacode::preview::scene
