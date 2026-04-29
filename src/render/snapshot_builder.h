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

}  // namespace miacode::render::snapshot_builder
