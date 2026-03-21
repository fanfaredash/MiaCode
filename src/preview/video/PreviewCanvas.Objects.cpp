namespace {

qreal mirroredStarAngleDegrees(qreal angleDegrees)
{
    return 360.0 - angleDegrees;
}

quint64 touchPointKey(const QPointF& point)
{
    const qint32 x = qRound(point.x() * kPreviewPointHashScale);
    const qint32 y = qRound(point.y() * kPreviewPointHashScale);
    return (static_cast<quint64>(static_cast<quint32>(x)) << 32)
        | static_cast<quint64>(static_cast<quint32>(y));
}

quint64 touchRegionKey(const TimelineNoteMarker& marker)
{
    if (!marker.touchPad.isEmpty()) {
        return kTouchPadRegionHashFlag | static_cast<quint64>(qHash(marker.touchPad.toUpper()));
    }
    return touchPointKey(marker.touchPoint);
}

qreal touchPreHitElapsedSeconds(qreal deltaSeconds)
{
    return qBound<qreal>(0.0, deltaSeconds + kTouchDurationSeconds, kTouchDurationSeconds);
}

qreal touchPreHitAlpha(qreal deltaSeconds)
{
    if (deltaSeconds >= 0.0) {
        return 1.0;
    }
    return qBound<qreal>(
        0.0,
        touchPreHitElapsedSeconds(deltaSeconds) / qMax<qreal>(kTouchPhaseDivisionEpsilonSeconds, kTouchShowDurationSeconds),
        1.0
    );
}

qreal touchCloseProgress(qreal deltaSeconds)
{
    if (deltaSeconds >= 0.0) {
        return 1.0;
    }
    const qreal preHitElapsed = touchPreHitElapsedSeconds(deltaSeconds);
    if (preHitElapsed <= kTouchShowDurationSeconds) {
        return 0.0;
    }
    return qBound<qreal>(
        0.0,
        (preHitElapsed - kTouchShowDurationSeconds) / qMax<qreal>(kTouchPhaseDivisionEpsilonSeconds, kTouchCloseDurationSeconds),
        1.0
    );
}

qreal touchCloseAmount(qreal progress, qreal range)
{
    if (range <= 0.0) {
        return 0.0;
    }
    const qreal clampedProgress = qBound<qreal>(0.0, progress, 1.0);
    const qreal closeResidualNormalized = qBound<qreal>(
        0.0,
        kTouchCloseCurveResidualBias - qExp(kTouchCloseCurveExponent * clampedProgress - kTouchCloseCurveProgressBias),
        kTouchPrefabMaxCloseAmountNormalized
    );
    return range * (1.0 - closeResidualNormalized / kTouchPrefabMaxCloseAmountNormalized);
}

qreal touchLogicalOffsetForDelta(qreal deltaSeconds, qreal startOffset, qreal endOffset)
{
    if (deltaSeconds >= 0.0) {
        return endOffset;
    }
    const qreal range = qMax<qreal>(0.0, startOffset - endOffset);
    return startOffset - touchCloseAmount(touchCloseProgress(deltaSeconds), range);
}

bool isTouchMarkerVisibleAtPlayhead(const TimelineNoteMarker& marker, double playheadSeconds)
{
    if (marker.type != "touch") {
        return false;
    }
    if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
        return false;
    }
    const qreal deltaSeconds = static_cast<qreal>(playheadSeconds - marker.second);
    return deltaSeconds > -kTouchDurationSeconds && deltaSeconds < 0.0;
}

int passedCheckpointCount(const QVector<MuriCheckpointState>& checkpoints, double playheadSeconds)
{
    int passed = 0;
    for (const MuriCheckpointState& checkpoint : checkpoints) {
        if (checkpoint.second >= 0.0 && checkpoint.second <= playheadSeconds + 1e-6) {
            ++passed;
        }
    }
    return passed;
}

int slideAreaCutForPassedCount(const QVector<int>& cutIndices, int passedCount, int pointCount)
{
    if (passedCount <= 0) {
        return 0;
    }
    if (cutIndices.isEmpty()) {
        return qBound(0, passedCount, pointCount);
    }
    const int index = qBound(0, passedCount - 1, cutIndices.size() - 1);
    return qBound(0, cutIndices.value(index), pointCount);
}

qreal muriFlashOpacity(const MarkerMuriState* state, double playheadSeconds)
{
    if (state == nullptr || state->flashSecond < 0.0) {
        return 0.0;
    }
    const qreal elapsed = static_cast<qreal>(playheadSeconds - state->flashSecond);
    constexpr qreal kFlashDurationSeconds = 0.14;
    if (elapsed < 0.0 || elapsed > kFlashDurationSeconds) {
        return 0.0;
    }
    return qBound<qreal>(0.0, 0.45 * (1.0 - elapsed / kFlashDurationSeconds), 0.45);
}

}  // namespace

const MarkerMuriState* PreviewCanvas::markerMuriState(const TimelineNoteMarker& marker) const
{
    const auto it = muriAnalysisReport_.markerStates.constFind(makeMarkerAnalysisKey(marker));
    if (it == muriAnalysisReport_.markerStates.constEnd()) {
        return nullptr;
    }
    return &it.value();
}

void PreviewCanvas::drawTouchLayer(QPainter& painter, const QRectF& playfieldRect)
{
    QHash<quint64, int> overlapCounts;
    overlapCounts.reserve(16);
    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (!isTouchMarkerVisibleAtPlayhead(marker, playheadSeconds_)) {
            continue;
        }
        overlapCounts[touchRegionKey(marker)] += 1;
    }

    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "touch") {
            drawTouchMarker(painter, marker, playfieldRect, overlapCounts.value(touchRegionKey(marker), 0));
        }
    }
}
void PreviewCanvas::drawTouchHoldLayer(QPainter& painter, const QRectF& playfieldRect)
{
    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "touch_hold") {
            drawTouchHoldMarker(painter, marker, playfieldRect);
        }
    }
}

void PreviewCanvas::drawTrackLayer(QPainter& painter, const QRectF& playfieldRect)
{
    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    for (qsizetype markerIndex = noteMarkers_.size() - 1; markerIndex >= 0; --markerIndex) {
        const TimelineNoteMarker& marker = noteMarkers_[markerIndex];
        if (marker.type == "slide") {
            drawSlideTrack(painter, marker, playfieldRect);
        } else if (marker.type == "wifi") {
            drawWifiTrack(painter, marker, playfieldRect);
        }
    }
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}
void PreviewCanvas::drawSlideMotionLayer(QPainter& painter, const QRectF& playfieldRect)
{
    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    for (qsizetype markerIndex = noteMarkers_.size() - 1; markerIndex >= 0; --markerIndex) {
        const TimelineNoteMarker& marker = noteMarkers_[markerIndex];
        if (marker.type == "slide") {
            drawSlideMarker(painter, marker, playfieldRect);
        } else if (marker.type == "wifi") {
            drawWifiMarker(painter, marker, playfieldRect);
        }
    }
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}

void PreviewCanvas::drawGuideLayer(QPainter& painter, const QRectF& playfieldRect)
{
    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    drawNoteGuides(painter, playfieldRect);
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}

void PreviewCanvas::drawHoldAndTapHeadLayer(QPainter& painter, const QRectF& playfieldRect)
{
    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    tapAtlasBatchingActive_ = glRenderer_.isInitialized();
    tapAtlasBatch_.clear();

    QVector<qsizetype> layerOrder;
    layerOrder.reserve(noteMarkers_.size());
    for (qsizetype markerIndex = 0; markerIndex < noteMarkers_.size(); ++markerIndex) {
        const TimelineNoteMarker& marker = noteMarkers_[markerIndex];
        if (marker.type == "hold" || marker.type == "tap" || marker.type == "slide" || marker.type == "wifi") {
            layerOrder.append(markerIndex);
        }
    }
    std::sort(layerOrder.begin(), layerOrder.end(), [this](qsizetype a, qsizetype b) {
        const TimelineNoteMarker& markerA = noteMarkers_[a];
        const TimelineNoteMarker& markerB = noteMarkers_[b];
        if (!qFuzzyCompare(1.0 + markerA.second, 1.0 + markerB.second)) {
            return markerA.second > markerB.second;
        }
        return a > b;
    });

    for (qsizetype markerIndex : layerOrder) {
        const TimelineNoteMarker& marker = noteMarkers_[markerIndex];
        if (marker.type == "hold") {
            flushTapAtlasBatch(painter);
            drawHoldMarker(painter, marker, playfieldRect);
        } else {
            drawTapMarker(painter, marker, playfieldRect);
        }
    }

    flushTapAtlasBatch(painter);
    tapAtlasBatchingActive_ = false;
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}

void PreviewCanvas::drawJudgeEffectLayer(QPainter& painter, const QRectF& playfieldRect)
{
    if (judgeEffectTapImage_.isNull() && judgeEffectTapBreakImage_.isNull()) {
        return;
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const qreal fallbackTapPixels = kJudgeEffectFallbackTapPixels * canvasScale;
    const qreal tapBasePixels = (!tapImage_.isNull())
        ? (tapImage_.width() * kSkinAssetScale * canvasScale)
        : fallbackTapPixels;
    const qreal effectBasePixels = qMax<qreal>(kJudgeEffectMinBasePixels, tapBasePixels * kJudgeEffectBaseRelativeToTap);
    const qreal effectOffsetPixels = qMax<qreal>(kJudgeEffectMinOffsetPixels, tapBasePixels * kJudgeEffectOffsetRelativeToTap);

    struct JudgeEffectTrigger {
        qreal second = -1.0;
        QPointF logicalCenter;
        qreal facingAngle = 0.0;
        bool useBreakShape = false;
        qreal visibleStartSecond = -1.0;
        qreal visibleEndSecond = -1.0;
        bool useApproxAlpha = false;
    };
    struct LaneJudgeEffectTrigger {
        bool active = false;
        JudgeEffectTrigger trigger;
    };
    struct HoldSustainTrigger {
        QPointF logicalCenter;
        qreal startSecond = 0.0;
    };
    std::array<LaneJudgeEffectTrigger, 8> laneTriggers;
    QVector<JudgeEffectTrigger> freeTriggers;
    freeTriggers.reserve(kPreviewSmallCollectionReserve);
    QHash<quint64, HoldSustainTrigger> holdSustainTriggers;
    holdSustainTriggers.reserve(kPreviewSmallCollectionReserve);

    auto laneFacingAngleFor = [](int lane) {
        return kLaneUnitVectorBaseDegrees
            + (lane - 1) * kLaneAngleStepDegrees
            + kJudgeEffectLaneFacingAngleOffsetDegrees;
    };
    auto queueLaneTrigger = [&](int lane, qreal second, bool useBreakShape, qreal visibleStartOffsetSeconds, qreal visibleEndOffsetSeconds, bool useApproxAlpha) {
        if (lane < 1 || lane > 8) {
            return;
        }
        const qreal visibleStartSecond = second + visibleStartOffsetSeconds;
        const qreal visibleEndSecond = second + visibleEndOffsetSeconds;
        if (visibleEndSecond <= visibleStartSecond) {
            return;
        }
        const qreal elapsedSeconds = static_cast<qreal>(playheadSeconds_ - second);
        if (playheadSeconds_ < visibleStartSecond || playheadSeconds_ > visibleEndSecond
            || elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectDurationSeconds) {
            return;
        }
        const QPointF laneUnit = laneUnitVector(lane);
        JudgeEffectTrigger trigger;
        trigger.second = second;
        trigger.logicalCenter = QPointF(
            kLogicalCanvasCenter + laneUnit.x() * kLogicalDistanceEdge,
            kLogicalCanvasCenter + laneUnit.y() * kLogicalDistanceEdge
        );
        trigger.facingAngle = laneFacingAngleFor(lane);
        trigger.useBreakShape = useBreakShape;
        trigger.visibleStartSecond = visibleStartSecond;
        trigger.visibleEndSecond = visibleEndSecond;
        trigger.useApproxAlpha = useApproxAlpha;

        LaneJudgeEffectTrigger& laneTrigger = laneTriggers[static_cast<std::size_t>(lane - 1)];
        if (!laneTrigger.active || second >= laneTrigger.trigger.second) {
            laneTrigger.active = true;
            laneTrigger.trigger = trigger;
        }
    };
    auto queueFreeTrigger = [&](qreal second, const QPointF& logicalCenter, qreal facingAngle, bool useBreakShape) {
        const qreal elapsedSeconds = static_cast<qreal>(playheadSeconds_ - second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectDurationSeconds) {
            return;
        }
        JudgeEffectTrigger trigger;
        trigger.second = second;
        trigger.logicalCenter = logicalCenter;
        trigger.facingAngle = facingAngle + kJudgeEffectLaneFacingAngleOffsetDegrees;
        trigger.useBreakShape = useBreakShape;
        trigger.visibleStartSecond = second;
        trigger.visibleEndSecond = second + kJudgeEffectDurationSeconds;
        trigger.useApproxAlpha = false;
        freeTriggers.append(trigger);
    };
    auto queueHoldSustain = [&](const QPointF& logicalCenter, qreal startSecond, qreal endSecond) {
        const qreal effectiveStartSecond = startSecond
            + static_cast<qreal>(miacode::preview_gameplay::kHoldSustainEffectStartOffsetSeconds);
        const qreal effectiveEndSecond = endSecond
            - static_cast<qreal>(miacode::preview_gameplay::kHoldSustainEffectEndOffsetSeconds);
        if (effectiveEndSecond <= effectiveStartSecond) {
            return;
        }
        const qreal elapsedSeconds = static_cast<qreal>(playheadSeconds_ - effectiveStartSecond);
        if (elapsedSeconds < 0.0 || playheadSeconds_ >= effectiveEndSecond) {
            return;
        }
        const quint64 key = touchPointKey(logicalCenter);
        const auto existing = holdSustainTriggers.constFind(key);
        if (existing != holdSustainTriggers.constEnd() && existing->startSecond > effectiveStartSecond) {
            return;
        }
        HoldSustainTrigger trigger;
        trigger.logicalCenter = logicalCenter;
        trigger.startSecond = effectiveStartSecond;
        holdSustainTriggers.insert(key, trigger);
    };
    auto approximateLaneJudgeAlpha = [](qreal normalizedTime) {
        constexpr qreal kDelayNorm = 0.56;
        constexpr qreal kSigmaNorm = 0.252;
        constexpr qreal kExponent = 2.3;
        if (normalizedTime <= 0.0) {
            return 1.0;
        }
        if (normalizedTime >= 1.0) {
            return 0.0;
        }
        const qreal delayedTime = qMax<qreal>(0.0, normalizedTime - kDelayNorm);
        if (delayedTime <= 0.0) {
            return 1.0;
        }
        return qExp(-qPow(delayedTime / kSigmaNorm, kExponent));
    };

    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "tap") {
            // Tap-on-slide is still a real tap judgment moment; only the note body
            // overlaps the slide shoot point. Its judge effect should still render.
            queueLaneTrigger(
                marker.lane,
                marker.second,
                marker.isBreak,
                static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectLaneTriggerVisibleStartSeconds),
                static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectLaneTriggerVisibleEndSeconds),
                true
            );
            continue;
        }
        if (marker.type == "slide" || marker.type == "wifi") {
            if (marker.hasHeadStar) {
                queueLaneTrigger(marker.lane, marker.second, marker.headBreak, 0.0, kJudgeEffectDurationSeconds, false);
            }
            continue;
        }
        if (marker.type == "hold") {
            if (marker.endSecond >= 0.0) {
                const int holdEndLane = (marker.endLane >= 1 && marker.endLane <= 8) ? marker.endLane : marker.lane;
                queueLaneTrigger(
                    holdEndLane,
                    marker.endSecond,
                    marker.isBreak,
                    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectLaneTriggerVisibleStartSeconds),
                    static_cast<qreal>(miacode::preview_gameplay::kJudgeEffectLaneTriggerVisibleEndSeconds),
                    true
                );
            }
            if (marker.endSecond > marker.second
                && marker.lane >= 1 && marker.lane <= 8) {
                const QPointF laneUnit = laneUnitVector(marker.lane);
                const QPointF logicalCenter(
                    kLogicalCanvasCenter + laneUnit.x() * kLogicalDistanceEdge,
                    kLogicalCanvasCenter + laneUnit.y() * kLogicalDistanceEdge
                );
                queueHoldSustain(logicalCenter, marker.second, marker.endSecond);
            }
            continue;
        }
        if (marker.type == "touch_hold") {
            if (marker.endSecond > marker.second
                && !(qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y()))) {
                queueHoldSustain(marker.touchPoint, marker.second, marker.endSecond);
            }
            if (marker.endSecond < 0.0) {
                continue;
            }
            if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
                continue;
            }
            const QPointF radial = marker.touchPoint - QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter);
            const qreal facingAngle = (qFuzzyIsNull(radial.x()) && qFuzzyIsNull(radial.y()))
                ? 0.0
                : qRadiansToDegrees(qAtan2(radial.y(), radial.x()));
            queueFreeTrigger(marker.endSecond, marker.touchPoint, facingAngle, false);
        }
    }

    if (!holdSustainTriggers.isEmpty()) {
        const qreal holdBasePixels = mapLogicalLengthToRect(static_cast<qreal>(kHoldTargetWidth), playfieldRect);
        const qreal sustainBasePixels = qMax<qreal>(kJudgeEffectHoldSustainMinBasePixels, holdBasePixels * kJudgeEffectHoldSustainBaseRelativeToTap);
        const bool useHoldTexture = !judgeEffectHoldSustainCircleImage_.isNull();
        for (auto it = holdSustainTriggers.cbegin(); it != holdSustainTriggers.cend(); ++it) {
            const HoldSustainTrigger& trigger = it.value();
            const qreal elapsedSeconds = qMax<qreal>(0.0, static_cast<qreal>(playheadSeconds_ - trigger.startSecond));
            const QPointF center = mapLogicalPointToRect(trigger.logicalCenter, playfieldRect);
            for (int particleIndex = 0; particleIndex < kJudgeEffectHoldSustainParticleCount; ++particleIndex) {
                const qreal basePhaseNorm =
                    kJudgeEffectHoldSustainPhaseOffsets[static_cast<std::size_t>(particleIndex)
                    % kJudgeEffectHoldSustainPhaseOffsets.size()];
                const quint64 jitterSeed =
                    it.key() ^ (kJudgeEffectParticleSeedMultiplier * static_cast<quint64>(particleIndex + 1));
                const qreal phaseJitterNorm =
                    (static_cast<qreal>((jitterSeed >> kJudgeEffectParticleJitterBitShift) & kJudgeEffectParticleJitterMask) / 255.0
                        - kJudgeEffectParticleJitterCenter) * kJudgeEffectParticleJitterScale;
                qreal phaseNorm = basePhaseNorm + phaseJitterNorm;
                phaseNorm -= qFloor(phaseNorm);
                const qreal phase = phaseNorm * kJudgeEffectHoldSustainLifetimeSeconds;
                qreal particleAge = std::fmod(elapsedSeconds + phase, kJudgeEffectHoldSustainLifetimeSeconds);
                if (particleAge < 0.0) {
                    particleAge += kJudgeEffectHoldSustainLifetimeSeconds;
                }
                const qreal normalizedAge = particleAge / kJudgeEffectHoldSustainLifetimeSeconds;
                const qreal particleScale = sampleScalarCurve(kJudgeEffectHoldSustainSizeKeys, normalizedAge);
                const qreal sampledAlpha = qBound<qreal>(
                    0.0,
                    sampleScalarCurve(kJudgeEffectHoldSustainAlphaKeys, normalizedAge),
                    1.0
                );
                qreal particleAlpha = sampledAlpha;
                particleAlpha = qBound<qreal>(0.0, particleAlpha, 1.0);
                if (particleAlpha <= kRenderAlphaEpsilon) {
                    continue;
                }
                const int particleSize = qMax(1, qRound(sustainBasePixels * particleScale));
                if (useHoldTexture) {
                    const qreal glowAlpha = qBound<qreal>(
                        0.0,
                        particleAlpha * kJudgeEffectEdgeGlowAlpha,
                        1.0
                    );
                    if (glowAlpha > kRenderAlphaEpsilon) {
                        const int glowSize = qMax(1, qRound(particleSize * kJudgeEffectEdgeGlowScale));
                        drawSpriteImage(
                            painter,
                            judgeEffectHoldSustainCircleImage_,
                            center,
                            glowSize,
                            glowSize,
                            0.0,
                            glowAlpha
                        );
                    }
                    drawSpriteImage(
                        painter,
                        judgeEffectHoldSustainCircleImage_,
                        center,
                        particleSize,
                        particleSize,
                        0.0,
                        particleAlpha
                    );
                } else {
                    const qreal radius = qMax<qreal>(kJudgeEffectParticleMinRadiusPixels, particleSize * 0.5);
                    QColor ringOuter(kJudgeEffectHoldRingRed, kJudgeEffectHoldRingGreen, kJudgeEffectHoldRingBlue);
                    ringOuter.setAlphaF(qBound<qreal>(0.0, particleAlpha * kJudgeEffectHoldRingOuterAlphaScale, 1.0));
                    QColor ringCore(kJudgeEffectHoldRingRed, kJudgeEffectHoldRingGreen, kJudgeEffectHoldRingBlue);
                    ringCore.setAlphaF(qBound<qreal>(0.0, particleAlpha * kJudgeEffectHoldRingCoreAlphaScale, 1.0));
                    painter.setBrush(Qt::NoBrush);
                    painter.setPen(QPen(ringOuter, qMax<qreal>(kJudgeEffectHoldRingOuterMinWidth, radius * kJudgeEffectHoldRingOuterWidthScale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    painter.drawEllipse(center, radius, radius);
                    painter.setPen(QPen(ringCore, qMax<qreal>(kJudgeEffectHoldRingCoreMinWidth, radius * kJudgeEffectHoldRingCoreWidthScale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
                    painter.drawEllipse(center, radius, radius);
                }
            }
        }
    }

    auto drawJudgeEffectShapeWithEdgeGlow = [this, &painter](const QImage* image, const QRectF& sourceRect, const QPointF& center, int size, qreal angleDegrees, qreal opacity) {
        if (size <= 0 || opacity <= kRenderAlphaEpsilon) {
            return;
        }
        if (image == nullptr || image->isNull()) {
            return;
        }
        const qreal sourceWidth = sourceRect.isValid() && !sourceRect.isEmpty()
            ? sourceRect.width()
            : static_cast<qreal>(image->width());
        const qreal sourceHeight = sourceRect.isValid() && !sourceRect.isEmpty()
            ? sourceRect.height()
            : static_cast<qreal>(image->height());
        const qreal aspect = (sourceWidth > 0.0 && sourceHeight > 0.0) ? (sourceHeight / sourceWidth) : 1.0;
        const int targetWidth = qMax(1, size);
        const int targetHeight = qMax(1, qRound(size * aspect));
        const int glowWidth = qMax(1, qRound(targetWidth * kJudgeEffectEdgeGlowScale));
        const int glowHeight = qMax(1, qRound(targetHeight * kJudgeEffectEdgeGlowScale));
        const qreal glowAlpha = qBound<qreal>(0.0, opacity * kJudgeEffectEdgeGlowAlpha, 1.0);
        if (glowAlpha > kRenderAlphaEpsilon) {
            drawSpriteImage(
                painter,
                *image,
                center,
                glowWidth,
                glowHeight,
                angleDegrees,
                glowAlpha,
                sourceRect
            );
        }
        drawSpriteImage(
            painter,
            *image,
            center,
            targetWidth,
            targetHeight,
            angleDegrees,
            opacity,
            sourceRect
        );
    };

    auto drawJudgeEffectTrigger = [&](const JudgeEffectTrigger& trigger) {
        const bool useBreakShape = trigger.useBreakShape;
        const QImage* effectImage = useBreakShape ? &judgeEffectTapBreakImage_ : &judgeEffectTapImage_;
        QRectF effectSourceRect = useBreakShape ? judgeEffectTapBreakSourceRect_ : judgeEffectTapSourceRect_;
        if (effectImage == nullptr || effectImage->isNull()) {
            effectImage = useBreakShape ? &judgeEffectTapImage_ : &judgeEffectTapBreakImage_;
            effectSourceRect = useBreakShape ? judgeEffectTapSourceRect_ : judgeEffectTapBreakSourceRect_;
        }
        if (effectImage == nullptr || effectImage->isNull()) {
            return;
        }

        const qreal elapsedSeconds = static_cast<qreal>(playheadSeconds_ - trigger.second);
        const qreal clipTime = judgeEffectClipTime(elapsedSeconds);
        qreal alpha = 0.0;
        qreal normalizedTime = 0.0;
        qreal normalizedMotionTime = 0.0;
        qreal motionClipTime = clipTime;
        if (trigger.useApproxAlpha) {
            const qreal visibleDuration = trigger.visibleEndSecond - trigger.visibleStartSecond;
            if (visibleDuration <= 0.0) {
                return;
            }
            normalizedTime = qBound<qreal>(
                0.0,
                static_cast<qreal>((playheadSeconds_ - trigger.visibleStartSecond) / visibleDuration),
                1.0
            );
            const qreal legacyMotionStart = qMax<qreal>(0.0, static_cast<qreal>(trigger.visibleStartSecond - trigger.second));
            const qreal legacyMotionEnd = qMax<qreal>(
                legacyMotionStart,
                static_cast<qreal>(trigger.visibleEndSecond - trigger.second)
            );
            const qreal legacyMotionDuration = legacyMotionEnd - legacyMotionStart;
            normalizedMotionTime = normalizedTime;
            motionClipTime = qBound<qreal>(
                0.0,
                legacyMotionStart + legacyMotionDuration * normalizedMotionTime,
                kJudgeEffectDurationSeconds
            );
            alpha = qBound<qreal>(0.0, approximateLaneJudgeAlpha(normalizedTime), 1.0);
        } else {
            const qreal sampledAlpha = qBound<qreal>(0.0, sampleScalarHermiteCurve(kJudgeEffectAlphaKeys, clipTime), 1.0);
            alpha = qBound<qreal>(0.0, qPow(sampledAlpha, kJudgeEffectAlphaTailGamma), 1.0);
        }
        const qreal rootScale = sampleScalarCurve(kJudgeEffectRootScaleKeys, motionClipTime);
        if (alpha <= 0.001) {
            return;
        }
        const qreal laneFacingAngle = trigger.facingAngle;
        const qreal spriteAngleOffset = useBreakShape
            ? kJudgeEffectBreakTextureAngleOffsetDegrees
            : kJudgeEffectTapTextureAngleOffsetDegrees;
        const qreal spriteBaseAngle = laneFacingAngle + spriteAngleOffset;

        const QPointF effectCenter = mapLogicalPointToRect(trigger.logicalCenter, playfieldRect);
        const int rootSize = qMax(1, qRound(effectBasePixels * rootScale));
        drawJudgeEffectShapeWithEdgeGlow(effectImage, effectSourceRect, effectCenter, rootSize, spriteBaseAngle, alpha);

        const qreal rotationBase = trigger.useApproxAlpha
            ? (180.0 * normalizedTime)
            : sampleScalarCurve(kJudgeEffectRotationKeys, motionClipTime);
        for (int i = 0; i < static_cast<int>(kJudgeEffectParentRotationDirection.size()); ++i) {
            const qreal parentRotation = rotationBase * kJudgeEffectParentRotationDirection[static_cast<std::size_t>(i)];
            const qreal childRotation = -parentRotation;
            const QPointF localOffset = sampleVec2Curve(kJudgeEffectTapHexPositionKeys[static_cast<std::size_t>(i)], motionClipTime)
                * (effectOffsetPixels * rootScale);
            const QPointF rotatedOffset = rotatePointDegrees(localOffset, laneFacingAngle + parentRotation);
            const QPointF childCenter = effectCenter + rotatedOffset;
            const qreal childScale = sampleScalarCurve(kJudgeEffectTapHexScaleKeys[static_cast<std::size_t>(i)], motionClipTime);
            const int childSize = qMax(1, qRound(effectBasePixels * rootScale * childScale));
            drawJudgeEffectShapeWithEdgeGlow(
                effectImage,
                effectSourceRect,
                childCenter,
                childSize,
                laneFacingAngle + parentRotation + childRotation + spriteAngleOffset,
                alpha
            );
        }
    };

    for (const LaneJudgeEffectTrigger& laneTrigger : laneTriggers) {
        if (!laneTrigger.active) {
            continue;
        }
        drawJudgeEffectTrigger(laneTrigger.trigger);
    }
    for (const JudgeEffectTrigger& trigger : freeTriggers) {
        drawJudgeEffectTrigger(trigger);
    }
}

void PreviewCanvas::drawJudgeEffectTouchLayer(QPainter& painter, const QRectF& playfieldRect)
{
    struct TouchJudgeTrigger {
        const TimelineNoteMarker* marker = nullptr;
        qreal second = -1.0;
    };
    QHash<quint64, TouchJudgeTrigger> positionTriggers;
    positionTriggers.reserve(kPreviewSmallCollectionReserve);
    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type != "touch") {
            continue;
        }
        if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
            continue;
        }
        const qreal elapsedSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectTouchDurationSeconds) {
            continue;
        }
        const quint64 key = touchPointKey(marker.touchPoint);
        const auto existing = positionTriggers.constFind(key);
        if (existing == positionTriggers.constEnd() || marker.second >= existing->second) {
            TouchJudgeTrigger trigger;
            trigger.marker = &marker;
            trigger.second = marker.second;
            positionTriggers.insert(key, trigger);
        }
    }
    if (positionTriggers.isEmpty()) {
        return;
    }

    // Firework uses an isotropic scale basis to prevent any accidental non-square distortion.
    const qreal canvasScale = qMin(playfieldRect.width(), playfieldRect.height()) / kLogicalCanvasSize;
    const qreal fallbackTouchPixels = kJudgeEffectTouchFallbackPixels * kTouchAssetScale * canvasScale;
    const qreal touchBasePixels = (!touchPointImage_.isNull())
        ? (touchPointImage_.width() * kTouchAssetScale * canvasScale)
        : fallbackTouchPixels;
    const qreal prefabUnitPixels = qMax<qreal>(kJudgeEffectTouchMinUnitPixels, touchBasePixels * kJudgeEffectTouchUnitRelativeToTouch);
    const bool useTextureSprites = !judgeEffectTouchCircleImage_.isNull()
        && !judgeEffectTouchPart01Image_.isNull()
        && !judgeEffectTouchPart02Image_.isNull();
    const bool resumeNativeBatch = nativePaintingActive_ && !useTextureSprites;
    if (resumeNativeBatch) {
        endNativeBatch(painter);
    }
    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (!useTextureSprites) {
        painter.setRenderHint(QPainter::Antialiasing, true);
    }

    for (auto it = positionTriggers.cbegin(); it != positionTriggers.cend(); ++it) {
        const TouchJudgeTrigger& trigger = it.value();
        if (trigger.marker == nullptr) {
            continue;
        }
        const TimelineNoteMarker& marker = *trigger.marker;

        const qreal elapsedSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
        if (elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectTouchDurationSeconds) {
            continue;
        }
        const qreal clipTime = judgeEffectTouchClipTime(elapsedSeconds);
        // [ASSET_TUNING] DestroySelf.ifDestroy flips at 0.25s in the source effect timing, so visual should stop there.
        if (clipTime > kJudgeEffectTouchDestroySeconds) {
            continue;
        }
        const qreal lifeAlpha = 1.0;

        const qreal circleScale = sampleScalarCurve(kJudgeEffectTouchCircleScaleKeys, clipTime);
        const qreal innerParentScale = sampleScalarCurve(kJudgeEffectTouchInnerParentScaleKeys, clipTime);
        const qreal outerParentScale = sampleScalarCurve(kJudgeEffectTouchOuterParentScaleKeys, clipTime);
        const qreal circleFade = qBound<qreal>(0.0, 1.0 - clipTime / kJudgeEffectTouchCircleFadeEndSeconds, 1.0);
        const qreal circleAlpha = lifeAlpha * circleFade;
        const QPointF center = mapLogicalPointToRect(marker.touchPoint, playfieldRect);
        const qreal worldToPixels = prefabUnitPixels;

        if (useTextureSprites) {
            auto drawTouchEffectSpriteByUnits =
                [this, &painter, worldToPixels](const QImage& image, const QPointF& spriteCenter, qreal widthUnits, qreal angleDegrees, qreal opacity) {
                    if (image.isNull() || widthUnits <= 0.0 || opacity <= kRenderAlphaEpsilon) {
                        return;
                    }
                    const qreal aspect = image.width() > 0 ? static_cast<qreal>(image.height()) / image.width() : 1.0;
                    const int targetWidth = qMax(1, qRound(widthUnits * worldToPixels));
                    const int targetHeight = qMax(1, qRound(targetWidth * aspect));
                    drawSpriteImage(
                        painter,
                        image,
                        spriteCenter,
                        targetWidth,
                        targetHeight,
                        angleDegrees,
                        opacity
                    );
                };

            const qreal circleWidthUnits = kJudgeEffectTouchCircleSpriteWidthUnits * circleScale;
            drawTouchEffectSpriteByUnits(
                judgeEffectTouchCircleImage_,
                center,
                circleWidthUnits,
                0.0,
                circleAlpha
            );

            const qreal innerPieceWidthUnits =
                kJudgeEffectTouchPartSpriteWidthUnits * kJudgeEffectTouchInnerScaleBase * innerParentScale;
            for (const QPointF& offset : kJudgeEffectTouchInnerOffsets) {
                const QPointF pieceCenter = center + offset * (worldToPixels * innerParentScale);
                drawTouchEffectSpriteByUnits(
                    judgeEffectTouchPart02Image_,
                    pieceCenter,
                    innerPieceWidthUnits,
                    0.0,
                    lifeAlpha
                );
            }

            const qreal outerPieceWidthUnits =
                kJudgeEffectTouchPartSpriteWidthUnits * kJudgeEffectTouchOuterScaleBase * outerParentScale;
            for (const QPointF& offset : kJudgeEffectTouchOuterDiagonalOffsets) {
                const QPointF pieceCenter = center + offset * (worldToPixels * outerParentScale);
                drawTouchEffectSpriteByUnits(
                    judgeEffectTouchPart02Image_,
                    pieceCenter,
                    outerPieceWidthUnits,
                    kJudgeEffectTouchTextureOuterDiagonalAngleDegrees,
                    lifeAlpha
                );
            }
            for (const QPointF& offset : kJudgeEffectTouchOuterCardinalOffsets) {
                const QPointF pieceCenter = center + offset * (worldToPixels * outerParentScale);
                drawTouchEffectSpriteByUnits(
                    judgeEffectTouchPart01Image_,
                    pieceCenter,
                    outerPieceWidthUnits,
                    0.0,
                    lifeAlpha
                );
            }
            continue;
        }

        const QColor circleColor(255, 253, 119);
        const QColor sparkleColor(255, 230, 119);
        auto drawSparkle = [&painter, &sparkleColor](const QPointF& sparkleCenter, qreal size, qreal rotationDeg, qreal alpha, qreal axisScaleX, qreal axisScaleY) {
            if (size <= kJudgeEffectTouchSparkleSizeThreshold || alpha <= kRenderAlphaEpsilon) {
                return;
            }
            const qreal outer = size * kJudgeEffectTouchSparkleOuterRadiusScale;
            const qreal inner = outer * kJudgeEffectTouchSparkleInnerRadiusScale;
            QPainterPath path;
            for (int i = 0; i < kJudgeEffectTouchSparklePointCount; ++i) {
                const qreal radius = (i % 2 == 0) ? outer : inner;
                const qreal radians = qDegreesToRadians(rotationDeg + i * kJudgeEffectTouchSparklePointAngleStepDegrees);
                const QPointF point(
                    qCos(radians) * radius * axisScaleX,
                    qSin(radians) * radius * axisScaleY
                );
                if (i == 0) {
                    path.moveTo(sparkleCenter + point);
                } else {
                    path.lineTo(sparkleCenter + point);
                }
            }
            path.closeSubpath();
            QColor glow = sparkleColor;
            glow.setAlphaF(qBound<qreal>(0.0, alpha * kJudgeEffectTouchSparkleGlowAlphaScale, 1.0));
            painter.setPen(Qt::NoPen);
            painter.setBrush(glow);
            QTransform grow;
            grow.translate(sparkleCenter.x(), sparkleCenter.y());
            grow.scale(kJudgeEffectTouchSparkleGlowScale, kJudgeEffectTouchSparkleGlowScale);
            grow.translate(-sparkleCenter.x(), -sparkleCenter.y());
            painter.drawPath(grow.map(path));
            QColor core = sparkleColor;
            core.setAlphaF(qBound<qreal>(0.0, alpha, 1.0));
            painter.setBrush(core);
            painter.drawPath(path);
        };

        const qreal circleRadius = qMax<qreal>(1.0, worldToPixels * circleScale);
        if (circleAlpha > kRenderAlphaEpsilon) {
            QColor ringOuter = circleColor;
            ringOuter.setAlphaF(qBound<qreal>(0.0, circleAlpha * kJudgeEffectTouchCircleOuterAlphaScale, 1.0));
            QColor ringCore = circleColor;
            ringCore.setAlphaF(qBound<qreal>(0.0, circleAlpha * kJudgeEffectTouchCircleCoreAlphaScale, 1.0));
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(ringOuter, qMax<qreal>(kJudgeEffectHoldRingOuterMinWidth, circleRadius * kJudgeEffectHoldRingOuterWidthScale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawEllipse(center, circleRadius, circleRadius);
            painter.setPen(QPen(ringCore, qMax<qreal>(kJudgeEffectHoldRingCoreMinWidth, circleRadius * kJudgeEffectHoldRingCoreWidthScale), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter.drawEllipse(center, circleRadius, circleRadius);
        }

        const qreal innerPieceSize = qMax<qreal>(1.0, worldToPixels * kJudgeEffectTouchInnerScaleBase * innerParentScale);
        for (const QPointF& offset : kJudgeEffectTouchInnerOffsets) {
            const QPointF pieceCenter = center + offset * (worldToPixels * innerParentScale);
            drawSparkle(pieceCenter, innerPieceSize, 0.0, lifeAlpha, 1.0, 1.0);
        }

        const qreal outerPieceSize = qMax<qreal>(1.0, worldToPixels * kJudgeEffectTouchOuterScaleBase * outerParentScale);
        for (const QPointF& offset : kJudgeEffectTouchOuterDiagonalOffsets) {
            const QPointF pieceCenter = center + offset * (worldToPixels * outerParentScale);
            drawSparkle(pieceCenter, outerPieceSize, kJudgeEffectTouchTextureOuterDiagonalAngleDegrees, lifeAlpha, 1.0, 1.0);
        }
        for (const QPointF& offset : kJudgeEffectTouchOuterCardinalOffsets) {
            const QPointF pieceCenter = center + offset * (worldToPixels * outerParentScale);
            drawSparkle(pieceCenter, outerPieceSize, 0.0, lifeAlpha, kJudgeEffectTouchOuterCardinalAxisScaleX, kJudgeEffectTouchOuterCardinalAxisScaleY);
        }
    }

    if (resumeNativeBatch) {
        painter.restore();
        beginNativeBatch(painter);
    } else {
        painter.restore();
    }
}

void PreviewCanvas::drawJudgeEffectFireworkLayer(QPainter& painter, const QRectF& playfieldRect)
{
    struct FireworkTrigger {
        const TimelineNoteMarker* marker = nullptr;
        qreal second = -1.0;
    };
    QHash<quint64, FireworkTrigger> positionTriggers;
    positionTriggers.reserve(kPreviewSmallCollectionReserve);

    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (!marker.isFirework) {
            continue;
        }
        if (marker.type != "touch" && marker.type != "touch_hold") {
            continue;
        }
        if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
            continue;
        }

        const qreal triggerSecond =
            marker.type == "touch"
            ? static_cast<qreal>(marker.second + kJudgeEffectFireworkTouchTriggerDelaySeconds)
            : static_cast<qreal>(marker.endSecond);
        if (triggerSecond < 0.0) {
            continue;
        }
        const qreal elapsedSeconds = static_cast<qreal>(playheadSeconds_) - triggerSecond;
        if (elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectFireworkDurationSeconds) {
            continue;
        }

        const quint64 key = touchPointKey(marker.touchPoint);
        const auto existing = positionTriggers.constFind(key);
        if (existing == positionTriggers.constEnd() || triggerSecond >= existing->second) {
            FireworkTrigger trigger;
            trigger.marker = &marker;
            trigger.second = triggerSecond;
            positionTriggers.insert(key, trigger);
        }
    }

    if (positionTriggers.isEmpty()) {
        return;
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const qreal fallbackTouchPixels = kJudgeEffectFireworkFallbackTouchPixels * kTouchAssetScale * canvasScale;
    const qreal touchBasePixels = (!touchPointImage_.isNull())
        ? (touchPointImage_.width() * kTouchAssetScale * canvasScale)
        : fallbackTouchPixels;
    const qreal worldToPixels = qMax<qreal>(kJudgeEffectFireworkMinUnitPixels, touchBasePixels * kJudgeEffectTouchUnitRelativeToTouch);
    const bool hasColorBallSprite = !judgeEffectFireworkColorBallImage_.isNull();
    const bool resumeNativeBatch = nativePaintingActive_;
    if (resumeNativeBatch) {
        endNativeBatch(painter);
    }

    QSize layerSize = painter.viewport().size();
    if (layerSize.isEmpty()) {
        layerSize = painter.window().size();
    }
    if (layerSize.isEmpty()) {
        layerSize = size();
    }
    if (layerSize.isEmpty()) {
        if (resumeNativeBatch) {
            beginNativeBatch(painter);
        }
        return;
    }

    QImage fireworkLayer(layerSize, QImage::Format_ARGB32_Premultiplied);
    fireworkLayer.fill(Qt::transparent);

    QPainter layerPainter(&fireworkLayer);
    layerPainter.setRenderHint(QPainter::Antialiasing, true);
    layerPainter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    const QPointF playfieldCenter = mapLogicalPointToRect(QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter), playfieldRect);
    const qreal outlineRadius = qMax<qreal>(1.0, mapLogicalLengthToRect(kLogicalDistanceEdge, playfieldRect));
    const qreal clipRadius = qMax<qreal>(1.0, outlineRadius - kJudgeEffectFireworkClipInsetPixels);
    QPainterPath playfieldClip;
    playfieldClip.addEllipse(playfieldCenter, clipRadius, clipRadius);
    layerPainter.setClipPath(playfieldClip, Qt::IntersectClip);

    auto drawColorBallSprite = [&layerPainter](
        const QImage& image,
        const QRectF& sourceRect,
        const QPointF& center,
        qreal widthPixels,
        qreal alpha
    ) {
        if (image.isNull() || widthPixels <= 1.0 || alpha <= kRenderAlphaEpsilon) {
            return;
        }
        const QRectF resolvedSource =
            (sourceRect.isValid() && !sourceRect.isEmpty())
            ? sourceRect
            : QRectF();
        const qreal sourceWidth = resolvedSource.isValid() && !resolvedSource.isEmpty()
            ? resolvedSource.width()
            : static_cast<qreal>(image.width());
        const qreal sourceHeight = resolvedSource.isValid() && !resolvedSource.isEmpty()
            ? resolvedSource.height()
            : static_cast<qreal>(image.height());
        if (sourceWidth <= 0.0 || sourceHeight <= 0.0) {
            return;
        }
        const qreal aspect = sourceHeight / sourceWidth;
        const qreal heightPixels = qMax<qreal>(1.0, widthPixels * aspect);
        const QRectF target(
            center.x() - widthPixels / 2.0,
            center.y() - heightPixels / 2.0,
            widthPixels,
            heightPixels
        );
        layerPainter.save();
        layerPainter.setOpacity(qBound<qreal>(0.0, alpha, 1.0));
        if (resolvedSource.isValid() && !resolvedSource.isEmpty()) {
            layerPainter.drawImage(target, image, resolvedSource);
        } else {
            layerPainter.drawImage(target, image);
        }
        layerPainter.restore();
    };

    for (auto it = positionTriggers.cbegin(); it != positionTriggers.cend(); ++it) {
        const FireworkTrigger& trigger = it.value();
        if (trigger.marker == nullptr) {
            continue;
        }
        const TimelineNoteMarker& marker = *trigger.marker;
        const qreal elapsedSeconds = static_cast<qreal>(playheadSeconds_) - trigger.second;
        if (elapsedSeconds < 0.0 || elapsedSeconds > kJudgeEffectFireworkDurationSeconds) {
            continue;
        }

        const qreal clipTime = qBound<qreal>(0.0, elapsedSeconds, kJudgeEffectFireworkDurationSeconds);
        const qreal life01 = qBound<qreal>(0.0, clipTime / kJudgeEffectFireworkDurationSeconds, 1.0);
        const qreal fireworkScale = qMax<qreal>(0.0, sampleScalarCurve(kJudgeEffectFireworkScaleKeys, clipTime));
        const qreal fireworkAlpha = qBound<qreal>(
            0.0,
            sampleScalarCurve(kJudgeEffectFireworkAlphaKeys, clipTime) * kJudgeEffectFireworkBrightnessGain,
            1.0
        );
        const qreal fireworkRotationDegrees = sampleScalarCurve(kJudgeEffectFireworkRotationKeys, clipTime);
        const qreal colorBallScale = qMax<qreal>(0.0, sampleScalarCurve(kJudgeEffectFireworkColorBallScaleKeys, clipTime));
        const qreal colorBallBigScale = qMax<qreal>(0.0, sampleScalarCurve(kJudgeEffectFireworkColorBallBigScaleKeys, clipTime));
        const qreal colorBallAlpha = qBound<qreal>(
            0.0,
            sampleScalarCurve(kJudgeEffectFireworkColorBallAlphaKeys, clipTime) * kJudgeEffectFireworkBrightnessGain,
            1.0
        );
        const qreal colorBallBigAlpha = qBound<qreal>(
            0.0,
            sampleScalarCurve(kJudgeEffectFireworkColorBallBigAlphaKeys, clipTime) * kJudgeEffectFireworkBrightnessGain,
            1.0
        );
        const int stepRotationIndex = qBound(
            0,
            static_cast<int>(qFloor(life01 * static_cast<qreal>(kJudgeEffectFireworkStepRotationSegmentCount) + 0.5)),
            kJudgeEffectFireworkStepRotationSegmentCount
        );
        const qreal steppedRotationDegrees =
            -static_cast<qreal>(stepRotationIndex) * kJudgeEffectFireworkStepRotationDegrees;
        if (fireworkScale <= kRenderAlphaEpsilon && colorBallScale <= kRenderAlphaEpsilon && colorBallBigScale <= kRenderAlphaEpsilon) {
            continue;
        }
        const QPointF center = mapLogicalPointToRect(marker.touchPoint, playfieldRect);

        if (fireworkScale > kRenderAlphaEpsilon && fireworkAlpha > kRenderAlphaEpsilon) {
            const qreal outerRadius = qMax<qreal>(1.0, (kJudgeEffectFireworkBaseWidthUnits * fireworkScale * worldToPixels) * 0.5);
            const QRectF sectorRect(
                center.x() - outerRadius,
                center.y() - outerRadius,
                outerRadius * 2.0,
                outerRadius * 2.0
            );
            layerPainter.save();
            layerPainter.setPen(Qt::NoPen);
            for (int sector = 0; sector < kJudgeEffectFireworkColoredSectorCount; ++sector) {
                const QColor sectorColor = scaleRgb(
                    kJudgeEffectFireworkSectorColors[static_cast<std::size_t>(sector % kJudgeEffectFireworkSectorColors.size())],
                    kJudgeEffectFireworkBrightnessGain,
                    fireworkAlpha * kJudgeEffectFireworkSectorAlphaScale
                );
                layerPainter.setBrush(sectorColor);
                const qreal startAngle =
                    kJudgeEffectFireworkSectorPhaseDegrees
                    + fireworkRotationDegrees
                    + steppedRotationDegrees
                    + static_cast<qreal>(sector) * kJudgeEffectFireworkSectorStepDegrees;
                layerPainter.drawPie(
                    sectorRect,
                    qRound(startAngle * 16.0),
                    qRound(kJudgeEffectFireworkSectorSpanDegrees * 16.0)
                );
            }
            layerPainter.restore();
        }

        if (hasColorBallSprite && colorBallBigScale > kRenderAlphaEpsilon && colorBallBigAlpha > kRenderAlphaEpsilon) {
            const qreal widthPixels = qMax<qreal>(
                1.0,
                kJudgeEffectFireworkColorBallBaseWidthUnits * colorBallBigScale * worldToPixels
            );
            drawColorBallSprite(
                judgeEffectFireworkColorBallImage_,
                judgeEffectFireworkColorBallSourceRect_,
                center,
                widthPixels,
                colorBallBigAlpha
            );
        }
        if (hasColorBallSprite && colorBallScale > kRenderAlphaEpsilon && colorBallAlpha > kRenderAlphaEpsilon) {
            const qreal widthPixels = qMax<qreal>(
                1.0,
                kJudgeEffectFireworkColorBallBaseWidthUnits * colorBallScale * worldToPixels
            );
            drawColorBallSprite(
                judgeEffectFireworkColorBallImage_,
                judgeEffectFireworkColorBallSourceRect_,
                center,
                widthPixels,
                colorBallAlpha
            );
        }
        if (!hasColorBallSprite && (colorBallScale > kRenderAlphaEpsilon || colorBallBigScale > kRenderAlphaEpsilon)) {
            const qreal fallbackScale = qMax(colorBallScale, colorBallBigScale);
            const qreal fallbackAlpha = qMax(colorBallAlpha, colorBallBigAlpha);
            const qreal radius = qMax<qreal>(1.0, (kJudgeEffectFireworkColorBallBaseWidthUnits * fallbackScale * worldToPixels) * 0.5);
            QRadialGradient glow(center, radius);
            glow.setColorAt(kJudgeEffectFireworkColorBallCoreStop, scaleRgb(QColor(255, 245, 160), 1.0, fallbackAlpha * kJudgeEffectFireworkColorBallCoreAlphaScale));
            glow.setColorAt(kJudgeEffectFireworkColorBallMidStop, scaleRgb(QColor(255, 110, 220), 1.0, fallbackAlpha * kJudgeEffectFireworkColorBallMidAlphaScale));
            glow.setColorAt(kJudgeEffectFireworkColorBallOuterStop, scaleRgb(QColor(110, 190, 255), 1.0, fallbackAlpha * kJudgeEffectFireworkColorBallOuterAlphaScale));
            glow.setColorAt(1.0, QColor(255, 240, 120, 0));
            layerPainter.setPen(Qt::NoPen);
            layerPainter.setBrush(glow);
            layerPainter.drawEllipse(center, radius, radius);
        }

        const qreal holeRadius = clipRadius * (
            kJudgeEffectFireworkHoleStartRadiusRatio
            + (kJudgeEffectFireworkHoleEndRadiusRatio - kJudgeEffectFireworkHoleStartRadiusRatio) * smoothStep01(life01)
        );
        const qreal holeFeather = qMax<qreal>(kJudgeEffectFireworkHoleMinFeatherPixels, holeRadius * kJudgeEffectFireworkHoleBandRatio);
        const qreal holeMaskRadius = holeRadius + holeFeather;
        const qreal holeSolidRatio = qBound<qreal>(0.0, holeRadius / qMax<qreal>(holeMaskRadius, 0.001), 1.0);
        QRadialGradient holeGradient(center, holeMaskRadius);
        holeGradient.setColorAt(0.0, QColor(0, 0, 0, 255));
        holeGradient.setColorAt(holeSolidRatio, QColor(0, 0, 0, 255));
        holeGradient.setColorAt(1.0, QColor(0, 0, 0, 0));

        layerPainter.save();
        layerPainter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        layerPainter.setPen(Qt::NoPen);
        layerPainter.setBrush(holeGradient);
        layerPainter.drawEllipse(center, holeMaskRadius, holeMaskRadius);
        layerPainter.restore();
    }
    layerPainter.end();

    painter.save();
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.drawImage(QPoint(0, 0), fireworkLayer);
    painter.restore();

    if (resumeNativeBatch) {
        beginNativeBatch(painter);
    }
}

void PreviewCanvas::drawHud(QPainter& painter, const QRectF& stageRect)
{
    if (!showTimestamp_ && !showDebugInfo_ && !showObjectStatsHud_) {
        return;
    }

    struct HudStats {
        int tapTotal = 0;
        int tapPlayed = 0;
        int holdTotal = 0;
        int holdPlayed = 0;
        int slideTotal = 0;
        int slidePlayed = 0;
        int touchTotal = 0;
        int touchPlayed = 0;
        int breakTotal = 0;
        int breakPlayed = 0;
        int totalCount = 0;
        int totalPlayed = 0;
        double finaleBaseTotal = 0.0;
        double finaleCurrent = 0.0;
        double deluxeBaseTotal = 0.0;
        double deluxeBaseCurrent = 0.0;
        int deluxeBreakTotal = 0;
        int deluxeBreakCurrent = 0;
    };
    const auto computeHudStats = [this](double second) {
        HudStats stats;
        QHash<QString, bool> slideHeadSeen;
        const auto eventPlayed = [second](double judgeSecond) {
            return judgeSecond <= (second + 1e-6);
        };
        const auto slideJudgeSecondFor = [](const TimelineNoteMarker& marker) {
            if (marker.endSecond > marker.second) {
                return marker.endSecond;
            }
            if (marker.slideTraceSecond > marker.second) {
                return marker.slideTraceSecond;
            }
            return marker.second;
        };
        const auto slideHeadEventKey = [](const TimelineNoteMarker& marker) {
            return QStringLiteral("slide_head_star|%1|%2|%3|%4|%5")
                .arg(marker.second, 0, 'f', 6)
                .arg(marker.lane)
                .arg(marker.sourceLine)
                .arg(marker.sourceCol)
                .arg(marker.eachGroupId);
        };
        const auto addNormalEvent = [&stats, &eventPlayed](
            double judgeSecond,
            int finaleScore,
            int deluxeValue,
            int* totalCounter,
            int* playedCounter
        ) {
            const bool played = eventPlayed(judgeSecond);
            ++(*totalCounter);
            ++stats.totalCount;
            stats.finaleBaseTotal += static_cast<double>(finaleScore);
            stats.deluxeBaseTotal += static_cast<double>(deluxeValue);
            if (played) {
                ++(*playedCounter);
                ++stats.totalPlayed;
                stats.finaleCurrent += static_cast<double>(finaleScore);
                stats.deluxeBaseCurrent += static_cast<double>(deluxeValue);
            }
        };
        const auto addBreakEvent = [&stats, &eventPlayed](double judgeSecond) {
            const bool played = eventPlayed(judgeSecond);
            ++stats.breakTotal;
            ++stats.totalCount;
            ++stats.deluxeBreakTotal;
            stats.finaleBaseTotal += 2500.0;
            stats.deluxeBaseTotal += 5.0;
            if (played) {
                ++stats.breakPlayed;
                ++stats.totalPlayed;
                ++stats.deluxeBreakCurrent;
                stats.finaleCurrent += 2600.0;
                stats.deluxeBaseCurrent += 5.0;
            }
        };
        for (const TimelineNoteMarker& marker : noteMarkers_) {
            const QString type = marker.type.toLower();
            if (type == "tap") {
                if (marker.isBreak) {
                    addBreakEvent(marker.second);
                } else {
                    addNormalEvent(marker.second, 500, 1, &stats.tapTotal, &stats.tapPlayed);
                }
                continue;
            }
            if (type == "touch") {
                if (marker.isBreak) {
                    addBreakEvent(marker.second);
                } else {
                    addNormalEvent(marker.second, 500, 1, &stats.touchTotal, &stats.touchPlayed);
                }
                continue;
            }
            if (type == "hold" || type == "touch_hold") {
                if (marker.isBreak) {
                    addBreakEvent(marker.second);
                } else {
                    addNormalEvent(marker.second, 1000, 2, &stats.holdTotal, &stats.holdPlayed);
                }
                continue;
            }
            if (type == "slide" || type == "wifi") {
                if (marker.hasHeadStar) {
                    const QString helperKey = slideHeadEventKey(marker);
                    if (!slideHeadSeen.contains(helperKey)) {
                        slideHeadSeen.insert(helperKey, true);
                        if (marker.headBreak) {
                            addBreakEvent(marker.second);
                        } else {
                            addNormalEvent(marker.second, 500, 1, &stats.tapTotal, &stats.tapPlayed);
                        }
                    }
                }
                const double slideJudgeSecond = slideJudgeSecondFor(marker);
                if (marker.trackBreak) {
                    addBreakEvent(slideJudgeSecond);
                } else {
                    addNormalEvent(slideJudgeSecond, 1500, 3, &stats.slideTotal, &stats.slidePlayed);
                }
            }
        }
        return stats;
    };
    painter.setPen(QColor("#D9E2EC"));
    const qreal hudPadding = qBound<qreal>(10.0, stageRect.width() * 0.028, 18.0);
    const qreal stageShortSide = qMax<qreal>(1.0, qMin(stageRect.width(), stageRect.height()));
    const int timeFontPointSize = qBound(12, qRound(stageShortSide * 0.022), 34);
    const int debugFontPointSize = qBound(8, qRound(stageShortSide * 0.013), 22);
    QFont timeFont = hudMonoFont(timeFontPointSize, QFont::DemiBold);
    if (showDebugInfo_) {
        QFont fpsFont = hudMonoFont(debugFontPointSize, QFont::Medium);
        painter.setFont(fpsFont);
        const QFontMetrics metrics(fpsFont);
        const qreal leftX = stageRect.left() + hudPadding;
        const qreal baseline0 = stageRect.top() + hudPadding + metrics.ascent();
        const QString rendererLabel = usedGpuRendererThisFrame_ ? "Renderer: GPU" : "Renderer: CPU";
        painter.drawText(
            QPointF(leftX, baseline0),
            rendererLabel
        );
        painter.drawText(
            QPointF(leftX, baseline0 + metrics.height()),
            QString::number(fpsDisplay_, 'f', 1) + " FPS"
        );
        painter.drawText(
            QPointF(leftX, baseline0 + metrics.height() * 2),
            QString("Fallback: %1")
                .arg(cpuFallbackCount_)
        );
    }

    if (showTimestamp_) {
        painter.setFont(timeFont);
        painter.drawText(
            QPointF(stageRect.left() + hudPadding, stageRect.bottom() - hudPadding),
            formatHudTimeLabel(playheadSeconds_)
        );
    }

    if (!showObjectStatsHud_) {
        return;
    }

    const qreal stageAspectRatio = stageRect.height() > 0.0 ? (stageRect.width() / stageRect.height()) : 1.0;
    if (qAbs(stageAspectRatio - 1.0) < 0.02) {
        return;
    }

    const QRectF playfieldRect = stagePlayfieldRect(stageRect);
    const qreal statsLeftLimit = playfieldRect.right() + hudPadding;
    const qreal statsRightLimit = stageRect.right() - hudPadding;
    const qreal availableStatsWidth = statsRightLimit - statsLeftLimit;
    if (availableStatsWidth < 40.0) {
        return;
    }

    const HudStats stats = computeHudStats(playheadSeconds_);
    const double finaleRate = stats.finaleBaseTotal > 0.0
        ? (stats.finaleCurrent / stats.finaleBaseTotal) * 100.0
        : 0.0;
    const double deluxeRate = (stats.deluxeBaseTotal > 0.0
        ? (stats.deluxeBaseCurrent / stats.deluxeBaseTotal) * 100.0
        : 0.0)
        + (stats.deluxeBreakTotal > 0
            ? (static_cast<double>(stats.deluxeBreakCurrent) / static_cast<double>(stats.deluxeBreakTotal))
            : 0.0);

    int baseFontPointSize = qBound(10, qRound(stageShortSide * 0.021), 22);
    QFont titleFont;
    QFont rateFont;
    QFont statFont;
    QFontMetrics titleMetrics{QFont()};
    QFontMetrics rateMetrics{QFont()};
    QFontMetrics statMetrics{QFont()};
    qreal blockWidth = 0.0;
    qreal blockHeight = 0.0;
    qreal headerGap = 0.0;
    qreal sectionGap = 0.0;
    qreal statGap = 0.0;
    QString rateLine;
    QStringList statLines;

    while (baseFontPointSize >= 8) {
        titleFont = hudMonoFont(baseFontPointSize, QFont::Black);
        rateFont = hudMonoFont(baseFontPointSize + 1, QFont::Black);
        statFont = hudMonoFont(baseFontPointSize, QFont::Black);
        titleMetrics = QFontMetrics(titleFont);
        rateMetrics = QFontMetrics(rateFont);
        statMetrics = QFontMetrics(statFont);

        const QString finaleLine = QStringLiteral("%1 %")
            .arg(QString::number(finaleRate, 'f', 2).rightJustified(6, QChar('0')));
        rateLine = QStringLiteral("%1 %")
            .arg(QString::number(deluxeRate, 'f', 4).rightJustified(8, QChar('0')));
        statLines = QStringList{
            finaleLine,
            QStringLiteral("TAP: %1").arg(stats.tapPlayed),
            QStringLiteral("HLD: %1").arg(stats.holdPlayed),
            QStringLiteral("SLD: %1").arg(stats.slidePlayed),
            QStringLiteral("TOH: %1").arg(stats.touchPlayed),
            QStringLiteral("BRK: %1").arg(stats.breakPlayed),
        };

        int maxStatWidth = 0;
        for (const QString& line : statLines) {
            maxStatWidth = qMax(maxStatWidth, statMetrics.horizontalAdvance(line));
        }

        headerGap = qMax<qreal>(2.0, titleMetrics.height() * 0.18);
        sectionGap = qMax<qreal>(8.0, titleMetrics.height() * 0.5);
        statGap = qMax<qreal>(1.0, statMetrics.height() * 0.08);
        blockWidth = qMax<qreal>(
            qMax<qreal>(
                titleMetrics.horizontalAdvance(QStringLiteral("FiNALE Rate:")),
                titleMetrics.horizontalAdvance(QStringLiteral("DELUXE Rate:"))
            ),
            qMax<qreal>(
                qMax(rateMetrics.horizontalAdvance(rateLine), rateMetrics.horizontalAdvance(finaleLine)),
                maxStatWidth
            )
        );
        blockHeight =
            static_cast<qreal>(titleMetrics.height()) * 2.0
            + headerGap * 2.0
            + static_cast<qreal>(rateMetrics.height()) * 2.0
            + sectionGap * 2.0
            + static_cast<qreal>(statMetrics.height()) * static_cast<qreal>(statLines.size() - 1)
            + statGap * static_cast<qreal>(qMax(0, statLines.size() - 2));

        if (blockWidth <= availableStatsWidth && blockHeight <= (stageRect.height() - hudPadding * 2.0)) {
            break;
        }
        --baseFontPointSize;
    }

    if (blockWidth > availableStatsWidth || blockHeight > (stageRect.height() - hudPadding * 2.0)) {
        return;
    }

    const qreal blockLeft = statsRightLimit - blockWidth;
    const qreal blockTop = stageRect.bottom() - hudPadding - blockHeight;
    if (blockTop < stageRect.top() + hudPadding) {
        return;
    }

    const auto drawHudText = [&painter](const QPointF& baseline, const QString& text, const QFont& font) {
        painter.save();
        painter.setFont(font);
        painter.setPen(QColor(0, 0, 0, 190));
        painter.drawText(baseline + QPointF(2.0, 2.0), text);
        painter.setPen(QColor("#FFFFFF"));
        painter.drawText(baseline, text);
        painter.restore();
    };

    qreal baseline = blockTop + titleMetrics.ascent();
    drawHudText(QPointF(blockLeft, baseline), QStringLiteral("FiNALE Rate:"), titleFont);
    baseline += titleMetrics.descent() + headerGap + rateMetrics.ascent();
    drawHudText(QPointF(blockLeft, baseline), statLines.at(0), rateFont);
    baseline += rateMetrics.descent() + sectionGap + titleMetrics.ascent();
    drawHudText(QPointF(blockLeft, baseline), QStringLiteral("DELUXE Rate:"), titleFont);
    baseline += titleMetrics.descent() + headerGap + rateMetrics.ascent();
    drawHudText(QPointF(blockLeft, baseline), rateLine, rateFont);
    baseline += rateMetrics.descent() + sectionGap + statMetrics.ascent();
    for (int i = 1; i < statLines.size(); ++i) {
        if (i > 1) {
            baseline += statGap + statMetrics.leading();
        }
        drawHudText(QPointF(blockLeft, baseline), statLines.at(i), statFont);
        baseline += statMetrics.height();
    }
}

bool PreviewCanvas::drawSpriteImage(
    QPainter& painter,
    const QImage& image,
    const QPointF& center,
    int targetWidth,
    int targetHeight,
    qreal angleDegrees,
    qreal opacity,
    const QRectF& sourceRect
)
{
    if (opacity <= 0.0) {
        return true;
    }
    if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
        return false;
    }

    const QImage* renderImage = &image;
    QRectF resolvedSourceRect = sourceRect;
    resolveAtlasImage(image, sourceRect, renderImage, resolvedSourceRect);
    if (renderImage == nullptr || renderImage->isNull()) {
        return false;
    }
    if (tapAtlasBatchingActive_ && !tapAtlasBatch_.isEmpty() && renderImage != &tapAtlasImage_) {
        flushTapAtlasBatch(painter);
    }
    if (tapAtlasBatchingActive_ && renderImage == &tapAtlasImage_) {
        BatchedSprite sprite;
        sprite.image = renderImage;
        sprite.center = center;
        sprite.targetWidth = targetWidth;
        sprite.targetHeight = targetHeight;
        sprite.angleDegrees = angleDegrees;
        sprite.opacity = opacity;
        sprite.sourceRect = resolvedSourceRect;
        tapAtlasBatch_.append(sprite);
        return true;
    }

    const QRectF targetRect(
        center.x() - targetWidth / 2.0,
        center.y() - targetHeight / 2.0,
        targetWidth,
        targetHeight
    );
    bool renderedByGl = false;
    if (glRenderer_.isInitialized()) {
        if (nativePaintingActive_) {
            renderedByGl = glRenderer_.drawImageQuad(*renderImage, targetRect, angleDegrees, opacity, resolvedSourceRect);
        } else {
            painter.beginNativePainting();
            renderedByGl = glRenderer_.drawImageQuad(*renderImage, targetRect, angleDegrees, opacity, resolvedSourceRect);
            painter.endNativePainting();
        }
    }
    if (renderedByGl) {
        usedGpuRendererThisFrame_ = true;
        return true;
    }
    if (nativePaintingActive_) {
        return false;
    }

    if (highQualityRender_ || (resolvedSourceRect.isValid() && !resolvedSourceRect.isEmpty())) {
        ++cpuFallbackCount_;
        painter.save();
        painter.setOpacity(opacity);
        painter.translate(center);
        painter.rotate(angleDegrees);
        const QRectF drawRect(-targetWidth / 2.0, -targetHeight / 2.0, targetWidth, targetHeight);
        if (resolvedSourceRect.isValid() && !resolvedSourceRect.isEmpty()) {
            painter.drawImage(drawRect, *renderImage, resolvedSourceRect);
        } else {
            painter.drawImage(drawRect, *renderImage);
        }
        painter.restore();
        return true;
    }

    const QImage transformed = cachedSpriteTransform(*renderImage, targetWidth, targetHeight, angleDegrees);
    if (transformed.isNull()) {
        return false;
    }
    ++cpuFallbackCount_;
    painter.save();
    painter.drawImage(
        QPointF(center.x() - transformed.width() / 2.0, center.y() - transformed.height() / 2.0),
        transformed
    );
    painter.restore();
    return true;
}

void PreviewCanvas::warmTransformCachesForCurrentSize()
{
    if (!kEnablePreviewCaches) {
        return;
    }

    guideTransformCache_.clear();
    guideTransformCacheOrder_.clear();
    spriteTransformCache_.clear();
    spriteTransformCacheOrder_.clear();

    const QRectF playfieldRect = currentPlayfieldRect();
    const int side = qMax(1, qRound(playfieldRect.width()));
    if (side <= 0) {
        return;
    }
    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const qreal tapBaseScale = canvasScale * kSkinAssetScale;

    const int laneAngles[] = {23, 68, 113, 158, 203, 248, 293, 338};

    const auto warmSprite = [this](const QImage& image, int targetWidth, int targetHeight, int angleDegrees) {
        if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
            return;
        }
        cachedSpriteTransform(image, targetWidth, targetHeight, angleDegrees);
    };
    const auto warmGuide = [this](const QImage& image, int targetWidth, int targetHeight, int angleDegrees) {
        if (image.isNull() || targetWidth <= 0 || targetHeight <= 0) {
            return;
        }
        cachedGuideTransform(image, targetWidth, targetHeight, angleDegrees);
    };

    const QImage* tapSprites[] = {
        &tapImage_, &tapEachImage_, &tapBreakImage_
    };
    for (const QImage* image : tapSprites) {
        if (image == nullptr || image->isNull()) {
            continue;
        }
        const int minWidth = quantizeDimension(qRound(image->width() * tapBaseScale * tapScaleForDistance(0.0)), kSpriteTransformSizeStep);
        const int maxWidth = quantizeDimension(qRound(image->width() * tapBaseScale), kSpriteTransformSizeStep);
        const qreal aspect = static_cast<qreal>(image->height()) / qMax(1, image->width());
        for (int targetWidth = minWidth; targetWidth <= maxWidth; targetWidth += kSpriteTransformSizeStep) {
            const int targetHeight = qMax(1, qRound(targetWidth * aspect));
            for (int angle : laneAngles) {
                warmSprite(*image, targetWidth, targetHeight, angle);
            }
        }
    }

    const QImage* starSprites[] = {
        &starImage_, &starEachImage_, &starBreakImage_
    };
    const qreal tapSizedStarWidth = (!tapImage_.isNull() ? tapImage_.width() * kSkinAssetScale : 90.0) * kSlideSpawnStarRelativeScale;
    const qreal tapSizedStarHeight = (!tapImage_.isNull() ? tapImage_.height() * kSkinAssetScale : 90.0) * kSlideSpawnStarRelativeScale;
    for (const QImage* image : starSprites) {
        if (image == nullptr || image->isNull()) {
            continue;
        }
        const int spawnMinWidth = quantizeDimension(qRound(tapSizedStarWidth * canvasScale * tapScaleForDistance(0.0)), kSpriteTransformSizeStep);
        const int spawnMaxWidth = quantizeDimension(qRound(tapSizedStarWidth * canvasScale), kSpriteTransformSizeStep);
        const qreal spawnAspect = tapSizedStarHeight / qMax<qreal>(1.0, tapSizedStarWidth);
        for (int targetWidth = spawnMinWidth; targetWidth <= spawnMaxWidth; targetWidth += kSpriteTransformSizeStep) {
            const int targetHeight = qMax(1, qRound(targetWidth * spawnAspect));
            for (int angle : laneAngles) {
                warmSprite(*image, targetWidth, targetHeight, angle);
            }
        }
        const int waitMinWidth = quantizeDimension(qRound(image->width() * canvasScale * slideStartupStarInitialScale(*image)), kSpriteTransformSizeStep);
        const int waitMaxWidth = quantizeDimension(qRound(image->width() * canvasScale * kStarAssetScale), kSpriteTransformSizeStep);
        const qreal waitAspect = static_cast<qreal>(image->height()) / qMax(1, image->width());
        for (int targetWidth = waitMinWidth; targetWidth <= waitMaxWidth; targetWidth += kSpriteTransformSizeStep) {
            const int targetHeight = qMax(1, qRound(targetWidth * waitAspect));
            for (int angle = 0; angle < 360; angle += 15) {
                warmSprite(*image, targetWidth, targetHeight, angle);
            }
        }
    }

    const struct {
        const QImage* image;
        qreal sourceRadius;
    } guideSprites[] = {
        {&noteGuideNormalImage_, kNoteGuideSourceRadius},
        {&noteGuideBreakImage_, kNoteGuideSourceRadius},
        {&noteGuideEachImage_, kNoteGuideSourceRadius},
        {&noteGuideSlideImage_, kNoteGuideSourceRadius},
        {&noteGuideEachLine1Image_, kEachLine1SourceRadius},
        {&noteGuideEachLine2Image_, kEachLine2SourceRadius},
        {&noteGuideEachLine3Image_, kEachLine3SourceRadius},
        {&noteGuideEachLine4Image_, kEachLine4SourceRadius},
    };
    for (const auto& guide : guideSprites) {
        if (guide.image == nullptr || guide.image->isNull() || guide.sourceRadius <= 0.0) {
            continue;
        }
        const qreal minScale = kLogicalDistanceTap / guide.sourceRadius;
        const qreal maxScale = kLogicalDistanceEdge / guide.sourceRadius;
        const int minWidth = quantizeDimension(qRound(guide.image->width() * canvasScale * minScale), kGuideTransformSizeStep);
        const int maxWidth = quantizeDimension(qRound(guide.image->width() * canvasScale * maxScale), kGuideTransformSizeStep);
        const qreal aspect = static_cast<qreal>(guide.image->height()) / qMax(1, guide.image->width());
        for (int targetWidth = minWidth; targetWidth <= maxWidth; targetWidth += kGuideTransformSizeStep) {
            const int targetHeight = qMax(1, qRound(targetWidth * aspect));
            for (int angle = 0; angle < 360; angle += 15) {
                warmGuide(*guide.image, targetWidth, targetHeight, angle);
            }
        }
    }
}

void PreviewCanvas::prebuildTrackAreaCachesForCurrentState()
{
    slideTrackAreaCache_.clear();
    wifiTrackAreaCache_.clear();
    if (!kEnablePreviewCaches || noteMarkers_.isEmpty()) {
        return;
    }

    const QRectF playfieldRect = currentPlayfieldRect();
    if (playfieldRect.width() <= 0.0 || playfieldRect.height() <= 0.0) {
        return;
    }

    QImage dummy(1, 1, QImage::Format_ARGB32_Premultiplied);
    dummy.fill(Qt::transparent);
    QPainter painter(&dummy);

    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "slide") {
            const QImage* image = selectSlideTrackImage(marker);
            if (image == nullptr || image->isNull()) {
                continue;
            }
            for (int segmentIndex = 0; segmentIndex < marker.slideTrackAreaPoints.size(); ++segmentIndex) {
                const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
                const QVector<QVector<int>>& areaCuts = marker.slideTrackAreaCutIndices.value(segmentIndex);
                for (int areaIndex = 0; areaIndex < areas.size(); ++areaIndex) {
                    drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, 0, playfieldRect, image);
                    const QVector<int>& cuts = areaCuts.value(areaIndex);
                    for (int cut : cuts) {
                        drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, cut, playfieldRect, image);
                    }
                }
            }
        } else if (marker.type == "wifi") {
            for (int areaIndex = 0; areaIndex < marker.wifiTrackAreaPoints.size(); ++areaIndex) {
                drawCachedWifiArea(painter, marker, areaIndex, 0, playfieldRect);
                const int areaSize = marker.wifiTrackAreaPoints[areaIndex].size();
                const QVector<double>& checkpoints = marker.wifiTrackAreaCheckpoints.value(areaIndex);
                for (int passed = 1; passed <= checkpoints.size(); ++passed) {
                    const int localCut = qBound(
                        0,
                        qFloor(static_cast<qreal>(passed) * areaSize / qMax(1, checkpoints.size())),
                        areaSize
                    );
                    drawCachedWifiArea(painter, marker, areaIndex, localCut, playfieldRect);
                }
            }
        }
    }
}

void PreviewCanvas::drawNoteGuides(QPainter& painter, const QRectF& playfieldRect)
{
    struct ActiveEachCandidate {
        const TimelineNoteMarker* marker = nullptr;
    };

    auto renderGuideImage = [this, &painter, &playfieldRect](
        const QImage* image,
        qreal scale,
        qreal angleDegrees,
        const QPointF& logicalPos,
        bool preferGpu,
        qreal gpuAngleOffset
    ) {
        if (image == nullptr || image->isNull() || scale <= 0.0) {
            return;
        }
        const QImage* renderImage = image;
        QRectF renderSourceRect;
        const bool atlasResolved = resolveAtlasImage(*image, QRectF(), renderImage, renderSourceRect);
        if (renderImage == nullptr || renderImage->isNull()) {
            return;
        }
        const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
        const QPointF point = mapLogicalPointToRect(logicalPos, playfieldRect);
        const int targetWidth = qMax(1, qRound(image->width() * canvasScale * scale));
        const int targetHeight = qMax(1, qRound(image->height() * canvasScale * scale));
        const QRectF targetRect(
            point.x() - targetWidth / 2.0,
            point.y() - targetHeight / 2.0,
            targetWidth,
            targetHeight
        );
        bool renderedByGl = false;
        if (preferGpu && glRenderer_.isInitialized()) {
            if (nativePaintingActive_) {
                renderedByGl = glRenderer_.drawImageQuad(
                    *renderImage,
                    targetRect,
                    angleDegrees + gpuAngleOffset,
                    1.0,
                    renderSourceRect
                );
            } else {
                painter.beginNativePainting();
                renderedByGl = glRenderer_.drawImageQuad(*renderImage, targetRect, angleDegrees + gpuAngleOffset, 1.0, renderSourceRect);
                painter.endNativePainting();
            }
        }
        if (renderedByGl) {
            usedGpuRendererThisFrame_ = true;
            return;
        }
        if (nativePaintingActive_) {
            return;
        }

        if (atlasResolved && renderSourceRect.isValid() && !renderSourceRect.isEmpty()) {
            ++cpuFallbackCount_;
            painter.save();
            painter.translate(point);
            painter.rotate(angleDegrees);
            painter.drawImage(
                QRectF(-targetWidth / 2.0, -targetHeight / 2.0, targetWidth, targetHeight),
                *renderImage,
                renderSourceRect
            );
            painter.restore();
            return;
        }

        const QImage transformed = cachedGuideTransform(*image, targetWidth, targetHeight, angleDegrees);
        if (transformed.isNull()) {
            return;
        }
        ++cpuFallbackCount_;
        painter.drawImage(
            QPointF(
                point.x() - transformed.width() / 2.0,
                point.y() - transformed.height() / 2.0
            ),
            transformed
        );
    };

    auto renderConcentricGuide = [&renderGuideImage](
        const QImage* image,
        qreal distance,
        qreal sourceRadius,
        qreal angleDegrees,
        bool preferGpu = true,
        qreal gpuAngleOffset = 0.0
    ) {
        if (distance <= 0.0 || sourceRadius <= 0.0) {
            return;
        }
        const qreal guideRadius = distance < kLogicalDistanceTap
            ? kLogicalDistanceTap
            : qMin(distance, kLogicalDistanceEdge);
        const qreal scale = guideRadius / sourceRadius;
        renderGuideImage(
            image,
            scale,
            angleDegrees,
            QPointF(kLogicalCanvasCenter, kLogicalCanvasCenter),
            preferGpu,
            gpuAngleOffset
        );
    };

    auto renderTailGuide = [this, &renderGuideImage](const QImage* image, int lane, qreal deltaSeconds) {
        TapApproachSample approach;
        const qreal logicalDistanceTap = static_cast<qreal>(miacode::preview_gameplay::kLogicalDistanceTap);
        const qreal logicalDistanceEdge = static_cast<qreal>(miacode::preview_gameplay::kLogicalDistanceEdge);
        approach.distance = logicalDistanceTap;
        approach.scale = 0.0;
        const qreal holdTailGuideLifecycleDurationSeconds = static_cast<qreal>(tapFlyDurationSeconds_);
        if (deltaSeconds < -holdTailGuideLifecycleDurationSeconds) {
            return;
        }
        if (deltaSeconds < 0.0) {
            approach.distance = qBound<qreal>(
                logicalDistanceTap,
                logicalDistanceTap + (deltaSeconds + holdTailGuideLifecycleDurationSeconds)
                    * static_cast<qreal>(tapUnitsPerSecond_),
                logicalDistanceEdge
            );
            approach.scale = 1.0;
        } else {
            approach.distance = logicalDistanceEdge;
            approach.scale = 1.0;
        }
        if (approach.scale <= 0.0) {
            return;
        }
        const QPointF unit = laneUnitVector(lane);
        const QPointF logicalPos(
            kLogicalCanvasCenter + unit.x() * approach.distance,
            kLogicalCanvasCenter + unit.y() * approach.distance
        );
        renderGuideImage(image, approach.scale, laneRotationDegrees(lane), logicalPos, true, 0.0);
    };

    QHash<qint64, QVector<ActiveEachCandidate>> eachGroups;
    const auto addEachCandidate = [&eachGroups](const TimelineNoteMarker& marker) {
        const qint64 key = qRound64(marker.second * 1000000.0);
        eachGroups[key].append(ActiveEachCandidate{&marker});
    };

    for (const TimelineNoteMarker& marker : noteMarkers_) {
        if (marker.type == "tap" || marker.type == "slide" || marker.type == "wifi") {
            const bool slideHeadStar = marker.type == "slide" || marker.type == "wifi";
            if (slideHeadStar && !marker.hasHeadStar) {
                continue;
            }
            const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
            const TapApproachSample approach = sampleTapApproach(deltaSeconds);
            if (playheadSeconds_ > marker.second || approach.scale <= 0.0) {
                continue;
            }
            renderConcentricGuide(
                selectTapNoteGuideImage(marker),
                approach.distance,
                kNoteGuideSourceRadius,
                laneRotationDegrees(marker.lane),
                true,
                0.0
            );
            const bool inEachGroup = slideHeadStar ? marker.headEach : marker.isEach;
            if (inEachGroup) {
                addEachCandidate(marker);
            }
        } else if (marker.type == "hold") {
            if (marker.endSecond < marker.second || playheadSeconds_ > marker.endSecond) {
                continue;
            }
            const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
            const TapApproachSample approach = sampleTapApproach(deltaSeconds);
            if (approach.scale <= 0.0) {
                continue;
            }
            const QImage* headImage = marker.isBreak ? &noteGuideBreakImage_
                : marker.isEach ? &noteGuideEachImage_
                : &noteGuideNormalImage_;
            renderConcentricGuide(headImage, approach.distance, kNoteGuideSourceRadius, laneRotationDegrees(marker.lane), true, 0.0);

            const qreal deltaEndSeconds = static_cast<qreal>(playheadSeconds_ - marker.endSecond);
            if (sampleTapApproach(deltaEndSeconds).scale > 0.0) {
                renderTailGuide(selectHoldEndNoteGuideImage(marker), marker.lane, deltaEndSeconds);
            }
            if (marker.isEach) {
                addEachCandidate(marker);
            }
        }
    }

    for (auto it = eachGroups.cbegin(); it != eachGroups.cend(); ++it) {
        const QVector<ActiveEachCandidate>& notes = it.value();
        if (notes.size() < 2) {
            continue;
        }

        const int groupSize = notes.size();
        int laneDistance = 4;
        if (groupSize == 2) {
            const int a = notes[0].marker->lane;
            const int b = notes[1].marker->lane;
            const int delta = qAbs(a - b);
            laneDistance = qMin(delta, 8 - delta);
        }

        const QImage* lineImage = nullptr;
        qreal sourceRadius = kEachLine4SourceRadius;
        if (groupSize >= 3 || laneDistance == 4) {
            lineImage = &noteGuideEachLine4Image_;
            sourceRadius = kEachLine4SourceRadius;
        } else if (laneDistance == 3) {
            lineImage = &noteGuideEachLine3Image_;
            sourceRadius = kEachLine3SourceRadius;
        } else if (laneDistance == 2) {
            lineImage = &noteGuideEachLine2Image_;
            sourceRadius = kEachLine2SourceRadius;
        } else if (laneDistance == 1) {
            lineImage = &noteGuideEachLine1Image_;
            sourceRadius = kEachLine1SourceRadius;
        }
        if (lineImage == nullptr || lineImage->isNull()) {
            continue;
        }

        const TapApproachSample approach = sampleTapApproach(static_cast<qreal>(playheadSeconds_ - notes[0].marker->second));
        if (approach.scale <= 0.0) {
            continue;
        }

        qreal angleDegrees = 0.0;
        if (!(groupSize >= 3 || laneDistance == 4)) {
            const int a = notes[0].marker->lane;
            const int b = notes[1].marker->lane;
            int diff = (b - a) % 8;
            if (diff > 4) {
                diff -= 8;
            } else if (diff < -4) {
                diff += 8;
            }
            qreal midpoint = a + diff / 2.0;
            while (midpoint <= 0.0) {
                midpoint += 8.0;
            }
            while (midpoint > 8.0) {
                midpoint -= 8.0;
            }
            const qreal sourceMidpoint = 1.0 + laneDistance / 2.0;
            angleDegrees = laneRotationDegreesForIndex(midpoint) - laneRotationDegreesForIndex(sourceMidpoint);
        }

        renderConcentricGuide(lineImage, approach.distance, sourceRadius, angleDegrees, true, 0.0);
    }
}

void PreviewCanvas::drawTapMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    const bool slideHeadStar = marker.type == "slide" || marker.type == "wifi";
    if (slideHeadStar && !marker.hasHeadStar) {
        return;
    }

    const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
    if (slideHeadStar ? deltaSeconds >= 0.0 : deltaSeconds > 0.0) {
        return;
    }

    const TapApproachSample approach = sampleTapApproach(deltaSeconds);
    if (approach.scale <= 0.0) {
        return;
    }

    const QPointF unit = laneUnitVector(marker.lane);
    const QPointF logicalPoint(
        kLogicalCanvasCenter + unit.x() * approach.distance,
        kLogicalCanvasCenter + unit.y() * approach.distance
    );
    const QPointF point = mapLogicalPointToRect(logicalPoint, playfieldRect);
    const QImage* tapImage = slideHeadStar ? selectSlideStarImage(marker) : selectTapImage(marker);
    QImage renderImage = tapImage != nullptr ? *tapImage : QImage();
    if (slideHeadStar && marker.headEx && !renderImage.isNull()) {
        const QImage& overlay = (marker.sameHeadSlide && !starExDoubleImage_.isNull()) ? starExDoubleImage_ : starExImage_;
        if (!overlay.isNull()) {
            const QColor tint = exStarTintColor(marker.headBreak, marker.headEach);
            renderImage = composeOverlay(renderImage, overlay, kTapOverlayBaseMix, kTapOverlayAlphaMix, nullptr, &tint);
        }
    } else if (!slideHeadStar && marker.isEx && !renderImage.isNull() && !tapExImage_.isNull()) {
        const QColor tint = exTintColor(marker.isBreak, marker.isEach);
        renderImage = composeOverlay(renderImage, tapExImage_, kTapOverlayBaseMix, kTapOverlayAlphaMix, nullptr, &tint);
    }

    if (!renderImage.isNull()) {
        const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
        int targetWidth = 0;
        int targetHeight = 0;
        if (slideHeadStar) {
            const qreal baseWidth = (!tapImage_.isNull() ? tapImage_.width() * kSkinAssetScale : renderImage.width() * kStarAssetScale)
                * kSlideSpawnStarRelativeScale;
            const qreal baseHeight = (!tapImage_.isNull() ? tapImage_.height() * kSkinAssetScale : renderImage.height() * kStarAssetScale)
                * kSlideSpawnStarRelativeScale;
            targetWidth = qMax(1, qRound(baseWidth * canvasScale * approach.scale));
            targetHeight = qMax(1, qRound(baseHeight * canvasScale * approach.scale));
        } else {
            const qreal imageScale = canvasScale * approach.scale * kSkinAssetScale;
            targetWidth = qMax(1, qRound(renderImage.width() * imageScale));
            targetHeight = qMax(1, qRound(renderImage.height() * imageScale));
        }

        if (!drawSpriteImage(
                painter,
                renderImage,
                point,
                targetWidth,
                targetHeight,
                slideHeadStar ? mirroredStarAngleDegrees(laneRotationDegrees(marker.lane)) : laneRotationDegrees(marker.lane))) {
            return;
        }
        return;
    }

    const qreal radius = mapLogicalLengthToRect(kTapFallbackRadiusLogical * approach.scale, playfieldRect);
    const QColor fillColor = tapColorForMarker(marker);
    painter.setPen(QPen(QColor("#0F1720"), qMax<qreal>(kTapFallbackOuterStrokeMinWidth, radius * kTapFallbackOuterStrokeScale)));
    painter.setBrush(fillColor);
    painter.drawEllipse(point, radius, radius);

    painter.setPen(QPen(QColor("#FFFDF4"), qMax<qreal>(kTapFallbackInnerStrokeMinWidth, radius * kTapFallbackInnerStrokeScale)));
    painter.drawEllipse(point, radius * kTapFallbackInnerRadiusScale, radius * kTapFallbackInnerRadiusScale);
}

void PreviewCanvas::drawHoldMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (marker.endSecond < marker.second) {
        return;
    }

    const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
    const qreal deltaEndSeconds = static_cast<qreal>(playheadSeconds_ - marker.endSecond);
    if (deltaEndSeconds > 0.0) {
        return;
    }

    const TapApproachSample headApproach = sampleTapApproach(deltaSeconds);
    if (headApproach.scale <= 0.0) {
        return;
    }

    const QPointF unit = laneUnitVector(marker.lane);
    const QImage* holdImage = selectHoldImage(marker);
    QImage holdCapImage = holdImage != nullptr ? *holdImage : QImage();
    if (marker.isEx && !holdCapImage.isNull() && !holdExImage_.isNull()) {
        const QColor tint = exTintColor(marker.isBreak, marker.isEach);
        holdCapImage = composeOverlay(holdCapImage, holdExImage_, kHoldOverlayBaseMix, kHoldOverlayAlphaMix, nullptr, &tint);
    }
    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const auto drawHoldStripSlices = [&](const QPointF& center, int bodyLogicalLength, qreal scale) -> bool {
        if (holdCapImage.isNull()) {
            return false;
        }

        const int srcW = qMax(1, holdCapImage.width());
        const int srcH = qMax(1, holdCapImage.height());
        const int capRaw = qMax(1, qMin(srcH / 2, qRound(srcH * kHoldCapSliceRatioNumerator / kHoldCapSliceRatioDenominator)));
        const int midY = qBound(0, srcH / 2, srcH - 1);
        const int targetWidth = qMax(1, qRound(kHoldTargetWidth * canvasScale * scale));
        const int targetCapHeight = qMax(
            1,
            qRound(static_cast<qreal>(qRound(static_cast<qreal>(capRaw) * kHoldTargetWidth / srcW)) * canvasScale * scale)
        );
        const int targetBodyHeight = qMax(0, qRound(bodyLogicalLength * canvasScale * scale));
        const qreal capOffset = (targetBodyHeight + targetCapHeight) / 2.0;
        const qreal angle = laneRotationDegrees(marker.lane);
        const QPointF headCenter = center + unit * capOffset;
        const QPointF tailCenter = center - unit * capOffset;

        if (targetBodyHeight > 0) {
            drawSpriteImage(
                painter,
                holdCapImage,
                center,
                targetWidth,
                targetBodyHeight,
                angle,
                1.0,
                QRectF(0.0, midY, srcW, 1.0)
            );
        }
        drawSpriteImage(
            painter,
            holdCapImage,
            headCenter,
            targetWidth,
            targetCapHeight,
            angle,
            1.0,
            QRectF(0.0, 0.0, srcW, capRaw)
        );
        drawSpriteImage(
            painter,
            holdCapImage,
            tailCenter,
            targetWidth,
            targetCapHeight,
            angle,
            1.0,
            QRectF(0.0, srcH - capRaw, srcW, capRaw)
        );
        return true;
    };

    if (deltaSeconds < -static_cast<qreal>(tapFlyDurationSeconds_)) {
        const QPointF logicalPoint(
            kLogicalCanvasCenter + unit.x() * kLogicalDistanceTap,
            kLogicalCanvasCenter + unit.y() * kLogicalDistanceTap
        );
        const QPointF point = mapLogicalPointToRect(logicalPoint, playfieldRect);
        if (drawHoldStripSlices(point, 0, headApproach.scale)) {
            return;
        }

        const qreal radius = mapLogicalLengthToRect(kHoldFallbackCapRadiusLogical * headApproach.scale, playfieldRect);
        painter.setPen(QPen(QColor("#0F1720"), qMax<qreal>(kTapFallbackOuterStrokeMinWidth, radius * kTapFallbackOuterStrokeScale)));
        painter.setBrush(tapColorForMarker(marker));
        painter.drawEllipse(point, radius, radius);
        return;
    }

    const qreal distance = headApproach.distance;
    const qreal distanceEnd = sampleTapApproach(deltaEndSeconds).distance;

    const QPointF logicalHead(
        kLogicalCanvasCenter + unit.x() * distance,
        kLogicalCanvasCenter + unit.y() * distance
    );
    const QPointF logicalTail(
        kLogicalCanvasCenter + unit.x() * distanceEnd,
        kLogicalCanvasCenter + unit.y() * distanceEnd
    );
    const QPointF logicalCenter = (logicalHead + logicalTail) * 0.5;
    const QPointF centerPoint = mapLogicalPointToRect(logicalCenter, playfieldRect);
    const QPointF headPoint = mapLogicalPointToRect(logicalHead, playfieldRect);
    const QPointF tailPoint = mapLogicalPointToRect(logicalTail, playfieldRect);

    const int lineLength = qMax(0, qRound(distance - distanceEnd));
    if (drawHoldStripSlices(centerPoint, lineLength, 1.0)) {
        return;
    }

    const qreal bodyWidth = mapLogicalLengthToRect(kHoldFallbackBodyWidthLogical, playfieldRect);
    painter.setPen(QPen(tapColorForMarker(marker), qMax<qreal>(kHoldFallbackBodyMinWidth, bodyWidth), Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(tailPoint, headPoint);
}

void PreviewCanvas::drawCachedSlideArea(
    QPainter& painter,
    const TimelineNoteMarker& marker,
    int segmentIndex,
    int areaIndex,
    int localCut,
    const QRectF& playfieldRect,
    const QImage* image,
    qreal opacity,
    bool trimFromTail
)
{
    if (image == nullptr || image->isNull()) {
        return;
    }
    if (opacity <= 0.0) {
        return;
    }
    if (segmentIndex < 0 || segmentIndex >= marker.slideTrackAreaPoints.size()) {
        return;
    }
    const QVector<QVector<QPointF>>& segmentAreas = marker.slideTrackAreaPoints[segmentIndex];
    if (areaIndex < 0 || areaIndex >= segmentAreas.size()) {
        return;
    }
    const QVector<QPointF>& points = segmentAreas[areaIndex];
    if (points.isEmpty()) {
        return;
    }
    const QVector<double>& rotations = marker.slideTrackAreaRotations.value(segmentIndex).value(areaIndex);
    const int clampedCut = qBound(0, localCut, points.size());
    if (clampedCut >= points.size()) {
        return;
    }
    const int startPointIndex = trimFromTail ? 0 : clampedCut;
    const int endPointIndex = trimFromTail ? points.size() - clampedCut : points.size();
    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const int targetWidth = qMax(1, qRound(image->width() * canvasScale * kSlideTrackScale));
    const int targetHeight = qMax(1, qRound(image->height() * canvasScale * kSlideTrackScale));

    if (glRenderer_.isInitialized()) {
        const QImage* renderImage = image;
        QRectF resolvedSourceRect;
        resolveAtlasImage(*image, QRectF(), renderImage, resolvedSourceRect);
        if (renderImage != nullptr && !renderImage->isNull()) {
            QVector<QPointF> centers;
            QVector<qreal> angles;
            centers.reserve(qMax(0, endPointIndex - startPointIndex));
            angles.reserve(qMax(0, endPointIndex - startPointIndex));
            for (int pointIndex = startPointIndex; pointIndex < endPointIndex; ++pointIndex) {
                centers.append(mapLogicalPointToRect(
                    QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                    playfieldRect
                ));
                angles.append(-rotations.value(pointIndex));
            }

            bool renderedByGl = false;
            if (nativePaintingActive_) {
                renderedByGl = glRenderer_.drawImageQuadBatch(
                    *renderImage,
                    centers,
                    targetWidth,
                    targetHeight,
                    angles,
                    opacity,
                    resolvedSourceRect
                );
            } else {
                painter.beginNativePainting();
                renderedByGl = glRenderer_.drawImageQuadBatch(
                    *renderImage,
                    centers,
                    targetWidth,
                    targetHeight,
                    angles,
                    opacity,
                    resolvedSourceRect
                );
                painter.endNativePainting();
            }
            if (renderedByGl) {
                usedGpuRendererThisFrame_ = true;
                return;
            }
            if (nativePaintingActive_) {
                return;
            }
        }
    }

    const bool allowCpuTrackAreaCaching = kEnablePreviewCaches && cpuTrackAreaCachingEnabled_;
    const int playfieldWidth = qMax(1, qRound(playfieldRect.width()));
    const QString cacheKey = QStringLiteral("%1|%2|%3|%4|%5|%6|%7|%8|%9")
        .arg(marker.slideSegmentKeys.value(segmentIndex, marker.slideTrackKey))
        .arg(static_cast<qulonglong>(image->cacheKey()))
        .arg(segmentIndex)
        .arg(areaIndex)
        .arg(clampedCut)
        .arg(trimFromTail ? 1 : 0)
        .arg(playfieldWidth)
        .arg(qRound(playfieldRect.left()))
        .arg(qRound(playfieldRect.top()));

    CachedTrackArea uncached;
    CachedTrackArea* cachedArea = nullptr;
    auto cacheIt = slideTrackAreaCache_.end();
    if (allowCpuTrackAreaCaching) {
        cacheIt = slideTrackAreaCache_.find(cacheKey);
        if (cacheIt != slideTrackAreaCache_.end()) {
            cachedArea = &cacheIt.value();
        }
    }
    if (cachedArea == nullptr) {
        const qreal targetWidth = image->width() * canvasScale * kSlideTrackScale;
        const qreal targetHeight = image->height() * canvasScale * kSlideTrackScale;

        QRectF bounds;
        for (int pointIndex = startPointIndex; pointIndex < endPointIndex; ++pointIndex) {
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            );
            bounds = bounds.isNull()
                ? QRectF(
                    point.x() - targetWidth / 2.0,
                    point.y() - targetHeight / 2.0,
                    targetWidth,
                    targetHeight
                )
                : bounds.united(QRectF(
                    point.x() - targetWidth / 2.0,
                    point.y() - targetHeight / 2.0,
                    targetWidth,
                    targetHeight
                ));
        }

        if (bounds.isNull() || bounds.width() <= 0.0 || bounds.height() <= 0.0) {
            return;
        }

        const QRect pixelBounds = bounds.adjusted(-1.0, -1.0, 1.0, 1.0).toAlignedRect();
        CachedTrackArea built;
        built.image = QImage(pixelBounds.size(), QImage::Format_ARGB32_Premultiplied);
        built.image.fill(Qt::transparent);
        built.offset = pixelBounds.topLeft();

        QPainter cachePainter(&built.image);
        cachePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (int pointIndex = startPointIndex; pointIndex < endPointIndex; ++pointIndex) {
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            ) - built.offset;
            const qreal angle = rotations.value(pointIndex);
            cachePainter.save();
            cachePainter.translate(point);
            cachePainter.rotate(-angle);
            cachePainter.drawImage(
                QRectF(-targetWidth / 2.0, -targetHeight / 2.0, targetWidth, targetHeight),
                *image
            );
            cachePainter.restore();
        }
        if (allowCpuTrackAreaCaching) {
            cacheIt = slideTrackAreaCache_.insert(cacheKey, built);
            cachedArea = &cacheIt.value();
        } else {
            uncached = built;
            cachedArea = &uncached;
        }
    }

    if (cachedArea != nullptr) {
        painter.save();
        painter.setOpacity(opacity);
        painter.drawImage(cachedArea->offset, cachedArea->image);
        painter.restore();
    }
}

void PreviewCanvas::drawCachedWifiArea(
    QPainter& painter,
    const TimelineNoteMarker& marker,
    int areaIndex,
    int localCut,
    const QRectF& playfieldRect,
    qreal opacity
)
{
    if (opacity <= 0.0) {
        return;
    }
    if (areaIndex < 0 || areaIndex >= marker.wifiTrackAreaPoints.size()) {
        return;
    }
    const QVector<QPointF>& points = marker.wifiTrackAreaPoints[areaIndex];
    if (points.isEmpty()) {
        return;
    }
    const QVector<double>& rotations = marker.wifiTrackAreaRotations.value(areaIndex);
    const QVector<int>& imageIndices = marker.wifiTrackAreaImageIndices.value(areaIndex);
    const int clampedCut = qBound(0, localCut, points.size());
    const int visibleCount = points.size() - clampedCut;
    if (visibleCount <= 0) {
        return;
    }

    if (glRenderer_.isInitialized()) {
        const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
        for (int pointIndex = clampedCut; pointIndex < points.size(); ++pointIndex) {
            const int imageIndex = imageIndices.value(pointIndex, pointIndex);
            const QImage* image = selectWifiTrackImage(marker, imageIndex, 0);
            if (image == nullptr || image->isNull()) {
                continue;
            }
            const int targetWidth = qMax(1, qRound(image->width() * canvasScale * kSlideTrackScale));
            const int targetHeight = qMax(1, qRound(image->height() * canvasScale * kSlideTrackScale));
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            );
            const qreal angle = -rotations.value(pointIndex);
            drawSpriteImage(painter, *image, point, targetWidth, targetHeight, angle, opacity);
        }
        return;
    }

    int variantTag = 0;
    if (marker.trackBreak && !wifiBreakImages_.isEmpty()) {
        variantTag = 2;
    } else if (marker.slideEach && !wifiEachImages_.isEmpty()) {
        variantTag = 1;
    }

    const bool allowCpuTrackAreaCaching = kEnablePreviewCaches && cpuTrackAreaCachingEnabled_;
    const int playfieldWidth = qMax(1, qRound(playfieldRect.width()));
    const QString cacheKey = QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(marker.slideTrackKey)
        .arg(variantTag)
        .arg(areaIndex)
        .arg(clampedCut)
        .arg(playfieldWidth)
        .arg(qRound(playfieldRect.left()))
        .arg(qRound(playfieldRect.top()));

    CachedTrackArea uncached;
    CachedTrackArea* cachedArea = nullptr;
    auto cacheIt = wifiTrackAreaCache_.end();
    if (allowCpuTrackAreaCaching) {
        cacheIt = wifiTrackAreaCache_.find(cacheKey);
        if (cacheIt != wifiTrackAreaCache_.end()) {
            cachedArea = &cacheIt.value();
        }
    }
    if (cachedArea == nullptr) {
        QRectF bounds;

        for (int pointIndex = clampedCut; pointIndex < points.size(); ++pointIndex) {
            const int imageIndex = imageIndices.value(pointIndex, pointIndex);
            const QImage* image = selectWifiTrackImage(marker, imageIndex, 0);
            if (image == nullptr || image->isNull()) {
                continue;
            }
            const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
            const qreal targetWidth = image->width() * canvasScale * kSlideTrackScale;
            const qreal targetHeight = image->height() * canvasScale * kSlideTrackScale;
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            );
            bounds = bounds.isNull()
                ? QRectF(point.x() - targetWidth / 2.0, point.y() - targetHeight / 2.0, targetWidth, targetHeight)
                : bounds.united(QRectF(point.x() - targetWidth / 2.0, point.y() - targetHeight / 2.0, targetWidth, targetHeight));
        }

        if (bounds.isNull() || bounds.width() <= 0.0 || bounds.height() <= 0.0) {
            return;
        }

        const QRect pixelBounds = bounds.adjusted(-1.0, -1.0, 1.0, 1.0).toAlignedRect();
        CachedTrackArea built;
        built.image = QImage(pixelBounds.size(), QImage::Format_ARGB32_Premultiplied);
        built.image.fill(Qt::transparent);
        built.offset = pixelBounds.topLeft();

        QPainter cachePainter(&built.image);
        cachePainter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        for (int pointIndex = clampedCut; pointIndex < points.size(); ++pointIndex) {
            const int imageIndex = imageIndices.value(pointIndex, pointIndex);
            const QImage* image = selectWifiTrackImage(marker, imageIndex, 0);
            if (image == nullptr || image->isNull()) {
                continue;
            }
            const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
            const qreal targetWidth = image->width() * canvasScale * kSlideTrackScale;
            const qreal targetHeight = image->height() * canvasScale * kSlideTrackScale;
            const QPointF point = mapLogicalPointToRect(
                QPointF(kLogicalCanvasCenter + points[pointIndex].x(), kLogicalCanvasCenter + points[pointIndex].y()),
                playfieldRect
            ) - built.offset;
            const qreal angle = rotations.value(pointIndex);
            cachePainter.save();
            cachePainter.translate(point);
            cachePainter.rotate(-angle);
            cachePainter.drawImage(
                QRectF(-targetWidth / 2.0, -targetHeight / 2.0, targetWidth, targetHeight),
                *image
            );
            cachePainter.restore();
        }
        if (allowCpuTrackAreaCaching) {
            cacheIt = wifiTrackAreaCache_.insert(cacheKey, built);
            cachedArea = &cacheIt.value();
        } else {
            uncached = built;
            cachedArea = &uncached;
        }
    }

    if (cachedArea != nullptr) {
        painter.save();
        painter.setOpacity(opacity);
        painter.drawImage(cachedArea->offset, cachedArea->image);
        painter.restore();
    }
}

int slideAreaTrimCountForProportion(
    const QVector<QPointF>& areaPoints,
    const QVector<double>& areaThresholds,
    const QVector<double>& areaCheckpoints,
    const QVector<int>& areaCutIndices,
    int areaIndex,
    qreal proportion
)
{
    if (areaPoints.isEmpty()) {
        return 0;
    }

    qreal areaStart = 0.0;
    qreal areaEnd = 1.0;
    if (!areaThresholds.isEmpty()) {
        areaStart = qBound<qreal>(0.0, areaThresholds.value(areaIndex, 0.0), 1.0);
        if (areaIndex + 1 < areaThresholds.size()) {
            areaEnd = qBound<qreal>(areaStart, areaThresholds.at(areaIndex + 1), 1.0);
        }
    }

    const qreal clampedProportion = qBound<qreal>(0.0, proportion, 1.0);
    if (!areaCheckpoints.isEmpty()) {
        const int checkpointCount = areaCheckpoints.size();
        const auto cutAt = [&](int index) {
            if (!areaCutIndices.isEmpty()) {
                return qBound(0, areaCutIndices.value(index, 0), areaPoints.size());
            }
            return qBound(
                0,
                qFloor(static_cast<qreal>(index + 1) * areaPoints.size() / checkpointCount),
                areaPoints.size()
            );
        };

        qreal previousThreshold = areaStart;
        int previousCut = 0;
        for (int checkpointIndex = 0; checkpointIndex < checkpointCount; ++checkpointIndex) {
            const qreal checkpointThreshold = qBound<qreal>(
                previousThreshold,
                areaCheckpoints.at(checkpointIndex),
                areaEnd
            );
            const int checkpointCut = cutAt(checkpointIndex);
            if (clampedProportion <= checkpointThreshold) {
                const qreal span = qMax<qreal>(0.001, checkpointThreshold - previousThreshold);
                const qreal localT = qBound<qreal>(0.0, (clampedProportion - previousThreshold) / span, 1.0);
                return qBound(
                    0,
                    qFloor(previousCut + (checkpointCut - previousCut) * localT),
                    areaPoints.size()
                );
            }
            previousThreshold = checkpointThreshold;
            previousCut = checkpointCut;
        }

        if (areaEnd > previousThreshold && previousCut < areaPoints.size()) {
            const qreal span = qMax<qreal>(0.001, areaEnd - previousThreshold);
            const qreal localT = qBound<qreal>(0.0, (clampedProportion - previousThreshold) / span, 1.0);
            return qBound(
                0,
                qFloor(previousCut + (areaPoints.size() - previousCut) * localT),
                areaPoints.size()
            );
        }
        return previousCut;
    }

    const qreal span = qMax<qreal>(0.001, areaEnd - areaStart);
    const qreal localT = qBound<qreal>(0.0, (clampedProportion - areaStart) / span, 1.0);
    return qBound(0, qFloor(localT * areaPoints.size()), areaPoints.size());
}

int totalSlideTrackArrowCount(const QVector<QVector<QVector<QPointF>>>& segmentAreas)
{
    int totalArrowCount = 0;
    for (const QVector<QVector<QPointF>>& areas : segmentAreas) {
        for (const QVector<QPointF>& areaPoints : areas) {
            totalArrowCount += areaPoints.size();
        }
    }
    return totalArrowCount;
}

int totalWifiTrackArrowCount(const QVector<QVector<QPointF>>& areas)
{
    int totalArrowCount = 0;
    for (const QVector<QPointF>& areaPoints : areas) {
        totalArrowCount += areaPoints.size();
    }
    return totalArrowCount;
}

void PreviewCanvas::drawSlideTrack(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (!muriRenderOptions_.showSlideTracks) {
        return;
    }
    if (marker.availableSecond < 0.0
        || marker.slideTrackAreaPoints.isEmpty()
        || playheadSeconds_ < marker.second - slideTrackAppearLeadInSeconds_
        || (marker.endSecond > marker.slideTraceSecond && playheadSeconds_ >= marker.endSecond)) {
        return;
    }
    const QImage* image = selectSlideTrackImage(marker);
    if (image->isNull()) {
        return;
    }

    if (muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle) {
        const MarkerMuriState* state = markerMuriState(marker);
        if (state != nullptr && !state->slideSegments.isEmpty()) {
            qreal opacity = 1.0;
            if (playheadSeconds_ < marker.slideTraceSecond) {
                opacity = sampleSlideTrackPreTraceOpacity(marker.second, playheadSeconds_);
                if (opacity < 0.0) {
                    return;
                }
            }

            painter.save();
            painter.setOpacity(opacity);
            for (int segmentIndex = marker.slideTrackAreaPoints.size() - 1; segmentIndex >= 0; --segmentIndex) {
                const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
                const MuriSegmentState& segmentState = state->slideSegments.value(segmentIndex);
                for (int areaIndex = areas.size() - 1; areaIndex >= 0; --areaIndex) {
                    const QVector<MuriCheckpointState>& checkpoints = segmentState.areaCheckpoints.value(areaIndex);
                    if (checkpoints.isEmpty()) {
                        if (segmentState.completedSecond >= 0.0
                            && playheadSeconds_ >= segmentState.completedSecond - 1e-6) {
                            continue;
                        }
                        drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, 0, playfieldRect, image, opacity);
                        continue;
                    }

                    const int passed = passedCheckpointCount(checkpoints, playheadSeconds_);
                    if (passed >= checkpoints.size()) {
                        continue;
                    }
                    const int localCut = slideAreaCutForPassedCount(
                        marker.slideTrackAreaCutIndices.value(segmentIndex).value(areaIndex),
                        passed,
                        areas[areaIndex].size()
                    );
                    drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, localCut, playfieldRect, image, opacity);
                }
            }
            painter.restore();

            const qreal flashOpacity = muriFlashOpacity(state, playheadSeconds_);
            if (flashOpacity > 0.0) {
                for (int segmentIndex = marker.slideTrackAreaPoints.size() - 1; segmentIndex >= 0; --segmentIndex) {
                    const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
                    for (int areaIndex = areas.size() - 1; areaIndex >= 0; --areaIndex) {
                        drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, 0, playfieldRect, image, flashOpacity);
                    }
                }
            }
            return;
        }
    }

    int startSegment = 0;
    int startAreaIndex = 0;
    int partialTrimCount = 0;
    int removedArrowCount = 0;
    qreal startProportion = 0.0;
    qreal opacity = 1.0;
    if (playheadSeconds_ < marker.slideTraceSecond) {
        opacity = sampleSlideTrackPreTraceOpacity(marker.second, playheadSeconds_);
        if (opacity < 0.0) {
            return;
        }
    } else {
        if (kSlideTrackTrimMode == SlideTrackTrimMode::AreaImmediate
            && !marker.slideSegmentShootSeconds.isEmpty()
            && marker.slideSegmentShootSeconds.size() == marker.slideSegmentDurations.size()
            && marker.slideTrackAreaPoints.size() == marker.slideSegmentDurations.size()) {
            for (int i = marker.slideSegmentShootSeconds.size() - 1; i >= 0; --i) {
                if (playheadSeconds_ >= marker.slideSegmentShootSeconds[i]) {
                    startSegment = i;
                    break;
                }
            }
            startSegment = qBound(0, startSegment, marker.slideTrackAreaPoints.size() - 1);
            const qreal duration = qMax(0.001, marker.slideSegmentDurations.value(startSegment));
            startProportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideSegmentShootSeconds[startSegment]) / duration, 1.0);
            const int areaCount = marker.slideTrackAreaPoints[startSegment].size();
            startAreaIndex = currentAreaIndexForProportion(marker.slideTrackAreaThresholds.value(startSegment), startProportion, areaCount);

            if (startAreaIndex >= 0 && startAreaIndex < areaCount) {
                partialTrimCount = slideAreaTrimCountForProportion(
                    marker.slideTrackAreaPoints[startSegment][startAreaIndex],
                    marker.slideTrackAreaThresholds.value(startSegment),
                    marker.slideTrackAreaCheckpoints.value(startSegment).value(startAreaIndex),
                    marker.slideTrackAreaCutIndices.value(startSegment).value(startAreaIndex),
                    startAreaIndex,
                    startProportion
                );
            }
        } else {
            const qreal totalDuration = qMax(0.001, marker.endSecond - marker.slideTraceSecond);
            const qreal totalProportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideTraceSecond) / totalDuration, 1.0);
            const int totalArrowCount = totalSlideTrackArrowCount(marker.slideTrackAreaPoints);
            removedArrowCount = qBound(0, qFloor(totalProportion * totalArrowCount), totalArrowCount);
        }
    }

    painter.save();
    painter.setOpacity(opacity);

    if (kSlideTrackTrimMode == SlideTrackTrimMode::AreaImmediate) {
        for (int segmentIndex = marker.slideTrackAreaPoints.size() - 1; segmentIndex > startSegment; --segmentIndex) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
            for (int areaIndex = areas.size() - 1; areaIndex >= 0; --areaIndex) {
                drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, 0, playfieldRect, image, opacity);
            }
        }
        if (startSegment >= 0 && startSegment < marker.slideTrackAreaPoints.size()) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[startSegment];
            const int clampedStartArea = qBound(0, startAreaIndex, areas.size());
            if (clampedStartArea >= 0 && clampedStartArea < areas.size()) {
                partialTrimCount = slideAreaTrimCountForProportion(
                    areas[clampedStartArea],
                    marker.slideTrackAreaThresholds.value(startSegment),
                    marker.slideTrackAreaCheckpoints.value(startSegment).value(clampedStartArea),
                    marker.slideTrackAreaCutIndices.value(startSegment).value(clampedStartArea),
                    clampedStartArea,
                    startProportion
                );
            }
            for (int areaIndex = areas.size() - 1; areaIndex >= clampedStartArea; --areaIndex) {
                const int localCut = areaIndex == clampedStartArea ? partialTrimCount : 0;
                drawCachedSlideArea(painter, marker, startSegment, areaIndex, localCut, playfieldRect, image, opacity);
            }
        }
    } else {
        int trimSegment = marker.slideTrackAreaPoints.size();
        int trimArea = 0;
        int trimLocalCut = 0;
        int remainingToRemove = removedArrowCount;

        for (int segmentIndex = 0; segmentIndex < marker.slideTrackAreaPoints.size(); ++segmentIndex) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
            int segmentArrowCount = 0;
            for (const QVector<QPointF>& areaPoints : areas) {
                segmentArrowCount += areaPoints.size();
            }
            if (remainingToRemove >= segmentArrowCount) {
                remainingToRemove -= segmentArrowCount;
                continue;
            }

            trimSegment = segmentIndex;
            for (int areaIndex = 0; areaIndex < areas.size(); ++areaIndex) {
                const int areaArrowCount = areas[areaIndex].size();
                if (remainingToRemove < areaArrowCount) {
                    trimArea = areaIndex;
                    trimLocalCut = qBound(0, remainingToRemove, areaArrowCount);
                    remainingToRemove = 0;
                    break;
                }
                remainingToRemove -= areaArrowCount;
            }
            break;
        }

        for (int segmentIndex = marker.slideTrackAreaPoints.size() - 1; segmentIndex > trimSegment; --segmentIndex) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[segmentIndex];
            for (int areaIndex = areas.size() - 1; areaIndex >= 0; --areaIndex) {
                drawCachedSlideArea(painter, marker, segmentIndex, areaIndex, 0, playfieldRect, image, opacity);
            }
        }
        if (trimSegment >= 0 && trimSegment < marker.slideTrackAreaPoints.size()) {
            const QVector<QVector<QPointF>>& areas = marker.slideTrackAreaPoints[trimSegment];
            for (int areaIndex = areas.size() - 1; areaIndex >= trimArea; --areaIndex) {
                const int localCut = areaIndex == trimArea ? trimLocalCut : 0;
                drawCachedSlideArea(
                    painter,
                    marker,
                    trimSegment,
                    areaIndex,
                    localCut,
                    playfieldRect,
                    image,
                    opacity,
                    areaIndex == trimArea
                );
            }
        }
    }
    painter.restore();
}

void PreviewCanvas::drawWifiTrack(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (!muriRenderOptions_.showSlideTracks) {
        return;
    }
    if (marker.availableSecond < 0.0
        || marker.wifiTrackAreaPoints.isEmpty()
        || playheadSeconds_ < marker.second - slideTrackAppearLeadInSeconds_
        || (marker.endSecond > marker.slideTraceSecond && playheadSeconds_ >= marker.endSecond)) {
        return;
    }
    qreal opacity = 1.0;
    int startAreaIndex = 0;
    qreal startProportion = 0.0;
    int removedArrowCount = 0;
    const MarkerMuriState* state = markerMuriState(marker);
    if (playheadSeconds_ < marker.slideTraceSecond) {
        opacity = sampleSlideTrackPreTraceOpacity(marker.second, playheadSeconds_);
        if (opacity < 0.0) {
            return;
        }
    } else if (playheadSeconds_ >= marker.slideTraceSecond) {
        const qreal totalDuration = qMax(0.001, marker.endSecond - marker.slideTraceSecond);
        startProportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideTraceSecond) / totalDuration, 1.0);
        if (kSlideTrackTrimMode == SlideTrackTrimMode::AreaImmediate) {
            startAreaIndex = currentAreaIndexForProportion(marker.wifiTrackAreaThresholds, startProportion, marker.wifiTrackAreaPoints.size());
        } else {
            const int totalArrowCount = totalWifiTrackArrowCount(marker.wifiTrackAreaPoints);
            removedArrowCount = qBound(0, qFloor(startProportion * totalArrowCount), totalArrowCount);
        }
    }

    if (muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle
        && state != nullptr
        && !state->wifiAreas.isEmpty()) {
        painter.save();
        painter.setOpacity(opacity);
        for (int areaIndex = marker.wifiTrackAreaPoints.size() - 1; areaIndex >= 0; --areaIndex) {
            const QVector<MuriCheckpointState>& checkpoints = state->wifiAreas.value(areaIndex);
            if (checkpoints.isEmpty()) {
                if (state->wifiCompletedSecond >= 0.0 && playheadSeconds_ >= state->wifiCompletedSecond - 1e-6) {
                    continue;
                }
                drawCachedWifiArea(painter, marker, areaIndex, 0, playfieldRect, opacity);
                continue;
            }

            const int passed = passedCheckpointCount(checkpoints, playheadSeconds_);
            if (passed >= checkpoints.size()) {
                continue;
            }
            const int localCut = checkpoints.isEmpty()
                ? 0
                : qBound(
                      0,
                      qFloor(static_cast<qreal>(passed) * marker.wifiTrackAreaPoints[areaIndex].size() / checkpoints.size()),
                      marker.wifiTrackAreaPoints[areaIndex].size()
                  );
            drawCachedWifiArea(painter, marker, areaIndex, localCut, playfieldRect, opacity);
        }
        painter.restore();

        const qreal flashOpacity = muriFlashOpacity(state, playheadSeconds_);
        if (flashOpacity > 0.0) {
            for (int areaIndex = marker.wifiTrackAreaPoints.size() - 1; areaIndex >= 0; --areaIndex) {
                drawCachedWifiArea(painter, marker, areaIndex, 0, playfieldRect, flashOpacity);
            }
        }
        return;
    }

    painter.save();
    const int clampedStartArea = qBound(0, startAreaIndex, marker.wifiTrackAreaPoints.size());
    int partialTrimCount = 0;
    if (kSlideTrackTrimMode == SlideTrackTrimMode::AreaImmediate
        && clampedStartArea >= 0
        && clampedStartArea < marker.wifiTrackAreaPoints.size()) {
        const QVector<double>& areaCheckpoints = marker.wifiTrackAreaCheckpoints.value(clampedStartArea);
        if (!areaCheckpoints.isEmpty()) {
            int passedCheckpoints = 0;
            for (double checkpoint : areaCheckpoints) {
                if (startProportion >= checkpoint) {
                    ++passedCheckpoints;
                } else {
                    break;
                }
            }
            partialTrimCount = qBound(
                0,
                qFloor(static_cast<qreal>(passedCheckpoints) * marker.wifiTrackAreaPoints[clampedStartArea].size() / areaCheckpoints.size()),
                marker.wifiTrackAreaPoints[clampedStartArea].size()
            );
        }
    }
    if (kSlideTrackTrimMode == SlideTrackTrimMode::AreaImmediate) {
        for (int areaIndex = marker.wifiTrackAreaPoints.size() - 1; areaIndex >= clampedStartArea; --areaIndex) {
            const int localCut = areaIndex == clampedStartArea ? partialTrimCount : 0;
            drawCachedWifiArea(painter, marker, areaIndex, localCut, playfieldRect, opacity);
        }
    } else {
        int trimArea = marker.wifiTrackAreaPoints.size();
        int trimLocalCut = 0;
        int remainingToRemove = removedArrowCount;
        for (int areaIndex = 0; areaIndex < marker.wifiTrackAreaPoints.size(); ++areaIndex) {
            const int areaArrowCount = marker.wifiTrackAreaPoints[areaIndex].size();
            if (remainingToRemove < areaArrowCount) {
                trimArea = areaIndex;
                trimLocalCut = qBound(0, remainingToRemove, areaArrowCount);
                remainingToRemove = 0;
                break;
            }
            remainingToRemove -= areaArrowCount;
        }
        for (int areaIndex = marker.wifiTrackAreaPoints.size() - 1; areaIndex >= trimArea; --areaIndex) {
            const int localCut = areaIndex == trimArea ? trimLocalCut : 0;
            drawCachedWifiArea(painter, marker, areaIndex, localCut, playfieldRect, opacity);
        }
    }
    painter.restore();
}

void PreviewCanvas::drawMuriPadStateOverlay(QPainter& painter, const QRectF& playfieldRect)
{
    if (!muriRenderOptions_.showJudgeMarkers || muriAnalysisReport_.padWindows.isEmpty()) {
        return;
    }

    const bool resumeNativeBatch = nativePaintingActive_;
    if (resumeNativeBatch) {
        endNativeBatch(painter);
    }

    QHash<QString, int> activePadCounts;
    for (const MuriPadWindow& window : muriAnalysisReport_.padWindows) {
        if (window.startSecond <= playheadSeconds_ + 1e-6 && window.endSecond + 1e-6 >= playheadSeconds_) {
            activePadCounts[window.pad] += 1;
        }
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    const bool maimuriDxStyle = muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle;
    for (auto it = activePadCounts.cbegin(); it != activePadCounts.cend(); ++it) {
        const QPointF logicalCenter = miacode::muri::padCenter(it.key());
        const double logicalRadius = miacode::muri::padRadius(it.key());
        if (logicalRadius <= 0.0) {
            continue;
        }
        const QPointF center = mapLogicalPointToRect(logicalCenter, playfieldRect);
        const qreal radius = qMax<qreal>(2.0, mapLogicalLengthToRect(logicalRadius, playfieldRect));
        if (maimuriDxStyle) {
            painter.setBrush(QColor(255, 255, 0));
            painter.setPen(Qt::NoPen);
            painter.drawEllipse(center, radius, radius);
            continue;
        }
        QColor fillColor(255, 225, 84, qBound(90, 110 + it.value() * 26, 200));
        QColor strokeColor(255, 248, 187, qBound(130, 170 + it.value() * 18, 255));
        painter.setBrush(fillColor);
        painter.setPen(QPen(strokeColor, qMax<qreal>(2.0, radius * 0.08)));
        painter.drawEllipse(center, radius, radius);
    }
    painter.restore();

    if (resumeNativeBatch) {
        beginNativeBatch(painter);
    }
}

void PreviewCanvas::drawMuriActionOverlay(QPainter& painter, const QRectF& playfieldRect)
{
    if (!muriRenderOptions_.showTouchTrail || muriAnalysisReport_.actionTrails.isEmpty()) {
        return;
    }

    const bool resumeNativeBatch = nativePaintingActive_;
    if (resumeNativeBatch) {
        endNativeBatch(painter);
    }

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    if (muriRenderOptions_.renderMode == RenderMode::MaimuriDxStyle) {
        constexpr qreal kPressEffectLifetimeSeconds = static_cast<qreal>(36.0 / miacode::muri::kJudgeTps);
        constexpr qreal kPressEffectTickSeconds = static_cast<qreal>(1.0 / miacode::muri::kJudgeTps);
        constexpr qreal kPressEffectSlideRadiusEndScale = 0.5;
        const qreal playheadSecond = static_cast<qreal>(playheadSeconds_);
        const qreal sampleStartSecond = playheadSecond - kPressEffectLifetimeSeconds;
        QVector<const MuriActionTrail*> visibleTrails;
        visibleTrails.reserve(muriAnalysisReport_.actionTrails.size());
        for (const MuriActionTrail& trail : muriAnalysisReport_.actionTrails) {
            if (trail.endSecond + 1e-6 < sampleStartSecond || trail.startSecond > playheadSecond + 1e-6) {
                continue;
            }
            if (trail.points.isEmpty()) {
                continue;
            }
            visibleTrails.append(&trail);
        }

        if (!visibleTrails.isEmpty()) {
            const int startTick = qMax(
                0,
                static_cast<int>(qFloor(sampleStartSecond * miacode::muri::kJudgeTps))
            );
            const int endTick = qMax(
                startTick,
                static_cast<int>(qFloor(playheadSecond * miacode::muri::kJudgeTps + 1e-6))
            );

            for (int tick = startTick; tick <= endTick; ++tick) {
                const qreal sampleSecond = static_cast<qreal>(tick) * kPressEffectTickSeconds;
                QVector<const MuriActionTrail*> activeTrails;
                activeTrails.reserve(visibleTrails.size());
                for (const MuriActionTrail* trail : visibleTrails) {
                    if (trail == nullptr) {
                        continue;
                    }
                    if (trail->startSecond <= sampleSecond + 1e-6
                        && trail->endSecond + 1e-6 >= sampleSecond) {
                        activeTrails.append(trail);
                    }
                }
                if (activeTrails.isEmpty()) {
                    continue;
                }

                const qreal ageSeconds = playheadSecond - sampleSecond;
                if (ageSeconds < 0.0 || ageSeconds > kPressEffectLifetimeSeconds) {
                    continue;
                }
                const qreal t = qBound<qreal>(0.0, ageSeconds / kPressEffectLifetimeSeconds, 1.0);
                QColor fillColor = activeTrails.size() > 2
                    ? QColor(224, 108, 117)
                    : QColor(255, 255, 255);
                fillColor.setAlpha(qBound(0, qRound((1.0 - t) * 255.0), 255));
                painter.setPen(Qt::NoPen);
                painter.setBrush(fillColor);

                for (const MuriActionTrail* trail : activeTrails) {
                    qreal proportion = 0.0;
                    if (trail->endSecond > trail->startSecond) {
                        proportion = qBound<qreal>(
                            0.0,
                            (sampleSecond - trail->startSecond) / (trail->endSecond - trail->startSecond),
                            1.0
                        );
                    }
                    const QPointF logicalPoint = trail->points.size() == 1
                        ? trail->points.constFirst()
                        : interpolatePoint(trail->points, proportion);
                    const QPointF center = mapLogicalPointToRect(logicalPoint, playfieldRect);
                    qreal logicalRadius = static_cast<qreal>(trail->radius);
                    if (trail->sourceType == QLatin1String("slide")
                        || trail->sourceType == QLatin1String("wifi")) {
                        logicalRadius *= (1.0 - (1.0 - kPressEffectSlideRadiusEndScale) * t);
                    }
                    const qreal radius = qMax<qreal>(1.0, mapLogicalLengthToRect(logicalRadius, playfieldRect));
                    painter.drawEllipse(center, radius, radius);
                }
            }
        }

        painter.restore();

        if (resumeNativeBatch) {
            beginNativeBatch(painter);
        }
        return;
    }

    for (const MuriActionTrail& trail : muriAnalysisReport_.actionTrails) {
        if (trail.startSecond > playheadSeconds_ + 1e-6 || trail.endSecond + 1e-6 < playheadSeconds_) {
            continue;
        }
        if (trail.points.isEmpty()) {
            continue;
        }

        qreal proportion = 0.0;
        if (trail.endSecond > trail.startSecond) {
            proportion = qBound<qreal>(
                0.0,
                static_cast<qreal>((playheadSeconds_ - trail.startSecond) / (trail.endSecond - trail.startSecond)),
                1.0
            );
        }
        const QPointF logicalPoint = trail.points.size() == 1
            ? trail.points.constFirst()
            : interpolatePoint(trail.points, proportion);
        const QPointF center = mapLogicalPointToRect(logicalPoint, playfieldRect);
        const qreal radius = qMax<qreal>(2.0, mapLogicalLengthToRect(trail.radius, playfieldRect));
        QColor fillColor = trail.sourceType == QLatin1String("wifi")
            ? QColor(75, 218, 255, 60)
            : QColor(255, 112, 84, 64);
        QColor strokeColor = trail.sourceType == QLatin1String("wifi")
            ? QColor(150, 240, 255, 220)
            : QColor(255, 182, 120, 220);
        painter.setBrush(fillColor);
        painter.setPen(QPen(strokeColor, qMax<qreal>(2.0, radius * 0.08)));
        painter.drawEllipse(center, radius, radius);
    }
    painter.restore();

    if (resumeNativeBatch) {
        beginNativeBatch(painter);
    }
}

void PreviewCanvas::drawSlideMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (marker.slideTraceSecond <= marker.second || marker.slideSegmentPoints.isEmpty()) {
        return;
    }

    qreal angle = 0.0;
    QPointF logicalPoint;
    qreal imageScale = kStarAssetScale;
    qreal imageOpacity = 1.0;
    const QImage* starImage = nullptr;

    if (playheadSeconds_ < marker.second || playheadSeconds_ > marker.endSecond) {
        return;
    }

    if (playheadSeconds_ < marker.slideTraceSecond) {
        if (!marker.hasHeadStar) {
            return;
        }
        starImage = selectSlideMovingStarImage(marker);
        if (starImage->isNull()) {
            return;
        }
        const QVector<QPointF>& points = marker.slideSegmentPoints.constFirst();
        const QVector<double>& angles = marker.slideSegmentAngles.constFirst();
        if (points.isEmpty()) {
            return;
        }
        logicalPoint = points.constFirst();
        angle = angles.isEmpty() ? 0.0 : angles.constFirst();
        const qreal waitDuration = qMax(kRenderDurationEpsilon, marker.slideTraceSecond - marker.second);
        const qreal waitT = qBound<qreal>(0.0, (playheadSeconds_ - marker.second) / waitDuration, 1.0);
        const qreal startScale = slideStartupStarInitialScale(*starImage);
        imageScale = startScale + (kStarAssetScale - startScale) * waitT;
        imageOpacity = kObjectWaitStateStartOpacity + kObjectWaitStateOpacityDelta * waitT;
    } else {
        starImage = selectSlideMovingStarImage(marker);
        if (starImage->isNull()) {
            return;
        }
        int segmentIndex = 0;
        if (!marker.slideSegmentShootSeconds.isEmpty() && marker.slideSegmentShootSeconds.size() == marker.slideSegmentDurations.size()) {
            for (int i = marker.slideSegmentShootSeconds.size() - 1; i >= 0; --i) {
                if (playheadSeconds_ >= marker.slideSegmentShootSeconds[i]) {
                    segmentIndex = i;
                    break;
                }
            }
        }
        segmentIndex = qBound(0, segmentIndex, marker.slideSegmentPoints.size() - 1);
        const QVector<QPointF>& points = marker.slideSegmentPoints[segmentIndex];
        const QVector<double>& angles = marker.slideSegmentAngles.value(segmentIndex);
        if (points.isEmpty()) {
            return;
        }
        qreal proportion = 1.0;
        if (segmentIndex < marker.slideSegmentShootSeconds.size() && segmentIndex < marker.slideSegmentDurations.size()) {
            const qreal duration = qMax(kRenderDurationEpsilon, marker.slideSegmentDurations[segmentIndex]);
            proportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideSegmentShootSeconds[segmentIndex]) / duration, 1.0);
        }
        logicalPoint = interpolatePoint(points, proportion);
        angle = interpolateAngle(angles, proportion);
    }
    angle = mirroredStarAngleDegrees(angle);

    const QPointF point = mapLogicalPointToRect(
        QPointF(kLogicalCanvasCenter + logicalPoint.x(), kLogicalCanvasCenter + logicalPoint.y()),
        playfieldRect
    );
    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const int targetWidth = qMax(1, qRound(starImage->width() * canvasScale * imageScale));
    const int targetHeight = qMax(1, qRound(starImage->height() * canvasScale * imageScale));
    if (!drawSpriteImage(
            painter,
            *starImage,
            point,
            targetWidth,
            targetHeight,
            angle,
            imageOpacity)) {
        return;
    }
}

void PreviewCanvas::drawWifiMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (marker.slideTraceSecond <= marker.second || marker.wifiLanePoints.isEmpty()) {
        return;
    }
    if (playheadSeconds_ < marker.second || playheadSeconds_ > marker.endSecond) {
        return;
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    qreal imageScale = kStarAssetScale;
    qreal imageOpacity = 1.0;
    qreal proportion = 0.0;
    bool waiting = playheadSeconds_ < marker.slideTraceSecond;
    const QImage* starImage = selectSlideMovingStarImage(marker);
    if (starImage->isNull()) {
        return;
    }
    if (waiting) {
        if (!marker.hasHeadStar) {
            return;
        }
        const qreal waitDuration = qMax(kRenderDurationEpsilon, marker.slideTraceSecond - marker.second);
        const qreal waitT = qBound<qreal>(0.0, (playheadSeconds_ - marker.second) / waitDuration, 1.0);
        const qreal startScale = slideStartupStarInitialScale(*starImage);
        imageScale = startScale + (kStarAssetScale - startScale) * waitT;
        imageOpacity = kObjectWaitStateStartOpacity + kObjectWaitStateOpacityDelta * waitT;
    } else if (!marker.slideSegmentDurations.isEmpty()) {
        const qreal duration = qMax(kRenderDurationEpsilon, marker.slideSegmentDurations.constFirst());
        proportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideTraceSecond) / duration, 1.0);
    } else if (marker.endSecond > marker.slideTraceSecond) {
        const qreal duration = qMax(kRenderDurationEpsilon, marker.endSecond - marker.slideTraceSecond);
        proportion = qBound<qreal>(0.0, (playheadSeconds_ - marker.slideTraceSecond) / duration, 1.0);
    }

    const int targetWidth = qMax(1, qRound(starImage->width() * canvasScale * imageScale));
    const int targetHeight = qMax(1, qRound(starImage->height() * canvasScale * imageScale));
    for (int laneIndex = 0; laneIndex < marker.wifiLanePoints.size(); ++laneIndex) {
        const QVector<QPointF>& points = marker.wifiLanePoints[laneIndex];
        const QVector<double>& angles = marker.wifiLaneAngles.value(laneIndex);
        if (points.isEmpty()) {
            continue;
        }
        const QPointF logicalPoint = waiting ? points.constFirst() : interpolatePoint(points, proportion);
        const qreal angle = waiting
            ? (angles.isEmpty() ? 0.0 : angles.constFirst())
            : interpolateAngle(angles, proportion);
        const QPointF point = mapLogicalPointToRect(
            QPointF(kLogicalCanvasCenter + logicalPoint.x(), kLogicalCanvasCenter + logicalPoint.y()),
            playfieldRect
        );
        if (!drawSpriteImage(
                painter,
                *starImage,
                point,
                targetWidth,
                targetHeight,
                mirroredStarAngleDegrees(angle),
                imageOpacity)) {
            continue;
        }
    }
}

void PreviewCanvas::drawTouchMarker(
    QPainter& painter,
    const TimelineNoteMarker& marker,
    const QRectF& playfieldRect,
    int overlapCount)
{
    if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
        return;
    }

    const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
    if (deltaSeconds <= -kTouchDurationSeconds || deltaSeconds >= 0.0) {
        return;
    }

    const QImage& basePointImage =
        (marker.isBreak && !touchPointBreakImage_.isNull())
            ? touchPointBreakImage_
            : ((marker.isEach && !touchPointEachImage_.isNull()) ? touchPointEachImage_ : touchPointImage_);
    const QImage& baseCornerImage =
        (marker.isBreak && !touchCornerBreakImage_.isNull())
            ? touchCornerBreakImage_
            : ((marker.isEach && !touchCornerEachImage_.isNull()) ? touchCornerEachImage_ : touchCornerImage_);
    const QImage* border2Image =
        marker.isBreak ? &touchBorder2BreakImage_ : (marker.isEach ? &touchBorder2EachImage_ : &touchBorder2Image_);
    const QImage* border3Image =
        marker.isBreak ? &touchBorder3BreakImage_ : (marker.isEach ? &touchBorder3EachImage_ : &touchBorder3Image_);
    if (basePointImage.isNull() || baseCornerImage.isNull()) {
        return;
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const QPointF point = mapLogicalPointToRect(marker.touchPoint, playfieldRect);
    const int pointWidth = qMax(1, qRound(basePointImage.width() * kTouchAssetScale * canvasScale));
    const int pointHeight = qMax(1, qRound(basePointImage.height() * kTouchAssetScale * canvasScale));
    const int cornerWidth = qMax(1, qRound(baseCornerImage.width() * kTouchAssetScale * canvasScale));
    const int cornerHeight = qMax(1, qRound(baseCornerImage.height() * kTouchAssetScale * canvasScale));
    const int border2Width =
        (border2Image != nullptr && !border2Image->isNull()) ? qMax(1, qRound(border2Image->width() * kTouchAssetScale * canvasScale)) : 0;
    const int border2Height =
        (border2Image != nullptr && !border2Image->isNull()) ? qMax(1, qRound(border2Image->height() * kTouchAssetScale * canvasScale)) : 0;
    const int border3Width =
        (border3Image != nullptr && !border3Image->isNull()) ? qMax(1, qRound(border3Image->width() * kTouchAssetScale * canvasScale)) : 0;
    const int border3Height =
        (border3Image != nullptr && !border3Image->isNull()) ? qMax(1, qRound(border3Image->height() * kTouchAssetScale * canvasScale)) : 0;
    const qreal alpha = touchPreHitAlpha(deltaSeconds);
    const qreal logicalOffset = touchLogicalOffsetForDelta(deltaSeconds, kTouchStartOffset, kTouchClosedOffset);
    const qreal offset = mapLogicalLengthToRect(logicalOffset, playfieldRect);
    const struct {
        qreal dx;
        qreal dy;
        int angle;
    } layout[] = {
        {0.0, -offset, kTouchUpAngleDegrees},
        {offset, 0.0, kTouchRightAngleDegrees},
        {0.0, offset, kTouchDownAngleDegrees},
        {-offset, 0.0, kTouchLeftAngleDegrees},
    };

    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    for (const auto& pieceLayout : layout) {
        drawSpriteImage(
            painter,
            baseCornerImage,
            QPointF(point.x() + pieceLayout.dx, point.y() + pieceLayout.dy),
            cornerWidth,
            cornerHeight,
            pieceLayout.angle,
            alpha
        );
    }
    if (overlapCount >= kTouchOverlapBorder2Threshold && border2Image != nullptr && !border2Image->isNull()) {
        drawSpriteImage(painter, *border2Image, point, border2Width, border2Height, 0.0, alpha);
    }
    if (overlapCount >= kTouchOverlapBorder3Threshold && border3Image != nullptr && !border3Image->isNull()) {
        drawSpriteImage(painter, *border3Image, point, border3Width, border3Height, 0.0, alpha);
    }
    drawSpriteImage(painter, basePointImage, point, pointWidth, pointHeight, 0.0, alpha);
    if (batchNative) {
        painter.endNativePainting();
        nativePaintingActive_ = false;
    }
}

void PreviewCanvas::drawTouchHoldMarker(QPainter& painter, const TimelineNoteMarker& marker, const QRectF& playfieldRect)
{
    if (qFuzzyIsNull(marker.touchPoint.x()) && qFuzzyIsNull(marker.touchPoint.y())) {
        return;
    }
    if (marker.endSecond <= marker.second) {
        return;
    }
    if (touchHold0Image_.isNull() || touchHold1Image_.isNull() || touchHold2Image_.isNull()
        || touchHold3Image_.isNull() || touchHoldBorderImage_.isNull()) {
        return;
    }

    const qreal deltaSeconds = static_cast<qreal>(playheadSeconds_ - marker.second);
    const qreal holdDuration = qMax<qreal>(0.001, marker.endSecond - marker.second);
    if (deltaSeconds <= -kTouchDurationSeconds || deltaSeconds >= holdDuration) {
        return;
    }

    const QImage& pointBase =
        (marker.isBreak && !touchPointBreakImage_.isNull())
            ? touchPointBreakImage_
            : (!touchPointEachImage_.isNull() ? touchPointEachImage_ : touchPointImage_);
    if (pointBase.isNull()) {
        return;
    }

    const qreal canvasScale = playfieldRect.width() / kLogicalCanvasSize;
    const QPointF point = mapLogicalPointToRect(marker.touchPoint, playfieldRect);
    const int pointWidth = qMax(1, qRound(pointBase.width() * kTouchAssetScale * canvasScale));
    const int pointHeight = qMax(1, qRound(pointBase.height() * kTouchAssetScale * canvasScale));
    const int borderWidth = qMax(1, qRound(touchHoldBorderImage_.width() * kTouchAssetScale * canvasScale));
    const int borderHeight = qMax(1, qRound(touchHoldBorderImage_.height() * kTouchAssetScale * canvasScale));
    qreal alpha = 1.0;
    qreal logicalOffset = kTouchHoldClosedOffset;
    if (deltaSeconds < 0.0) {
        alpha = touchPreHitAlpha(deltaSeconds);
        logicalOffset = touchLogicalOffsetForDelta(deltaSeconds, kTouchHoldStartOffset, kTouchHoldClosedOffset);
    }

    const qreal offset = mapLogicalLengthToRect(logicalOffset, playfieldRect);
    const struct {
        const QImage* image;
        int width;
        int height;
        int angle;
        qreal dx;
        qreal dy;
    } layout[] = {
        {&touchHold0Image_, qMax(1, qRound(touchHold0Image_.width() * kTouchAssetScale * canvasScale)), qMax(1, qRound(touchHold0Image_.height() * kTouchAssetScale * canvasScale)), kTouchHoldUpperRightAngleDegrees, offset, -offset},
        {&touchHold1Image_, qMax(1, qRound(touchHold1Image_.width() * kTouchAssetScale * canvasScale)), qMax(1, qRound(touchHold1Image_.height() * kTouchAssetScale * canvasScale)), kTouchHoldLowerRightAngleDegrees, offset, offset},
        {&touchHold2Image_, qMax(1, qRound(touchHold2Image_.width() * kTouchAssetScale * canvasScale)), qMax(1, qRound(touchHold2Image_.height() * kTouchAssetScale * canvasScale)), kTouchHoldLowerLeftAngleDegrees, -offset, offset},
        {&touchHold3Image_, qMax(1, qRound(touchHold3Image_.width() * kTouchAssetScale * canvasScale)), qMax(1, qRound(touchHold3Image_.height() * kTouchAssetScale * canvasScale)), kTouchHoldUpperLeftAngleDegrees, -offset, -offset},
    };

    const bool batchNative = glRenderer_.isInitialized() && !nativePaintingActive_;
    if (batchNative) {
        nativePaintingActive_ = true;
        painter.beginNativePainting();
    }
    for (int index = 3; index >= 0; --index) {
        const auto& pieceLayout = layout[index];
        drawSpriteImage(
            painter,
            *pieceLayout.image,
            QPointF(point.x() + pieceLayout.dx, point.y() + pieceLayout.dy),
            pieceLayout.width,
            pieceLayout.height,
            pieceLayout.angle,
            alpha
        );
    }

    if (deltaSeconds >= 0.0) {
        const bool resumeNativeAfterBorder = nativePaintingActive_;
        if (resumeNativeAfterBorder) {
            endNativeBatch(painter);
        }
        const QRectF borderRect(
            point.x() - borderWidth / 2.0,
            point.y() - borderHeight / 2.0,
            borderWidth,
            borderHeight
        );
        const qreal progress = qBound<qreal>(0.0, deltaSeconds / holdDuration, 1.0);
        if (progress > 0.0) {
            QPainterPath clipPath;
            clipPath.moveTo(borderRect.center());
            clipPath.arcTo(borderRect, 90.0, -progress * 360.0);
            clipPath.closeSubpath();
            painter.save();
            painter.setClipPath(clipPath);
            painter.drawImage(borderRect, touchHoldBorderImage_);
            painter.restore();
        }
        if (resumeNativeAfterBorder) {
            beginNativeBatch(painter);
        }
    }
    drawSpriteImage(painter, pointBase, point, pointWidth, pointHeight, 0.0, alpha);
    if (batchNative) {
        endNativeBatch(painter);
    }
}
