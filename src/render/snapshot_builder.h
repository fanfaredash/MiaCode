#pragma once

// Phase 2 helper. Hosts the small "push a batch of N descriptors of
// type T into the snapshot, and append a DrawBatch entry recording
// the contiguous-range offsets" helpers that PreviewDCompSurface had
// as file-local lambdas. Promoted to free functions so each
// IPreviewSource can mutate the snapshot the same way without copying
// the same 5–10-line logic into every source.
//
// All functions append-only — they never mutate or remove existing
// snapshot data. This is the contract IPreviewSource implementations
// rely on for idempotent behaviour: multiple compositor walks on the
// same context produce the same snapshot delta.

#include "core/scene/PreviewArcDescriptor.h"
#include "core/scene/PreviewCircleDescriptor.h"
#include "core/scene/PreviewJudgeFireworkLayerState.h"
#include "core/scene/PreviewSpriteDescriptor.h"
#include "render/backend_d3d11/PreviewDCompFrameStateSnapshot.h"

#include <QImage>
#include <QSharedPointer>
#include <QVector>

namespace miacode::render::snapshot_builder {

inline void pushSpriteBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const miacode::preview::scene::PreviewSpriteDescriptors& sprites)
{
    if (sprites.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::Sprites;
    batch.firstIndex = static_cast<qint32>(snapshot.sprites.size());
    batch.count = static_cast<qint32>(sprites.size());
    snapshot.sprites.append(sprites);
    snapshot.batches.append(batch);
}

inline void pushCircleBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const miacode::preview::scene::PreviewCircleDescriptors& circles)
{
    if (circles.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::Circles;
    batch.firstIndex = static_cast<qint32>(snapshot.circles.size());
    batch.count = static_cast<qint32>(circles.size());
    snapshot.circles.append(circles);
    snapshot.batches.append(batch);
}

inline void pushArcBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const miacode::preview::scene::PreviewArcDescriptors& arcs)
{
    if (arcs.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::Arcs;
    batch.firstIndex = static_cast<qint32>(snapshot.arcs.size());
    batch.count = static_cast<qint32>(arcs.size());
    snapshot.arcs.append(arcs);
    snapshot.batches.append(batch);
}

inline void pushFireworkBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const miacode::preview::scene::PreviewJudgeFireworkLayerState& fw)
{
    if (!fw.active) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::Fireworks;
    batch.firstIndex = static_cast<qint32>(snapshot.fireworks.size());
    batch.count = 1;
    snapshot.fireworks.append(fw);
    snapshot.batches.append(batch);
}

inline void appendOwnedImages(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const QVector<QSharedPointer<QImage>>& images)
{
    for (const QSharedPointer<QImage>& image : images) {
        snapshot.retainedImages.append(image);
    }
}

// Phase 2d — timeline batch pushers. Same shape as the chart pushers:
// append the descriptor list to the matching flat vector and emit a
// single DrawBatch entry pointing at the contiguous range. The render
// thread's batch switch currently skips these; Phase 3 wires up GPU
// pipelines to consume them.

inline void pushTimelineRectBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const QVector<miacode::timeline::TimelineSceneRect>& rects)
{
    if (rects.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::TimelineRects;
    batch.firstIndex = static_cast<qint32>(snapshot.timelineRects.size());
    batch.count = static_cast<qint32>(rects.size());
    snapshot.timelineRects.append(rects);
    snapshot.batches.append(batch);
}

inline void pushTimelineLineBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const QVector<miacode::timeline::TimelineSceneLine>& lines)
{
    if (lines.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::TimelineLines;
    batch.firstIndex = static_cast<qint32>(snapshot.timelineLines.size());
    batch.count = static_cast<qint32>(lines.size());
    snapshot.timelineLines.append(lines);
    snapshot.batches.append(batch);
}

inline void pushTimelineTriangleBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const QVector<miacode::timeline::TimelineSceneTriangle>& triangles)
{
    if (triangles.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::TimelineTriangles;
    batch.firstIndex = static_cast<qint32>(snapshot.timelineTriangles.size());
    batch.count = static_cast<qint32>(triangles.size());
    snapshot.timelineTriangles.append(triangles);
    snapshot.batches.append(batch);
}

inline void pushTimelineTextLabelBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const QVector<miacode::timeline::TimelineSceneTextLabel>& labels)
{
    if (labels.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::TimelineTextLabels;
    batch.firstIndex = static_cast<qint32>(snapshot.timelineTextLabels.size());
    batch.count = static_cast<qint32>(labels.size());
    snapshot.timelineTextLabels.append(labels);
    snapshot.batches.append(batch);
}

inline void pushTimelineGlyphBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const QVector<miacode::timeline::TimelineSceneGlyph>& glyphs)
{
    if (glyphs.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::TimelineGlyphs;
    batch.firstIndex = static_cast<qint32>(snapshot.timelineGlyphs.size());
    batch.count = static_cast<qint32>(glyphs.size());
    snapshot.timelineGlyphs.append(glyphs);
    snapshot.batches.append(batch);
}

inline void pushTimelineSpriteBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const QVector<miacode::timeline::TimelineSceneSprite>& sprites)
{
    if (sprites.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::TimelineSprites;
    batch.firstIndex = static_cast<qint32>(snapshot.timelineSprites.size());
    batch.count = static_cast<qint32>(sprites.size());
    snapshot.timelineSprites.append(sprites);
    snapshot.batches.append(batch);
}

inline void pushTimelineHoldSpanBatch(
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot& snapshot,
    const QVector<miacode::timeline::TimelineSceneHoldSpan>& spans)
{
    if (spans.isEmpty()) return;
    miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::DrawBatch batch;
    batch.type = miacode::preview::dcomp::PreviewDCompFrameStateSnapshot::BatchType::TimelineHoldSpans;
    batch.firstIndex = static_cast<qint32>(snapshot.timelineHoldSpans.size());
    batch.count = static_cast<qint32>(spans.size());
    snapshot.timelineHoldSpans.append(spans);
    snapshot.batches.append(batch);
}

}  // namespace miacode::render::snapshot_builder
