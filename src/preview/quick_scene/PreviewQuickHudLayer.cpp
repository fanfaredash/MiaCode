#include "preview/quick_scene/PreviewQuickHudLayer.h"

#include "common/DebugOptions.h"
#include "preview/runtime/PreviewRuntime.h"
#include "core/scene/PreviewFrameState.h"
#include "core/scene/PreviewHudState.h"
#include "core/scene/PreviewProgressStatsCache.h"
#include "core/scene/PreviewSceneGeometry.h"

#include <QFontMetrics>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QTimer>

#include <cmath>

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

QStringList wrapTextByPixelWidth(const QString& text, qreal maxWidth, const QFontMetrics& fm)
{
    QStringList lines;
    if (text.isEmpty()) {
        return lines;
    }
    if (maxWidth <= 0.0) {
        lines.append(text);
        return lines;
    }
    QString current;
    qreal currentWidth = 0.0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        const qreal chWidth = static_cast<qreal>(fm.horizontalAdvance(ch));
        if (!current.isEmpty() && currentWidth + chWidth > maxWidth) {
            const int spaceIdx = current.lastIndexOf(QLatin1Char(' '));
            if (spaceIdx > 0 && spaceIdx >= current.size() / 2) {
                lines.append(current.left(spaceIdx));
                current = current.mid(spaceIdx + 1);
                currentWidth = static_cast<qreal>(fm.horizontalAdvance(current));
            } else {
                lines.append(current);
                current.clear();
                currentWidth = 0.0;
            }
        }
        current.append(ch);
        currentWidth += chWidth;
    }
    if (!current.isEmpty()) {
        lines.append(current);
    }
    return lines;
}

void drawOutlinedHudText(
    QPainter& painter,
    const QPointF& baseline,
    const QString& text,
    const QFont& font,
    qreal outlineWidth,
    const QColor& fillColor)
{
    QPainterPath path;
    path.addText(baseline, font, text);
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    // Soft drop shadow, approximating MajdataPlay's TMP underlay (black, offset, softened).
    painter.translate(outlineWidth * 0.45, outlineWidth * 0.55);
    painter.setPen(QPen(QColor(0, 0, 0, 110), outlineWidth * 1.3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.translate(-outlineWidth * 0.45, -outlineWidth * 0.55);
    // Thin neutral-grey rim (matches TMP _OutlineColor rgb(185,185,185)), not a bright white halo.
    painter.setPen(QPen(QColor(185, 185, 185, 235), outlineWidth, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(path);
    painter.setPen(Qt::NoPen);
    painter.setBrush(fillColor);
    painter.drawPath(path);
    painter.restore();
}

QString formatCenterAchievement(double value)
{
    // Match MajdataPlay: truncate (floor) to 4 decimals, never round up.
    const double truncated = std::floor(value * 10000.0) / 10000.0;
    return QStringLiteral("%1%").arg(QString::number(truncated, 'f', 4));
}

QString centerDisplayTitle(miacode::preview_gameplay::CenterDisplayMode mode)
{
    switch (mode) {
    case miacode::preview_gameplay::CenterDisplayMode::Combo:
        return QStringLiteral("COMBO");
    case miacode::preview_gameplay::CenterDisplayMode::AchievementFinalePlus:
        return QStringLiteral("ACHIEVEMENT FiNALE");
    case miacode::preview_gameplay::CenterDisplayMode::AchievementDxPlus:
    case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100:
    case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101:
        return QStringLiteral("ACHIEVEMENT");
    case miacode::preview_gameplay::CenterDisplayMode::DxScorePlus:
    case miacode::preview_gameplay::CenterDisplayMode::DxScoreMinus:
        return QStringLiteral("DX SCORE");
    case miacode::preview_gameplay::CenterDisplayMode::Off:
    default:
        return QString();
    }
}

QString centerDisplayValue(
    miacode::preview_gameplay::CenterDisplayMode mode,
    const miacode::preview::scene::PreviewHudStats& stats)
{
    switch (mode) {
    case miacode::preview_gameplay::CenterDisplayMode::Combo:
        // MajdataPlay hides the whole displayer while combo is 0.
        return stats.combo == 0 ? QString() : QString::number(stats.combo);
    case miacode::preview_gameplay::CenterDisplayMode::AchievementDxPlus:
        return formatCenterAchievement(stats.deluxeRate);
    case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus100:
        if (stats.deluxeBreakTotal > 0)
            return formatCenterAchievement(100.0 + static_cast<double>(stats.deluxeBreakCurrent) / stats.deluxeBreakTotal);
        return formatCenterAchievement(100.0);
    case miacode::preview_gameplay::CenterDisplayMode::AchievementDxMinus101:
        return formatCenterAchievement(101.0);
    case miacode::preview_gameplay::CenterDisplayMode::DxScorePlus:
        return QString::number(stats.dxScore);
    case miacode::preview_gameplay::CenterDisplayMode::DxScoreMinus:
        return QString::number(stats.dxScoreMax);
    case miacode::preview_gameplay::CenterDisplayMode::AchievementFinalePlus:
        return formatCenterAchievement(stats.finaleRate);
    case miacode::preview_gameplay::CenterDisplayMode::Off:
    default:
        return QString();
    }
}

// MajdataPlay's ObjectCounter color palette (achievement tiers via UpdateAchievementColor).
QColor achievementTierColor(double rate)
{
    if (rate >= 100.0) {
        return QColor(200, 159, 109);  // gold
    }
    if (rate >= 97.0) {
        return QColor(159, 159, 159);  // silver
    }
    if (rate >= 80.0) {
        return QColor(127, 48, 32);  // bronze
    }
    return QColor(63, 127, 176);  // dud / blue
}

QColor centerDisplayValueColor(
    miacode::preview_gameplay::CenterDisplayMode mode,
    const miacode::preview::scene::PreviewHudStats& stats)
{
    using Mode = miacode::preview_gameplay::CenterDisplayMode;
    switch (mode) {
    case Mode::Combo:
        return QColor(186, 62, 118);
    case Mode::AchievementDxPlus:
        return achievementTierColor(stats.deluxeRate);
    case Mode::AchievementDxMinus100:
        return achievementTierColor(stats.deluxeBreakTotal > 0
            ? 100.0 + static_cast<double>(stats.deluxeBreakCurrent) / stats.deluxeBreakTotal
            : 100.0);
    case Mode::AchievementDxMinus101:
        return achievementTierColor(101.0);
    case Mode::DxScorePlus:
    case Mode::DxScoreMinus:
        return QColor(63, 176, 63);
    case Mode::AchievementFinalePlus:
        return achievementTierColor(stats.finaleRate);
    case Mode::Off:
    default:
        return QColor(186, 62, 118);
    }
}

QColor centerDisplayHeaderColor(miacode::preview_gameplay::CenterDisplayMode mode)
{
    using Mode = miacode::preview_gameplay::CenterDisplayMode;
    switch (mode) {
    case Mode::DxScorePlus:
    case Mode::DxScoreMinus:
        return QColor(63, 176, 63);
    case Mode::AchievementDxPlus:
    case Mode::AchievementDxMinus100:
    case Mode::AchievementDxMinus101:
    case Mode::AchievementFinalePlus:
        return QColor(200, 159, 109);
    case Mode::Combo:
    case Mode::Off:
    default:
        return QColor(186, 62, 118);
    }
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
    if (!state->render.showTimestamp
        && !state->render.showDebugInfo
        && !state->render.showObjectStatsHud
        && !state->render.showChartInfoHud) {
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

    // The HUD honours `hudPlayheadSecondsOverride` when finite so callers
    // can show a timestamp decoupled from the scene time. The full-range
    // video export sets it during the lead-in; the scene itself now plays
    // the real lead-in chart time (it is no longer clamped at chart 0), so
    // this currently matches the scene playhead.
    const double hudPlayheadSeconds = qIsFinite(state->hudPlayheadSecondsOverride)
        ? state->hudPlayheadSecondsOverride
        : state->playheadSeconds;

    if (state->render.showTimestamp) {
        const QString timeLabel = miacode::preview::scene::formatPreviewHudTimeLabel(hudPlayheadSeconds);
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

    // Optional top-left chart info HUD. Shares the 1:1 / 4:3 skip-rule
    // with the object stats HUD because both rely on the wide letterbox
    // margin to sit outside the playfield. Font + left inset match the
    // bottom-left timestamp HUD (same point size, DemiBold, +hudPadding
    // from stage's left edge) so the two text overlays read as a pair.
    // Lines 1-3 (title / artist / difficulty) render as single, un-wrapped
    // lines so a long song or artist name doesn't collapse into a column;
    // Qt's painter clips at the stage edge if they overflow. Line 4
    // (designer) auto-wraps inside the gap between the stage's left edge
    // and the playfield so it doesn't intrude on the chart sprites.
    // Total stack is capped at half the stage height ? if the wrapped
    // designer lines would overflow that budget, the final visible row
    // is replaced with "..." to signal the cut.
    if (state->render.showChartInfoHud
        && !aspectRatioNear(stageAspectRatio, 1.0)
        && !aspectRatioNear(stageAspectRatio, 4.0 / 3.0)) {
        const QRectF chartInfoPlayfield =
            miacode::preview::scene::playfieldRectForStage(stageRect, state->render.layoutSquareScale);
        const qreal chartInfoLeft = stageRect.left() + hudPadding;
        const qreal chartInfoDesignerRightLimit = chartInfoPlayfield.left() - hudPadding;
        const qreal chartInfoDesignerMaxWidth = chartInfoDesignerRightLimit - chartInfoLeft;
        const qreal chartInfoMaxHeight = stageRect.height() / 2.0 - hudPadding;
        if (chartInfoMaxHeight > 0.0) {
            // Mirror the timestamp HUD: same point size, DemiBold,
            // previewHudTimestampFont() font family.
            const QFontMetrics chartInfoMetrics(timeFont);
            const qreal lineHeight = static_cast<qreal>(chartInfoMetrics.lineSpacing());
            const int maxLines = qMax(0, static_cast<int>(chartInfoMaxHeight / qMax<qreal>(1.0, lineHeight)));
            if (maxLines > 0) {
                QStringList physicalLines;
                // Lines 1-3: pushed verbatim, no width clamp, no wrap.
                if (!state->chartTitle.isEmpty()) {
                    physicalLines.append(state->chartTitle);
                }
                if (!state->chartArtist.isEmpty()) {
                    physicalLines.append(state->chartArtist);
                }
                if (!state->chartDifficultyLabel.isEmpty()) {
                    physicalLines.append(state->chartDifficultyLabel);
                }
                // Line 4: designer wraps to stay inside the letterbox
                // margin. Skip wrapping if the margin is too narrow to
                // matter (would just emit one char per line) ? let the
                // painter clip instead, same fallback as lines 1-3.
                if (!state->chartDesigner.isEmpty()) {
                    if (chartInfoDesignerMaxWidth >= 40.0) {
                        physicalLines.append(wrapTextByPixelWidth(
                            state->chartDesigner, chartInfoDesignerMaxWidth, chartInfoMetrics));
                    } else {
                        physicalLines.append(state->chartDesigner);
                    }
                }
                bool truncated = false;
                if (physicalLines.size() > maxLines) {
                    truncated = true;
                    physicalLines = physicalLines.mid(0, maxLines);
                }
                if (truncated && !physicalLines.isEmpty()) {
                    physicalLines[physicalLines.size() - 1] = QStringLiteral("...");
                }
                if (!physicalLines.isEmpty()) {
                    const qreal chartInfoShadow = qMax<qreal>(1.0, 2.0 * hudScale);
                    qreal chartInfoBaseline = stageRect.top() + hudPadding + chartInfoMetrics.ascent();
                    for (const QString& line : physicalLines) {
                        drawHudText(
                            painter,
                            QPointF(chartInfoLeft, chartInfoBaseline),
                            line,
                            timeFont,
                            chartInfoShadow
                        );
                        chartInfoBaseline += lineHeight;
                    }
                }
            }
        }
    }

    const miacode::preview::scene::PreviewHudStats stats =
        state->progressStatsCache != nullptr
        ? state->progressStatsCache->hudStatsAt(hudPlayheadSeconds)
        : miacode::preview::scene::PreviewHudStats();

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
            QStringLiteral("TAP: %1/%2").arg(stats.tapPlayed).arg(stats.tapTotal),
            QStringLiteral("HLD: %1/%2").arg(stats.holdPlayed).arg(stats.holdTotal),
            QStringLiteral("SLD: %1/%2").arg(stats.slidePlayed).arg(stats.slideTotal),
            QStringLiteral("TOH: %1/%2").arg(stats.touchPlayed).arg(stats.touchTotal),
            QStringLiteral("BRK: %1/%2").arg(stats.breakPlayed).arg(stats.breakTotal),
            QStringLiteral("ALL: %1/%2").arg(stats.combo).arg(stats.totalNotes),
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

void paintCenterDisplay(
    QPainter& painter,
    const miacode::preview::scene::PreviewFrameState& state,
    const QSize& canvasSize)
{
    if (state.render.centerDisplayMode == miacode::preview_gameplay::CenterDisplayMode::Off) {
        return;
    }
    const QRectF stageRect = miacode::preview::scene::stageRectForSize(canvasSize);
    // Size/position the center display in the playfield's 1080 logical space, the same
    // design space MajdataPlay's CenterInfoDisplayer lives in, so it tracks the notes ring
    // (and the "Stage Display Scale" / layoutSquareScale) instead of the stage HUD reference.
    const QRectF playRect =
        miacode::preview::scene::playfieldRectForStage(stageRect, state.render.layoutSquareScale);
    const qreal playShort = qMax<qreal>(1.0, qMin(playRect.width(), playRect.height()));
    const double hudPlayheadSeconds = qIsFinite(state.hudPlayheadSecondsOverride)
        ? state.hudPlayheadSecondsOverride
        : state.playheadSeconds;
    const miacode::preview::scene::PreviewHudStats stats =
        state.progressStatsCache != nullptr
        ? state.progressStatsCache->hudStatsAt(hudPlayheadSeconds)
        : miacode::preview::scene::PreviewHudStats();
    const QString title = centerDisplayTitle(state.render.centerDisplayMode);
    const QString value = centerDisplayValue(state.render.centerDisplayMode, stats);
    if (title.isEmpty() || value.isEmpty()) {
        return;
    }
    // MajdataPlay reference (1080 design space): value TMP fontSize 65, header 44,
    // header centered below the value (which is centered on the playfield centre).
    // MajdataPlay puts the header 137.3px below; we pull it up to 2/3 that gap (~91.5px).
    const int valuePixelSize = qMax(1, qRound(65.0 / 1080.0 * playShort));
    const int titlePixelSize = qMax(1, qRound(44.0 / 1080.0 * playShort));
    const qreal headerCenterOffsetY = (137.3 * 2.0 / 3.0) / 1080.0 * playShort;

    QFont titleFont = miacode::preview::scene::previewHudTimestampFont(titlePixelSize, QFont::Black);
    titleFont.setPixelSize(titlePixelSize);
    QFont valueFont = miacode::preview::scene::previewHudTimestampFont(valuePixelSize, QFont::Black);
    valueFont.setPixelSize(valuePixelSize);
    const QFontMetricsF titleMetrics(titleFont);
    const QFontMetricsF valueMetrics(valueFont);

    const QPointF center = playRect.center();
    // Value glyph visually centered on the playfield centre.
    const qreal valueBaselineY = center.y() + (valueMetrics.ascent() - valueMetrics.descent()) / 2.0;
    const QPointF valueBaseline(
        center.x() - valueMetrics.horizontalAdvance(value) / 2.0,
        valueBaselineY);
    // Header label centered horizontally, sitting below the value.
    const qreal headerCenterY = center.y() + headerCenterOffsetY;
    const qreal titleBaselineY = headerCenterY + (titleMetrics.ascent() - titleMetrics.descent()) / 2.0;
    const QPointF titleBaseline(
        center.x() - titleMetrics.horizontalAdvance(title) / 2.0,
        titleBaselineY);

    const QColor valueColor = centerDisplayValueColor(state.render.centerDisplayMode, stats);
    const QColor titleColor = centerDisplayHeaderColor(state.render.centerDisplayMode);
    drawOutlinedHudText(painter, titleBaseline, title, titleFont,
        qMax<qreal>(1.5, titlePixelSize * 0.05), titleColor);
    drawOutlinedHudText(painter, valueBaseline, value, valueFont,
        qMax<qreal>(2.0, valuePixelSize * 0.05), valueColor);
}

}  // namespace miacode::preview::hud
