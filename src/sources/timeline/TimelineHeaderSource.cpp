#include "sources/timeline/TimelineHeaderSource.h"

#include "core/scene/PreviewSpriteDescriptor.h"
#include "render/snapshot_builder.h"
#include "sources/timeline/TimelineLabelCache.h"

namespace miacode::sources::timeline {

namespace scene = miacode::preview::scene;
namespace dcomp = miacode::preview::dcomp;
namespace sb = miacode::render::snapshot_builder;

TimelineHeaderSource::TimelineHeaderSource(TimelineLabelCache* labelCache)
    : labelCache_(labelCache)
{}

bool TimelineHeaderSource::isEnabled(const render::PreviewBuildContext& ctx) const
{
    return ctx.timelineState != nullptr;
}

void TimelineHeaderSource::contributeToSnapshot(
    const render::PreviewBuildContext& ctx,
    dcomp::PreviewDCompFrameStateSnapshot& snapshot)
{
    const auto* state = ctx.timelineState;
    if (state == nullptr) return;

    // Phase 4d-fix — opaque sidebar mask. Emitted at z=3 (this source)
    // so it draws OVER the notes/sprites at z=2 (TimelineNotesSource).
    // Without this, chart-content that scrolls into the lane-label
    // column (e.g. slide-trace arrows extending past the playfield's
    // left edge) bleeds through the lane numbers.
    //
    // Phase 4d-fix1b — pre-bake +horizontalScrollValue into the mask's
    // X coord because the TimelineRects pipeline applies a unified
    // Phase-8 -scroll translate to every rect. Without this, the mask
    // slides off-screen to the left as the user scrolls right (same
    // pattern as frameRects/baseBackgroundRects in TimelineGridSource).
    if (state->sidebarMaskRect.rect.isValid()
        && state->sidebarMaskRect.color.alpha() > 0) {
        const qreal scrollX = static_cast<qreal>(state->horizontalScrollValue);
        miacode::timeline::TimelineSceneRect masked = state->sidebarMaskRect;
        if (scrollX != 0.0) {
            masked.rect.translate(scrollX, 0.0);
        }
        QVector<miacode::timeline::TimelineSceneRect> maskBatch;
        maskBatch.append(masked);
        sb::pushTimelineRectBatch(snapshot, maskBatch);

        // Phase 4d-fix3 — re-emit the frame border lines AFTER the
        // mask so the sidebar's right-edge axis line + top/bottom/
        // left outer borders aren't occluded by the opaque mask.
        // TimelineGridSource emits these at z=0; without re-drawing
        // them at z=3 the mask wipes them out in the sidebar column.
        // Same +scroll bake as the mask itself (and as GridSource's
        // frameLines emission).
        if (!state->frameLines.isEmpty()) {
            QVector<miacode::timeline::TimelineSceneLine> shifted;
            shifted.reserve(state->frameLines.size());
            for (const auto& line : state->frameLines) {
                miacode::timeline::TimelineSceneLine copy = line;
                if (scrollX != 0.0) {
                    copy.start.rx() += scrollX;
                    copy.end.rx() += scrollX;
                }
                shifted.append(copy);
            }
            sb::pushTimelineLineBatch(snapshot, shifted);
        }
    }

    // Header triangles are emitted by TimelineOverlaySource so they stay above waveform and notes.

    // Text labels → CPU-rasterise to QImage via the cache, emit as
    // chart-style sprites. Phase 3 / Phase 5 polish may move this to
    // a font-atlas-based GPU pipeline if the per-label QImage cost
    // becomes a hot spot.
    if (labelCache_ == nullptr) {
        return;
    }
    // Phase 9c — like the note-sprite path in TimelineNotesSource, the
    // chart-preview Sprites batch doesn't have the timeline-scroll
    // translate baked in. Subtract horizontalScrollValue at GUI-thread
    // emission time for HEADER labels (line numbers — they scroll with
    // the chart) but NOT for LANE labels (the "1..8" left-side lane
    // indices stay fixed regardless of scroll). Mirrors the QSG
    // path's separate `headerTransformRoot` / `laneOverlay` split.
    const qreal scrollOffsetX =
        static_cast<qreal>(state->horizontalScrollValue);
    const auto rasteriseLabels =
        [&](const QVector<miacode::timeline::TimelineSceneTextLabel>& labels,
            qreal applyScrollOffsetX) {
            scene::PreviewSpriteDescriptors batch;
            batch.reserve(labels.size());
            for (const auto& label : labels) {
                if (label.text.isEmpty()
                    || label.logicalSize.width() <= 0.0
                    || label.logicalSize.height() <= 0.0) {
                    continue;
                }
                auto image = labelCache_->lookupOrRasterise(
                    label.text, label.font, label.color,
                    label.logicalSize, ctx.devicePixelRatio);
                if (!image || image->isNull()) {
                    continue;
                }
                snapshot.retainedImages.append(image);
                scene::PreviewSpriteDescriptor sprite;
                sprite.image = image.data();
                // Sub-pixel polish — round the sprite centre to an
                // integer logical pixel so projection lands the
                // texture on whole physical pixels (no bilinear
                // smear). The header markers and line numbers share
                // the same content-X anchor; without rounding the
                // float drift produces a 1-px-feeling misalignment
                // and slight blur at typical zooms.
                sprite.center = QPointF(
                    qRound(label.topLeft.x() + label.logicalSize.width() / 2.0
                        - applyScrollOffsetX),
                    qRound(label.topLeft.y() + label.logicalSize.height() / 2.0));
                sprite.width = label.logicalSize.width();
                sprite.height = label.logicalSize.height();
                sprite.rotationDegrees = 0.0;
                sprite.opacity = 1.0;
                sprite.effect = scene::PreviewAnimatedSpriteEffect::None;
                sprite.cacheable = true;
                batch.append(sprite);
            }
            if (!batch.isEmpty()) {
                sb::pushSpriteBatch(snapshot, batch);
            }
        };
    rasteriseLabels(state->laneLabels, /*applyScrollOffsetX=*/0.0);
    rasteriseLabels(state->headerLabels, scrollOffsetX);

    // Phase 9d-native — zoom button visual.
    // Pinned to the viewport (don't scroll). The TimelineRects /
    // TimelineLines pipelines apply Phase-8's -scroll uniformly, so
    // we pre-bake +scroll into the BG and border X coords so the
    // pipeline's -scroll cancels out. Same pattern as
    // laneOverlayRects in TimelineNotesSource. Sprites batch (text
    // labels) doesn't apply scroll-translate, so they stay
    // viewport-fixed without any baking.
    if (state->hasHeaderControls) {
        const auto shiftRect =
            [scrollOffsetX](miacode::timeline::TimelineSceneRect r) {
                r.rect.translate(scrollOffsetX, 0.0);
                return r;
            };

        // Background fills. Order matters:
        //   1. zoomButtonBg (covers the widget area)
        // Beta29 — the follow / progress-follow checkboxes that used to
        // be drawn alongside the zoom button moved to
        // BottomTabsQuickHost.qml's tab strip (real QML CheckBoxes).
        // The matching emit block in TimelineSceneStateBuilder is fenced
        // under #if 0; reading the now-default-constructed
        // followCheck* / progressFollowCheck* fields here would push
        // empty rects + invalid colors + 0×0 text textures into the
        // batch, which has been observed to silently abort the process
        // under x64 emulation on Apple-Silicon Windows VMs.
        QVector<miacode::timeline::TimelineSceneRect> bgRects;
        bgRects.reserve(1 + state->zoomButtonOverlayRects.size());
        bgRects.append(shiftRect(state->zoomButtonBg));
        for (const auto& rect : state->zoomButtonOverlayRects) {
            bgRects.append(shiftRect(rect));
        }
        sb::pushTimelineRectBatch(snapshot, bgRects);

        // 4-line frame around the zoom button.
        QVector<miacode::timeline::TimelineSceneLine> overlayLines;
        const auto pushFrame = [&overlayLines, scrollOffsetX](
            const miacode::timeline::TimelineSceneRect& r) {
            const qreal x0 = r.rect.left()   + scrollOffsetX;
            const qreal y0 = r.rect.top();
            const qreal x1 = r.rect.right()  + scrollOffsetX;
            const qreal y1 = r.rect.bottom();
            const QColor c = r.color;
            overlayLines.append(miacode::timeline::TimelineSceneLine{
                QPointF(x0, y0), QPointF(x1, y0), c, 1.0});
            overlayLines.append(miacode::timeline::TimelineSceneLine{
                QPointF(x1, y0), QPointF(x1, y1), c, 1.0});
            overlayLines.append(miacode::timeline::TimelineSceneLine{
                QPointF(x1, y1), QPointF(x0, y1), c, 1.0});
            overlayLines.append(miacode::timeline::TimelineSceneLine{
                QPointF(x0, y1), QPointF(x0, y0), c, 1.0});
        };
        pushFrame(state->zoomButtonBorder);
        for (const auto& line : state->zoomButtonInteriorLines) {
            miacode::timeline::TimelineSceneLine shiftedLine = line;
            shiftedLine.start.rx() += scrollOffsetX;
            shiftedLine.end.rx() += scrollOffsetX;
            overlayLines.append(shiftedLine);
        }
        sb::pushTimelineLineBatch(snapshot, overlayLines);

        if (!state->zoomButtonGlyphTriangles.isEmpty()) {
            QVector<miacode::timeline::TimelineSceneTriangle> glyphTriangles;
            glyphTriangles.reserve(state->zoomButtonGlyphTriangles.size());
            for (const auto& triangle : state->zoomButtonGlyphTriangles) {
                miacode::timeline::TimelineSceneTriangle shiftedTriangle = triangle;
                shiftedTriangle.a.rx() += scrollOffsetX;
                shiftedTriangle.b.rx() += scrollOffsetX;
                shiftedTriangle.c.rx() += scrollOffsetX;
                glyphTriangles.append(shiftedTriangle);
            }
            sb::pushTimelineTriangleBatch(snapshot, glyphTriangles);
        }

        // Labels (text) — chart-preview Sprites batch, no scroll
        // translate applied by pipeline, so applyScrollOffsetX=0.
        QVector<miacode::timeline::TimelineSceneTextLabel> controlLabels;
        controlLabels.reserve(1);
        controlLabels.append(state->zoomButtonLabel);
        rasteriseLabels(controlLabels, /*applyScrollOffsetX=*/0.0);
    }
}

}  // namespace miacode::sources::timeline
