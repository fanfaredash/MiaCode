#include "preview/quick_scene/PreviewQuickHudLayer.h"

#include "preview/runtime/PreviewRuntime.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewHudState.h"
#include "preview/scene/PreviewProgressStatsCache.h"
#include "preview/scene/PreviewSceneGeometry.h"

#include <QFontMetrics>
#include <QPainter>
#include <QQuickWindow>

namespace {

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

PreviewQuickHudLayer::PreviewQuickHudLayer(QQuickItem* parent)
    : QQuickPaintedItem(parent)
{
    setOpaquePainting(false);
    setAntialiasing(true);
    connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow*) {
        if (runtime_ != nullptr) {
            runtime_->setFrameSize(boundingRect().size().toSize());
        }
        update();
    });
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
        runtimeUpdateConnection_ = QObject::connect(runtime_, &PreviewRuntime::frameStateChanged, this, [this]() {
            update();
        });
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

    const miacode::preview::scene::PreviewFrameState* state = nullptr;
    if (runtime_ != nullptr) {
        state = &runtime_->frameState();
    } else {
        state = frameState_;
    }
    if (state == nullptr
        || !miacode::preview::scene::previewRenderLayerEnabled(
            layerFlags_,
            miacode::preview::scene::HudLayer)) {
        return;
    }
    if (!state->render.showTimestamp && !state->render.showDebugInfo && !state->render.showObjectStatsHud) {
        return;
    }

    const QRectF stageRect = miacode::preview::scene::stageRectForSize(boundingRect().size().toSize());
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
                *painter,
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
        drawDebugLine(
            QStringLiteral("Present: %1 FPS").arg(QString::number(state->fpsDisplay, 'f', 1))
        );
        drawDebugLine(
            QStringLiteral("Tick: %1 FPS  Req: %2 FPS")
                .arg(QString::number(state->tickFpsDisplay, 'f', 1))
                .arg(QString::number(state->updateRequestFpsDisplay, 'f', 1))
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
                *painter,
                QPointF(leftX, baseline0 + metrics.height() * lineIndex++),
                QStringLiteral("Media: external/%1").arg(mediaType),
                fpsFont,
                shadowOffset
            );
            drawHudText(
                *painter,
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
                *painter,
                QPointF(leftX, baseline0 + metrics.height() * lineIndex++),
                QStringLiteral("Age: %1 ms  AvgInt: %2  MaxInt: %3")
                    .arg(frameAgeText)
                    .arg(formatMetric(state->media.externalVideoFrameIntervalAvgMs))
                    .arg(formatMetric(state->media.externalVideoFrameIntervalMaxMs)),
                fpsFont,
                shadowOffset
            );
            drawHudText(
                *painter,
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

    if (state->render.showTimestamp) {
        drawHudText(
            *painter,
            QPointF(stageRect.left() + hudPadding, stageRect.bottom() - hudPadding),
            miacode::preview::scene::formatPreviewHudTimeLabel(state->playheadSeconds),
            timeFont,
            qMax<qreal>(1.0, 2.0 * hudScale)
        );
    }

    if (!state->render.showObjectStatsHud) {
        return;
    }

    const qreal stageAspectRatio = stageRect.height() > 0.0 ? (stageRect.width() / stageRect.height()) : 1.0;
    if (qAbs(stageAspectRatio - 1.0) < 0.02) {
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

    const qreal blockLeft = statsRightLimit - blockWidth;
    const qreal blockTop = stageRect.bottom() - hudPadding - blockHeight;
    if (blockTop < stageRect.top() + hudPadding) {
        return;
    }

    const qreal shadowOffset = qMax<qreal>(1.0, 2.0 * hudScale);
    qreal baseline = blockTop + titleMetrics.ascent();
    drawHudText(*painter, QPointF(blockLeft, baseline), QStringLiteral("FiNALE Rate:"), titleFont, shadowOffset);
    baseline += titleMetrics.descent() + headerGap + rateMetrics.ascent();
    drawHudText(*painter, QPointF(blockLeft, baseline), statLines.at(0), rateFont, shadowOffset);
    baseline += rateMetrics.descent() + sectionGap + titleMetrics.ascent();
    drawHudText(*painter, QPointF(blockLeft, baseline), QStringLiteral("DELUXE Rate:"), titleFont, shadowOffset);
    baseline += titleMetrics.descent() + headerGap + rateMetrics.ascent();
    drawHudText(*painter, QPointF(blockLeft, baseline), rateLine, rateFont, shadowOffset);
    baseline += rateMetrics.descent() + sectionGap + statMetrics.ascent();
    for (int i = 1; i < statLines.size(); ++i) {
        if (i > 1) {
            baseline += statGap + statMetrics.leading();
        }
        drawHudText(*painter, QPointF(blockLeft, baseline), statLines.at(i), statFont, shadowOffset);
        baseline += statMetrics.height();
    }
}
