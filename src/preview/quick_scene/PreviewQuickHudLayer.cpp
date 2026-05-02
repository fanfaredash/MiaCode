#include "preview/quick_scene/PreviewQuickHudLayer.h"

#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewHudState.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/scene/PreviewSceneGeometry.h"

#include <QFontMetrics>
#include <QPainter>
#include <QQuickWindow>
#include <QTimer>

namespace {

bool aspectRatioNear(qreal actual, qreal expected)
{
    return qAbs(actual - expected) < 0.02;
}

void drawHudText(QPainter& painter, const QPointF& baseline, const QString& text, const QFont& font, qreal shadowOffset)
{
    painter.save();
    painter.setFont(font);
    painter.setPen(QColor(0, 0, 0, 190));
    painter.drawText(baseline + QPointF(shadowOffset, shadowOffset), text);
    painter.setPen(QColor(QStringLiteral("#FFFFFF")));
    painter.drawText(baseline, text);
    painter.restore();
}

}  // namespace

// Min interval between HUD repaints. The HUD shows FPS/max-ms/stutter
// counts which are statistical aggregates over a ~1s rolling window —
// updating their visual presentation at ~10Hz is indistinguishable to the
// eye from updating at 60Hz, but cuts the QSG sync cost by 6× because the
// QQuickPaintedItem only re-rasterises its full-area backing texture on
// actual update() calls. Empirically observed to drop the QSG sync phase
// from ~7.8ms/frame to ~2-3ms on the test chart.
constexpr qint64 kHudUpdateIntervalMs = 100;

PreviewQuickHudLayer::PreviewQuickHudLayer(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(false);
    setAntialiasing(true);
    hudUpdateThrottleTimer_.start();
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow*) {
        if (runtime_ != nullptr) {
            runtime_->setFrameSize(boundingRect().size().toSize());
        }
        update();
    });
}

void PreviewQuickHudLayer::requestThrottledUpdate()
{
    if (!hudUpdateThrottleTimer_.isValid()) {
        hudUpdateThrottleTimer_.start();
    }
    const qint64 nowMs = hudUpdateThrottleTimer_.elapsed();
    if (lastHudUpdateMs_ < 0 || (nowMs - lastHudUpdateMs_) >= kHudUpdateIntervalMs) {
        lastHudUpdateMs_ = nowMs;
        hudUpdatePending_ = false;
        update();
        return;
    }
    // Inside the throttle window — schedule a single deferred update so the
    // last frameStateChanged in the window still produces a visible refresh
    // (otherwise a burst of changes ending at the start of the window would
    // never repaint until the next frameStateChanged after the window
    // closes, leaving stale text on screen).
    if (!hudUpdatePending_) {
        hudUpdatePending_ = true;
        const qint64 delayMs = kHudUpdateIntervalMs - (nowMs - lastHudUpdateMs_);
        QTimer::singleShot(qMax<qint64>(1, delayMs), this, [this]() {
            if (!hudUpdatePending_) {
                return;
            }
            lastHudUpdateMs_ = hudUpdateThrottleTimer_.elapsed();
            hudUpdatePending_ = false;
            update();
        });
    }
}

void PreviewQuickHudLayer::setRuntime(PreviewRuntime* runtime)
{
    if (runtime_ == runtime) {
        return;
    }
    if (runtimeUpdateConnection_) {
        QObject::disconnect(runtimeUpdateConnection_);
    }
    runtime_ = runtime;
    if (runtime_ != nullptr) {
        frameState_ = nullptr;
        if (!miacode::debug_options::previewDCompQuiesceQsgEnabled()) {
            runtimeUpdateConnection_ = QObject::connect(runtime_, &PreviewRuntime::frameStateChanged, this, [this]() {
                requestThrottledUpdate();
            });
        }
        runtime_->setFrameSize(boundingRect().size().toSize());
    }
    emit runtimeChanged();
    update();
}

QObject* PreviewQuickHudLayer::runtimeObject() const
{
    return runtime_;
}

void PreviewQuickHudLayer::setRuntimeObject(QObject* runtimeObject)
{
    setRuntime(qobject_cast<PreviewRuntime*>(runtimeObject));
}

void PreviewQuickHudLayer::setDCompFallbackActive(bool active)
{
    if (dcompFallbackActive_ == active) {
        return;
    }
    dcompFallbackActive_ = active;
    update();
    emit dcompFallbackActiveChanged();
}

void PreviewQuickHudLayer::setFrameState(const miacode::preview::scene::PreviewFrameState* frameState)
{
    frameState_ = frameState;
    if (frameState_ != nullptr) {
        runtime_ = nullptr;
    }
    update();
}

void PreviewQuickHudLayer::setLayerFlags(miacode::preview::scene::PreviewRenderLayerFlags layerFlags)
{
    if (layerFlags_ == layerFlags) {
        return;
    }
    layerFlags_ = layerFlags;
    update();
}

void PreviewQuickHudLayer::paint(QPainter* painter)
{
    if (painter == nullptr) {
        return;
    }
    // Phase 4b — when DComp-exclusive mode is on, the HUD is rendered
    // by PreviewDCompSurface via the same paintPreviewHudOverlay
    // helper into an offscreen QImage and uploaded as a DComp sprite.
    // Skipping QSG paint here avoids the redundant QQuickPaintedItem
    // texture upload that was the last QSG cost the user flagged as
    // perf-relevant.
    //
    // Issue #4 fix — `dcompFallbackActive_` overrides the gate: in the
    // fullscreen QuickShellPreviewSurface instance the DComp popup
    // can't render (see PreviewQuickSceneRoot's parallel comment), so
    // QML sets fallback=true to let this QQuickPaintedItem paint as
    // usual.
    if (!dcompFallbackActive_
        && miacode::debug_options::previewDCompExclusiveEnabled()) {
        return;
    }
    const miacode::preview::scene::PreviewFrameState* state = nullptr;
    if (runtime_ != nullptr) {
        state = &runtime_->frameState();
    } else {
        state = frameState_;
    }
    if (state == nullptr) {
        return;
    }
    miacode::preview::hud::paintPreviewHudOverlay(
        *painter, *state, boundingRect().size().toSize(), layerFlags_);
}

namespace miacode::preview::hud {

void paintPreviewHudOverlay(
    QPainter& painter,
    const miacode::preview::scene::PreviewFrameState& stateRef,
    const QSize& canvasSize,
    miacode::preview::scene::PreviewRenderLayerFlags layerFlags)
{
    const auto* state = &stateRef;
    if (!miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags, miacode::preview::scene::HudLayer)) {
        return;
    }
    if (!state->render.showTimestamp && !state->render.showDebugInfo && !state->render.showObjectStatsHud) {
        return;
    }

    const QRectF stageRect = miacode::preview::scene::stageRectForSize(canvasSize);
    constexpr qreal kHudReferenceShortSide = 1024.0;
    constexpr qreal kHudReferencePadding = 18.0;
    constexpr int kHudReferenceDebugFontPointSize = 13;
    constexpr int kHudReferenceStatsFontPointSize = 22;
    constexpr qreal kHudTimestampToStatsFontScale = 1.2;

    const qreal shortSide = qMin(stageRect.width(), stageRect.height());
    const qreal hudScale = qMax<qreal>(0.1, shortSide / kHudReferenceShortSide);
    const qreal hudPadding = qMax<qreal>(2.0, kHudReferencePadding * hudScale);
    const int timeFontPointSize = qMax(
        1,
        qRound(static_cast<qreal>(kHudReferenceStatsFontPointSize) * kHudTimestampToStatsFontScale * hudScale)
    );
    const int debugFontPointSize = qMax(1, qRound(static_cast<qreal>(kHudReferenceDebugFontPointSize) * hudScale));
    QFont timeFont = miacode::preview::scene::previewHudTimestampFont(timeFontPointSize, QFont::DemiBold);

    if (state->render.showDebugInfo) {
        QFont fpsFont = miacode::preview::scene::previewHudMonoFont(debugFontPointSize, QFont::Medium);
        const QFontMetrics metrics(fpsFont);
        const qreal leftX = stageRect.left() + hudPadding;
        const qreal baseline0 = stageRect.top() + hudPadding + metrics.ascent();
        const qreal shadowOffset = qMax<qreal>(1.0, 2.0 * hudScale);
        const auto formatMetric = [](double value) {
            return value > 0.0 ? QString::number(value, 'f', 1) : QStringLiteral("na");
        };
        int lineIndex = 0;
        const auto drawDebugLine = [&](const QString& text) {
            drawHudText(
                painter,
                QPointF(leftX, baseline0 + metrics.height() * lineIndex),
                text,
                fpsFont,
                shadowOffset
            );
            ++lineIndex;
        };

        drawDebugLine(
            QStringLiteral("Renderer: %1  Fallback: %2")
                .arg(state->usedGpuRendererThisFrame ? QStringLiteral("GPU") : QStringLiteral("CPU"))
                .arg(state->cpuFallbackCount)
        );
        // The trailing max=Nms and stut=N metrics surface what an FPS average
        // hides: max is the worst single inter-event interval in the rolling
        // window, stut is the count of intervals exceeding 1.5× the target
        // (i.e. user-noticeable hitches). A clean 60fps line should show
        // max≈17ms stut=0; numbers above that are the actual lag the user
        // perceives even when the FPS figure looks healthy.
        drawDebugLine(
            QStringLiteral("Present: %1 FPS  max=%2ms  stut=%3")
                .arg(QString::number(state->fpsDisplay, 'f', 1))
                .arg(QString::number(state->presentMaxMsDisplay, 'f', 0))
                .arg(state->presentStutterCountDisplay)
        );
        drawDebugLine(
            QStringLiteral("Tick: %1 FPS  max=%2ms  stut=%3")
                .arg(QString::number(state->tickFpsDisplay, 'f', 1))
                .arg(QString::number(state->tickMaxMsDisplay, 'f', 0))
                .arg(state->tickStutterCountDisplay)
        );
        drawDebugLine(
            QStringLiteral("Req: %1 FPS  max=%2ms  stut=%3")
                .arg(QString::number(state->updateRequestFpsDisplay, 'f', 1))
                .arg(QString::number(state->updateRequestMaxMsDisplay, 'f', 0))
                .arg(state->updateRequestStutterCountDisplay)
        );
        drawDebugLine(
            QStringLiteral("Count T/U/P: %1 / %2 / %3")
                .arg(state->tickCount)
                .arg(state->updateRequestCount)
                .arg(state->presentedFrameCount)
        );
        drawDebugLine(
            QStringLiteral("Pacing: %1 %2  Disp: %3")
                .arg(state->framePacingUsesDisplayRefresh ? QStringLiteral("display") : QStringLiteral("fixed"))
                .arg(formatMetric(state->framePacingTargetFps))
                .arg(formatMetric(state->displayRefreshRate))
        );
        if (state->media.presentationMode == miacode::preview::scene::PreviewStageMediaPresentationMode::ExternalQuickMediaItem) {
            QString mediaType = QStringLiteral("none");
            switch (state->media.externalMediaType) {
            case miacode::preview::scene::PreviewExternalStageMediaType::Image:
                mediaType = QStringLiteral("image");
                break;
            case miacode::preview::scene::PreviewExternalStageMediaType::Video:
                mediaType = QStringLiteral("video");
                break;
            case miacode::preview::scene::PreviewExternalStageMediaType::None:
            default:
                break;
            }
            drawHudText(
                painter,
                QPointF(leftX, baseline0 + metrics.height() * lineIndex++),
                QStringLiteral("Media: external/%1").arg(mediaType),
                fpsFont,
                shadowOffset
            );
            drawHudText(
                painter,
                QPointF(leftX, baseline0 + metrics.height() * lineIndex++),
                QStringLiteral("Video: %1  Delta: %2 s")
                    .arg(
                        QStringLiteral("%1 @ %2 FPS")
                            .arg(state->media.externalVideoPlaybackActive ? QStringLiteral("active") : QStringLiteral("idle"))
                            .arg(formatMetric(state->media.externalVideoFrameRate))
                    )
                    .arg(QString::number(state->media.externalClockDeltaSeconds, 'f', 3)),
                fpsFont,
                shadowOffset
            );
            const QString frameAgeText = state->media.externalVideoFrameAgeMs >= 0
                ? QString::number(state->media.externalVideoFrameAgeMs)
                : QStringLiteral("na");
            drawHudText(
                painter,
                QPointF(leftX, baseline0 + metrics.height() * lineIndex++),
                QStringLiteral("Age: %1 ms  AvgInt: %2  MaxInt: %3")
                    .arg(frameAgeText)
                    .arg(formatMetric(state->media.externalVideoFrameIntervalAvgMs))
                    .arg(formatMetric(state->media.externalVideoFrameIntervalMaxMs)),
                fpsFont,
                shadowOffset
            );
            drawHudText(
                painter,
                QPointF(leftX, baseline0 + metrics.height() * lineIndex++),
                QStringLiteral("Stall: %1  Count: %2  MediaT: %3 s")
                    .arg(state->media.externalVideoFrameStalled ? QStringLiteral("yes") : QStringLiteral("no"))
                    .arg(state->media.externalVideoFrameStallCount)
                    .arg(QString::number(state->media.externalPlaybackSecond, 'f', 3)),
                fpsFont,
                shadowOffset
            );
        }
    }

    const qreal stageAspectRatio = stageRect.height() > 0.0 ? (stageRect.width() / stageRect.height()) : 1.0;

    if (state->render.showTimestamp) {
        const QString timeLabel = miacode::preview::scene::formatPreviewHudTimeLabel(state->playheadSeconds);
        const QFontMetrics timeMetrics(timeFont);
        const bool insetTimestampForAspect =
            aspectRatioNear(stageAspectRatio, 16.0 / 9.0) || aspectRatioNear(stageAspectRatio, 4.0 / 3.0);
        const qreal positiveTimeExtraInset =
            timeLabel.startsWith(QLatin1Char('-')) || !insetTimestampForAspect
                ? 0.0
                : static_cast<qreal>(timeMetrics.horizontalAdvance(QStringLiteral(" ")));
        const qreal timestampBottomExtraInset =
            insetTimestampForAspect ? (static_cast<qreal>(timeMetrics.lineSpacing()) * 0.5) : 0.0;
        drawHudText(
            painter,
            QPointF(
                stageRect.left() + hudPadding + positiveTimeExtraInset,
                stageRect.bottom() - hudPadding - timestampBottomExtraInset
            ),
            timeLabel,
            timeFont,
            qMax<qreal>(1.0, 2.0 * hudScale)
        );
    }

    if (!state->render.showObjectStatsHud) {
        return;
    }

    if (aspectRatioNear(stageAspectRatio, 1.0) || aspectRatioNear(stageAspectRatio, 4.0 / 3.0)) {
        return;
    }

    const QRectF playfieldRect = miacode::preview::scene::playfieldRectForStage(stageRect, state->render.layoutSquareScale);
    const qreal statsLeftLimit = playfieldRect.right() + hudPadding;
    const qreal statsRightLimit = stageRect.right() - hudPadding;
    const qreal availableStatsWidth = statsRightLimit - statsLeftLimit;
    if (availableStatsWidth < 40.0) {
        return;
    }

    const miacode::preview::scene::PreviewHudStats stats =
        state->progressStatsCache != nullptr
        ? state->progressStatsCache->hudStatsAt(state->playheadSeconds)
        : miacode::preview::scene::PreviewHudStats();

    int baseFontPointSize = qMax(1, qRound(static_cast<qreal>(kHudReferenceStatsFontPointSize) * hudScale));
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

    while (baseFontPointSize >= 5) {
        titleFont = miacode::preview::scene::previewHudTimestampFont(baseFontPointSize, QFont::DemiBold);
        rateFont = miacode::preview::scene::previewHudTimestampFont(baseFontPointSize + 1, QFont::DemiBold);
        statFont = miacode::preview::scene::previewHudTimestampFont(baseFontPointSize, QFont::DemiBold);
        titleMetrics = QFontMetrics(titleFont);
        rateMetrics = QFontMetrics(rateFont);
        statMetrics = QFontMetrics(statFont);

        const QString finaleLine = QStringLiteral("%1 %")
            .arg(QString::number(stats.finaleRate, 'f', 2).rightJustified(6, QChar('0')));
        rateLine = QStringLiteral("%1 %")
            .arg(QString::number(stats.deluxeRate, 'f', 4).rightJustified(8, QChar('0')));
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

    const bool isSixteenByNine = aspectRatioNear(stageAspectRatio, 16.0 / 9.0);
    const qreal extraRightInset =
        isSixteenByNine ? static_cast<qreal>(statMetrics.horizontalAdvance(QStringLiteral("   "))) : 0.0;
    const qreal extraBottomInset =
        isSixteenByNine ? static_cast<qreal>(statMetrics.lineSpacing()) : 0.0;
    const qreal blockLeft = statsRightLimit - extraRightInset - blockWidth;
    const qreal blockTop = stageRect.bottom() - hudPadding - extraBottomInset - blockHeight;
    if (blockTop < stageRect.top() + hudPadding) {
        return;
    }

    const qreal shadowOffset = qMax<qreal>(1.0, 2.0 * hudScale);
    qreal baseline = blockTop + titleMetrics.ascent();
    drawHudText(painter, QPointF(blockLeft, baseline), QStringLiteral("FiNALE Rate:"), titleFont, shadowOffset);
    baseline += titleMetrics.descent() + headerGap + rateMetrics.ascent();
    drawHudText(painter, QPointF(blockLeft, baseline), statLines.at(0), rateFont, shadowOffset);
    baseline += rateMetrics.descent() + sectionGap + titleMetrics.ascent();
    drawHudText(painter, QPointF(blockLeft, baseline), QStringLiteral("DELUXE Rate:"), titleFont, shadowOffset);
    baseline += titleMetrics.descent() + headerGap + rateMetrics.ascent();
    drawHudText(painter, QPointF(blockLeft, baseline), rateLine, rateFont, shadowOffset);
    baseline += rateMetrics.descent() + sectionGap + statMetrics.ascent();
    for (int i = 1; i < statLines.size(); ++i) {
        if (i > 1) {
            baseline += statGap + statMetrics.leading();
        }
        drawHudText(painter, QPointF(blockLeft, baseline), statLines.at(i), statFont, shadowOffset);
        baseline += statMetrics.height();
    }
}

}  // namespace miacode::preview::hud
