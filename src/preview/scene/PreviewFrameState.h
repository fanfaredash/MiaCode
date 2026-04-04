#pragma once

#include <QImage>
#include <QVector>

#include "common/MuriRenderOptions.h"
#include "common/MuriTypes.h"
#include "preview/video/PreviewRenderSettings.h"
#include "timeline/TimelineData.h"

#ifdef HAVE_QT_MULTIMEDIA
#include <QVideoFrame>
#endif

namespace miacode::preview::scene {

struct PreviewMediaFrameState {
    QImage mediaFrame;
    QImage retainedVideoFallbackFrame;
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
    double noteFlowSpeed = 1.0;
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
    PreviewRenderState render;
    double playheadSeconds = 0.0;
    double fpsDisplay = 0.0;
    int cpuFallbackCount = 0;
    bool usedGpuRendererThisFrame = false;
};

}  // namespace miacode::preview::scene
