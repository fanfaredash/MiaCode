#include "VideoExportController.h"

#include "BassExportAudioBackend.h"
#include "LegacyExportAudioBackend.h"
#include "RawVideoPipeTransport.h"
#include "VideoExportAudioRenderPlan.h"
#include "VideoExportQuickRenderBackend.h"
#include "VideoExportRuntimePolicy.h"
#include "common/AssetPaths.h"
#include "common/ChartAssetPaths.h"
#include "common/IntroConfig.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"
#include "common/DebugOptions.h"
#include "common/LayoutRingConfig.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewGameplayConfig.h"
#include "core/scene/PreviewSceneGeometry.h"
#include "common/PreviewSfxTimeline.h"
#include "preview/runtime/PreviewSceneAssetLoader.h"
#include "tools/muri/MuriAnalyzer.h"

#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QImage>
#include <QImageReader>
#include <QLinearGradient>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QProcess>
#include <QRect>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QSurfaceFormat>
#include <QTemporaryDir>
#include <QTextStream>
#include <QThread>
#include <QUuid>
#include <QtMath>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <optional>

#ifdef Q_OS_WIN
#include <windows.h>
#endif
#include "VideoExportControllerInternal.h"

// VideoExportFrameRender.cpp — lead-in pause overlay, ready-frame payload assembly, GPU/offscreen frame rendering + PBO drain, circular dim mask, static background staging, and packed-RGBA frame preparation.
//
// Definitions extracted verbatim from the original VideoExportController.cpp
// during the god-file split. All helpers live in the shared
// miacode::video_export::detail namespace (declared in
// VideoExportControllerInternal.h).
using namespace miacode::video_export::detail;

namespace miacode::video_export::detail {

// Paint a semi-transparent two-bar pause glyph centred over the export
// frame. Called once per frame for the lead-in / pre-range window so the
// viewer immediately sees that the playfield is held in a stationary state
// (chart frozen at segmentStart, no playback yet). The glyph disappears
// the moment the lead-in ends and the chart starts to advance.
//
// SHIPPED STYLE: DarkRing (flat) — a solid dark disc with an even white
// hairline ring and square-cut white bars. Chosen 2026-08-16 after rendering
// all eight candidates below over both a bright PV and a dark playfield: the
// flat disc keeps the previous version's silhouette, and the ring is what
// stops a plain dark disc from disappearing into a dark background.
//
// The other seven candidates are kept as real code, not comments, so the
// choice can be revisited by editing ONE line (kLeadInPauseGlyphStyle) and
// re-rendering. They compile with the switch below, so they cannot rot.
//
// Every dimension derives from the shared PauseGlyphGeometry, which scales
// from the frame's short side — one knob resizes the whole badge.
//
// `Format_RGBA8888` (straight alpha) is what the export pipeline feeds
// FFmpeg — QPainter on this format composes correctly through Qt's
// internal premultiplied path; the final straight-alpha output is what
// FFmpeg's `overlay=…:alpha=straight` filter expects.
namespace {

enum class LeadInPauseGlyphStyle {
    GlassBadge,        // A — halo + gradient scrim + sheen + bar shadow (skeuomorphic)
    SolidDark,         // B — flat opaque dark disc, no ring
    SolidLight,        // C — flat white disc with dark bars
    OutlineRing,       // D — thick white ring over a lightly dimmed interior
    BarsOnly,          // E — no disc; deeper full-frame dim behind bare bars
    KnockoutBars,      // F — white disc with the bars knocked out of it
    DarkRing,          // G — flat dark disc + white hairline ring  << shipped
    TranslucentLight,  // H — 22% white disc + white ring, all-white palette
};

inline constexpr LeadInPauseGlyphStyle kLeadInPauseGlyphStyle = LeadInPauseGlyphStyle::DarkRing;

struct PauseGlyphGeometry {
    QPointF center;
    qreal diameter = 0.0;
    qreal radius = 0.0;
    QRectF badge;
    QRectF leftBar;
    QRectF rightBar;
};

PauseGlyphGeometry pauseGlyphGeometry(const QSize& frameSize)
{
    PauseGlyphGeometry geometry;
    const int shortSide = qMin(frameSize.width(), frameSize.height());
    geometry.center = QPointF(frameSize.width() / 2.0, frameSize.height() / 2.0);
    geometry.diameter = qMax<qreal>(28.0, shortSide * 0.17);
    geometry.radius = geometry.diameter / 2.0;
    geometry.badge = QRectF(
        geometry.center.x() - geometry.radius,
        geometry.center.y() - geometry.radius,
        geometry.diameter,
        geometry.diameter
    );
    // 0.40 x 0.115 of the diameter reads as the standard pause mark, with a
    // comfortable margin left inside the disc.
    const qreal barHeight = geometry.diameter * 0.40;
    const qreal barWidth = geometry.diameter * 0.115;
    const qreal gap = barWidth * 0.85;
    const qreal totalWidth = barWidth * 2.0 + gap;
    const qreal x0 = geometry.center.x() - totalWidth / 2.0;
    const qreal y0 = geometry.center.y() - barHeight / 2.0;
    geometry.leftBar = QRectF(x0, y0, barWidth, barHeight);
    geometry.rightBar = QRectF(x0 + barWidth + gap, y0, barWidth, barHeight);
    return geometry;
}

// Full-frame dim. SourceOver onto an opaque chart frame pulls its perceived
// brightness down; onto a transparent base it leaves a dark wash that FFmpeg's
// overlay filter then composites over the chart background. Either way the
// playfield reads paused.
void drawPauseFrameDim(QPainter& painter, const QRect& frameRect, int alpha)
{
    painter.fillRect(frameRect, QColor(0, 0, 0, alpha));
}

void drawPauseBars(
    QPainter& painter,
    const PauseGlyphGeometry& geometry,
    const QColor& color,
    qreal cornerFactor
)
{
    const qreal radius = geometry.leftBar.width() * cornerFactor;
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(geometry.leftBar, radius, radius);
    painter.drawRoundedRect(geometry.rightBar, radius, radius);
}

void strokePauseRing(
    QPainter& painter,
    const QRectF& badge,
    const QColor& color,
    qreal width
)
{
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, width));
    painter.drawEllipse(badge.adjusted(width / 2.0, width / 2.0, -width / 2.0, -width / 2.0));
    painter.setPen(Qt::NoPen);
}

// A — frosted glass: radial halo, vertical scrim, top sheen, dropped bars.
void drawPauseGlyphGlassBadge(QPainter& painter, const QRect& frameRect, const PauseGlyphGeometry& g)
{
    drawPauseFrameDim(painter, frameRect, 70);

    const qreal haloRadius = g.radius * 1.45;
    QRadialGradient halo(g.center, haloRadius);
    halo.setColorAt(0.0, QColor(0, 0, 0, 120));
    halo.setColorAt(g.radius / haloRadius, QColor(0, 0, 0, 96));
    halo.setColorAt(1.0, QColor(0, 0, 0, 0));
    painter.setBrush(halo);
    painter.drawEllipse(g.center, haloRadius, haloRadius);

    QLinearGradient scrim(g.badge.topLeft(), g.badge.bottomLeft());
    scrim.setColorAt(0.0, QColor(12, 14, 20, 132));
    scrim.setColorAt(1.0, QColor(4, 5, 8, 168));
    painter.setBrush(scrim);
    painter.drawEllipse(g.badge);

    QLinearGradient sheen(g.badge.topLeft(), g.badge.bottomLeft());
    sheen.setColorAt(0.0, QColor(255, 255, 255, 54));
    sheen.setColorAt(0.5, QColor(255, 255, 255, 10));
    sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter.setBrush(sheen);
    painter.drawEllipse(g.badge);

    strokePauseRing(painter, g.badge, QColor(255, 255, 255, 110), qMax<qreal>(1.0, g.diameter * 0.018));

    const qreal barRadius = g.leftBar.width() * 0.42;
    const qreal shadowOffset = qMax<qreal>(1.0, g.diameter * 0.012);
    painter.setBrush(QColor(0, 0, 0, 90));
    painter.drawRoundedRect(g.leftBar.translated(0.0, shadowOffset), barRadius, barRadius);
    painter.drawRoundedRect(g.rightBar.translated(0.0, shadowOffset), barRadius, barRadius);
    drawPauseBars(painter, g, QColor(255, 255, 255, 242), 0.42);
}

// B — flat opaque dark disc. Nearly vanishes on a dark playfield; kept for
// reference, use DarkRing instead.
void drawPauseGlyphSolidDark(QPainter& painter, const QRect& frameRect, const PauseGlyphGeometry& g)
{
    drawPauseFrameDim(painter, frameRect, 70);
    painter.setBrush(QColor(17, 19, 26, 214));
    painter.drawEllipse(g.badge);
    drawPauseBars(painter, g, QColor(255, 255, 255, 255), 0.0);
}

// C — flat white disc with dark bars. Highest contrast, but the disc becomes
// the brightest object in frame over a bright PV.
void drawPauseGlyphSolidLight(QPainter& painter, const QRect& frameRect, const PauseGlyphGeometry& g)
{
    drawPauseFrameDim(painter, frameRect, 70);
    painter.setBrush(QColor(255, 255, 255, 235));
    painter.drawEllipse(g.badge);
    drawPauseBars(painter, g, QColor(20, 22, 30, 255), 0.0);
}

// D — thick white ring, interior only lightly dimmed.
void drawPauseGlyphOutlineRing(QPainter& painter, const QRect& frameRect, const PauseGlyphGeometry& g)
{
    drawPauseFrameDim(painter, frameRect, 70);
    painter.setBrush(QColor(0, 0, 0, 96));
    painter.drawEllipse(g.badge);
    strokePauseRing(painter, g.badge, QColor(255, 255, 255, 235), qMax<qreal>(2.0, g.diameter * 0.05));
    drawPauseBars(painter, g, QColor(255, 255, 255, 255), 0.0);
}

// E — bars alone over a deeper dim. The most restrained, and the weakest read.
void drawPauseGlyphBarsOnly(QPainter& painter, const QRect& frameRect, const PauseGlyphGeometry& g)
{
    drawPauseFrameDim(painter, frameRect, 96);
    drawPauseBars(painter, g, QColor(255, 255, 255, 240), 0.5);
}

// F — white disc with the bars knocked back out of it, so the bars show the
// dimmed chart through them.
void drawPauseGlyphKnockoutBars(QPainter& painter, const QRect& frameRect, const PauseGlyphGeometry& g)
{
    drawPauseFrameDim(painter, frameRect, 70);
    painter.setBrush(QColor(255, 255, 255, 216));
    painter.drawEllipse(g.badge);
    painter.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    painter.setBrush(QColor(0, 0, 0, 255));
    painter.drawRect(g.leftBar);
    painter.drawRect(g.rightBar);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
}

// G — SHIPPED. Flat dark disc plus an even white hairline ring: the flat read
// of B, with the ring keeping the silhouette on a dark playfield.
void drawPauseGlyphDarkRing(QPainter& painter, const QRect& frameRect, const PauseGlyphGeometry& g)
{
    drawPauseFrameDim(painter, frameRect, 70);
    painter.setBrush(QColor(17, 19, 26, 214));
    painter.drawEllipse(g.badge);
    strokePauseRing(painter, g.badge, QColor(255, 255, 255, 200), qMax<qreal>(1.5, g.diameter * 0.028));
    drawPauseBars(painter, g, QColor(255, 255, 255, 255), 0.0);
}

// H — all-white palette: 22% white disc, white ring, white bars.
void drawPauseGlyphTranslucentLight(QPainter& painter, const QRect& frameRect, const PauseGlyphGeometry& g)
{
    drawPauseFrameDim(painter, frameRect, 70);
    painter.setBrush(QColor(255, 255, 255, 56));
    painter.drawEllipse(g.badge);
    strokePauseRing(painter, g.badge, QColor(255, 255, 255, 170), qMax<qreal>(1.5, g.diameter * 0.022));
    drawPauseBars(painter, g, QColor(255, 255, 255, 255), 0.0);
}

}  // namespace

void drawLeadInPauseOverlay(QImage* frame)
{
    if (frame == nullptr || frame->isNull()) {
        return;
    }
    const QSize frameSize = frame->size();
    if (qMin(frameSize.width(), frameSize.height()) <= 0) {
        return;
    }

    QPainter painter(frame);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    const PauseGlyphGeometry geometry = pauseGlyphGeometry(frameSize);
    switch (kLeadInPauseGlyphStyle) {
    case LeadInPauseGlyphStyle::GlassBadge:
        drawPauseGlyphGlassBadge(painter, frame->rect(), geometry);
        break;
    case LeadInPauseGlyphStyle::SolidDark:
        drawPauseGlyphSolidDark(painter, frame->rect(), geometry);
        break;
    case LeadInPauseGlyphStyle::SolidLight:
        drawPauseGlyphSolidLight(painter, frame->rect(), geometry);
        break;
    case LeadInPauseGlyphStyle::OutlineRing:
        drawPauseGlyphOutlineRing(painter, frame->rect(), geometry);
        break;
    case LeadInPauseGlyphStyle::BarsOnly:
        drawPauseGlyphBarsOnly(painter, frame->rect(), geometry);
        break;
    case LeadInPauseGlyphStyle::KnockoutBars:
        drawPauseGlyphKnockoutBars(painter, frame->rect(), geometry);
        break;
    case LeadInPauseGlyphStyle::DarkRing:
        drawPauseGlyphDarkRing(painter, frame->rect(), geometry);
        break;
    case LeadInPauseGlyphStyle::TranslucentLight:
        drawPauseGlyphTranslucentLight(painter, frame->rect(), geometry);
        break;
    }
}

ReadyFramePayload buildReadyFramePayload(
    VideoExportQuickRenderBackend* exportBackend,
    int frameIndex,
    double exportSecond,
    QVector<ObjectTraceItem>&& traceItems,
    QImage frame,
    qint64 renderNs,
    bool usedOffscreenPath
)
{
    ReadyFramePayload readyFrame;
    readyFrame.frameIndex = frameIndex;
    readyFrame.exportSecond = exportSecond;
    readyFrame.traceItems = std::move(traceItems);
    readyFrame.frame = std::move(frame);
    readyFrame.renderNs = renderNs;
    readyFrame.usedOffscreenPath = usedOffscreenPath;
    if (exportBackend != nullptr) {
        readyFrame.offscreenDrawNs = exportBackend->offscreenDrawNsLastFrameForDebug();
        readyFrame.offscreenReadbackNs = exportBackend->offscreenReadbackNsLastFrameForDebug();
        readyFrame.stateUpdateNs = exportBackend->stateUpdateNsLastFrameForDebug();
        readyFrame.polishNs = exportBackend->polishNsLastFrameForDebug();
        readyFrame.syncNs = exportBackend->syncNsLastFrameForDebug();
        readyFrame.renderSubmitNs = exportBackend->renderSubmitNsLastFrameForDebug();
        readyFrame.fallbackCount = exportBackend->cpuFallbackCountLastFrameForDebug();
        readyFrame.usedGpuRenderer = exportBackend->usedGpuRendererLastFrameForDebug();
    }
    return readyFrame;
}

ExportFrameRenderStatus renderExportFrameWithConfiguredBackend(
    VideoExportQuickRenderBackend* exportBackend,
    bool* useOffscreenGpu,
    bool* useOffscreenPboReadback,
    std::deque<PendingPboFrame>* pendingPboFrames,
    const QSize& frameSize,
    int frameIndex,
    double exportSecond,
    bool showTimestamp,
    bool showObjectStatsHud,
    QVector<ObjectTraceItem>&& traceItems,
    ReadyFramePayload* readyFrame,
    QString* fallbackDetail,
    double hudPlayheadSecondsOverride
)
{
    if (exportBackend == nullptr
        || useOffscreenGpu == nullptr
        || useOffscreenPboReadback == nullptr
        || pendingPboFrames == nullptr
        || readyFrame == nullptr) {
        return ExportFrameRenderStatus::Failed;
    }

    *readyFrame = ReadyFramePayload{};
    QElapsedTimer frameTimer;
    frameTimer.start();
    bool usedOffscreenPath = false;
    QImage frame;

    if (*useOffscreenGpu && *useOffscreenPboReadback) {
        QImage completedFrame;
        bool completedFrameReady = false;
        QString pboStepError;
        const bool pboStepOk = exportBackend->renderOverlayFrameOffscreenPboStep(
            frameSize,
            exportSecond,
            showTimestamp,
            showObjectStatsHud,
            &completedFrame,
            &completedFrameReady,
            false,
            &pboStepError,
            hudPlayheadSecondsOverride
        );
        const qint64 renderNs = frameTimer.nsecsElapsed();
        if (pboStepOk) {
            usedOffscreenPath = true;
            // Two pending slots in steady state since the convert worker
            // adds an extra frame of pipeline depth on top of the PBO
            // ping-pong: oldest entry = frame whose worker job just
            // finished and is being returned to us in completedFrame;
            // newer entry = frame currently in a PBO awaiting either
            // worker submission or readback finalisation.
            const bool producedReadyFrame = completedFrameReady && !pendingPboFrames->empty();
            if (producedReadyFrame) {
                PendingPboFrame oldest = std::move(pendingPboFrames->front());
                pendingPboFrames->pop_front();
                *readyFrame = buildReadyFramePayload(
                    exportBackend,
                    oldest.frameIndex,
                    oldest.exportSecond,
                    std::move(oldest.traceItems),
                    std::move(completedFrame),
                    renderNs,
                    true
                );
            }
            PendingPboFrame newPending;
            newPending.valid = true;
            newPending.frameIndex = frameIndex;
            newPending.exportSecond = exportSecond;
            newPending.traceItems = std::move(traceItems);
            pendingPboFrames->push_back(std::move(newPending));
            return producedReadyFrame ? ExportFrameRenderStatus::Ready : ExportFrameRenderStatus::Deferred;
        }

        appendRenderBackendFallbackDetail(
            fallbackDetail,
            QStringLiteral("frame=%1 reason=offscreen_pbo_failed error=%2")
                .arg(frameIndex)
                .arg(pboStepError)
        );
        exportBackend->resetOffscreenPboReadback();
        *useOffscreenPboReadback = false;
    }

    frame = *useOffscreenGpu
        ? exportBackend->renderOverlayFrameOffscreen(
              frameSize,
              exportSecond,
              showTimestamp,
              showObjectStatsHud,
              hudPlayheadSecondsOverride)
        : exportBackend->renderOverlayFrame(
              frameSize,
              exportSecond,
              showTimestamp,
              showObjectStatsHud,
              hudPlayheadSecondsOverride);
    if (frame.isNull()) {
        appendRenderBackendFallbackDetail(
            fallbackDetail,
            QStringLiteral("frame=%1 reason=quick_render_failed").arg(frameIndex));
        return ExportFrameRenderStatus::Failed;
    }

    *readyFrame = buildReadyFramePayload(
        exportBackend,
        frameIndex,
        exportSecond,
        std::move(traceItems),
        std::move(frame),
        frameTimer.nsecsElapsed(),
        usedOffscreenPath || *useOffscreenGpu
    );
    return ExportFrameRenderStatus::Ready;
}

bool drainPendingExportFrame(
    VideoExportQuickRenderBackend* exportBackend,
    std::deque<PendingPboFrame>* pendingPboFrames,
    const QSize& frameSize,
    bool showTimestamp,
    bool showObjectStatsHud,
    ReadyFramePayload* readyFrame,
    QString* errorMessage
)
{
    if (exportBackend == nullptr || pendingPboFrames == nullptr || readyFrame == nullptr
        || pendingPboFrames->empty()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("no pending PBO frame to drain");
        }
        return false;
    }

    *readyFrame = ReadyFramePayload{};
    QElapsedTimer frameTimer;
    frameTimer.start();
    QImage drainedFrame;
    bool drainedFrameReady = false;
    QString drainError;
    const bool drainOk = exportBackend->renderOverlayFrameOffscreenPboStep(
        frameSize,
        pendingPboFrames->front().exportSecond,
        showTimestamp,
        showObjectStatsHud,
        &drainedFrame,
        &drainedFrameReady,
        true,
        &drainError
    );
    if (!drainOk || !drainedFrameReady) {
        if (errorMessage != nullptr) {
            *errorMessage = drainError.isEmpty() ? QStringLiteral("failed to drain PBO readback") : drainError;
        }
        return false;
    }

    PendingPboFrame oldest = std::move(pendingPboFrames->front());
    pendingPboFrames->pop_front();
    *readyFrame = buildReadyFramePayload(
        exportBackend,
        oldest.frameIndex,
        oldest.exportSecond,
        std::move(oldest.traceItems),
        std::move(drainedFrame),
        frameTimer.nsecsElapsed(),
        true
    );
    return true;
}

QImage buildCircularDimMaskImage(
    int frameWidth,
    int frameHeight,
    double outerDimAlpha,
    double innerDimAlpha,
    double layoutRingDiameterRatio,
    double layoutSquareScale,
    bool smoothBrightness
)
{
    const int width = qMax(1, frameWidth);
    const int height = qMax(1, frameHeight);
    QImage mask(width, height, QImage::Format_RGBA8888);
    mask.fill(Qt::transparent);

    const int outerAlpha = qBound(0, qRound(outerDimAlpha * 255.0), 255);
    const int innerAlpha = qBound(0, qRound(innerDimAlpha * 255.0), 255);
    if (outerAlpha == 0 && innerAlpha == 0) {
        return mask;
    }

    const double layoutSide = miacode::preview_video::layoutSquareSideForStage(
        QRectF(0.0, 0.0, static_cast<double>(width), static_cast<double>(height)),
        layoutSquareScale
    );
    const double centerX = (static_cast<double>(width) - 1.0) * 0.5;
    const double centerY = (static_cast<double>(height) - 1.0) * 0.5;

    for (int y = 0; y < height; ++y) {
        uchar* row = mask.scanLine(y);
        const double dy = static_cast<double>(y) - centerY;
        for (int x = 0; x < width; ++x) {
            const double dx = static_cast<double>(x) - centerX;
            const double radius = std::sqrt(dx * dx + dy * dy);
            const int alpha = qBound(
                0,
                qRound(
                    miacode::preview_video::dimAlphaForRadius(
                        radius,
                        outerDimAlpha,
                        innerDimAlpha,
                        layoutSide,
                        layoutRingDiameterRatio,
                        smoothBrightness
                    ) * 255.0
                ),
                255
            );
            const int offset = x * 4;
            row[offset + 0] = 0;
            row[offset + 1] = 0;
            row[offset + 2] = 0;
            row[offset + 3] = static_cast<uchar>(alpha);
        }
    }
    return mask;
}

QImage buildCircularMediaMaskImage(
    int frameWidth,
    int frameHeight,
    double layoutSquareScale
)
{
    const int width = qMax(1, frameWidth);
    const int height = qMax(1, frameHeight);
    QImage mask(width, height, QImage::Format_Grayscale8);
    mask.fill(0);

    const double layoutSide = miacode::preview_video::layoutSquareSideForStage(
        QRectF(0.0, 0.0, static_cast<double>(width), static_cast<double>(height)),
        layoutSquareScale
    );
    const double radius = qMax(1.0, layoutSide * 0.5);
    const double centerX = (static_cast<double>(width) - 1.0) * 0.5;
    const double centerY = (static_cast<double>(height) - 1.0) * 0.5;

    for (int y = 0; y < height; ++y) {
        uchar* row = mask.scanLine(y);
        const double dy = static_cast<double>(y) - centerY;
        for (int x = 0; x < width; ++x) {
            const double dx = static_cast<double>(x) - centerX;
            const double distance = std::sqrt(dx * dx + dy * dy);
            row[x] = distance <= radius ? 255 : 0;
        }
    }
    return mask;
}

QRectF staticMediaTargetRect(
    const QSize& mediaSize,
    const QSize& outputSize,
    PreviewBackgroundScaleMode scaleMode)
{
    return miacode::preview::scene::mediaTargetRect(
        mediaSize,
        QRectF(0.0, 0.0, outputSize.width(), outputSize.height()),
        scaleMode
    );
}

bool stageStaticBackgroundImageForExport(
    const QString& sourcePath,
    const QSize& outputSize,
    PreviewBackgroundScaleMode scaleMode,
    double layoutSquareScale,
    const QString& stagedPath,
    QString* detail)
{
    if (detail != nullptr) {
        detail->clear();
    }

    if (sourcePath.isEmpty() || outputSize.isEmpty() || stagedPath.isEmpty()) {
        if (detail != nullptr) {
            *detail = QStringLiteral("invalid_input");
        }
        return false;
    }

    QImageReader reader(sourcePath);
    reader.setAutoTransform(true);
    const QImage sourceImage = reader.read();
    if (sourceImage.isNull()) {
        if (detail != nullptr) {
            *detail = QStringLiteral("read_failed error=%1").arg(reader.errorString());
        }
        return false;
    }

    QImage stagedImage(outputSize, QImage::Format_RGBA8888);
    stagedImage.fill(Qt::black);

    QPainter painter(&stagedImage);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    if (scaleMode == PreviewBackgroundScaleMode::InnerCircleFitOuterFill) {
        const QRectF stageRect(0.0, 0.0, outputSize.width(), outputSize.height());
        const QRectF innerCircleRect = miacode::preview_video::centeredLayoutRectForStage(
            stageRect,
            layoutSquareScale
        );
        painter.drawImage(
            staticMediaTargetRect(sourceImage.size(), outputSize, PreviewBackgroundScaleMode::FillCrop),
            sourceImage,
            miacode::preview::scene::mediaSourceRect(sourceImage.size(), PreviewBackgroundScaleMode::FillCrop)
        );
        QPainterPath innerCircleClip;
        innerCircleClip.addEllipse(innerCircleRect);
        painter.setClipPath(innerCircleClip);
        painter.drawImage(
            miacode::preview::scene::mediaTargetRect(
                sourceImage.size(),
                innerCircleRect,
                PreviewBackgroundScaleMode::FitContain
            ),
            sourceImage,
            miacode::preview::scene::mediaSourceRect(sourceImage.size(), PreviewBackgroundScaleMode::FitContain)
        );
    } else {
        painter.drawImage(
            staticMediaTargetRect(sourceImage.size(), outputSize, scaleMode),
            sourceImage,
            miacode::preview::scene::mediaSourceRect(sourceImage.size(), scaleMode)
        );
    }
    painter.end();

    if (!stagedImage.save(stagedPath)) {
        if (detail != nullptr) {
            *detail = QStringLiteral("save_failed path=%1").arg(stagedPath);
        }
        return false;
    }

    if (detail != nullptr) {
        *detail = QStringLiteral("source=%1x%2 staged=%3x%4 mode=%5 path=%6")
            .arg(sourceImage.width())
            .arg(sourceImage.height())
            .arg(stagedImage.width())
            .arg(stagedImage.height())
            .arg(scaleMode == PreviewBackgroundScaleMode::FitContain
                    ? QStringLiteral("fit")
                    : (scaleMode == PreviewBackgroundScaleMode::SquareFitContain
                           ? QStringLiteral("square_fit")
                           : (scaleMode == PreviewBackgroundScaleMode::InnerCircleFitOuterFill
                                  ? QStringLiteral("inner_circle_fit_outer_fill")
                                  : QStringLiteral("fill"))))
            .arg(stagedPath);
    }
    return true;
}

bool preparePackedRgbaFrame(
    const QImage& frame,
    bool preservePremultiplied,
    QImage* convertedFrame,
    QByteArray* packedScratch,
    const char** data,
    qint64* size
)
{
    if (convertedFrame == nullptr || packedScratch == nullptr || data == nullptr || size == nullptr) {
        return false;
    }
    *convertedFrame = QImage();
    packedScratch->clear();
    *data = nullptr;
    *size = 0;

    const QImage::Format expectedFormat = preservePremultiplied
        ? QImage::Format_RGBA8888_Premultiplied
        : QImage::Format_RGBA8888;
    const QImage* rgba = &frame;
    if (rgba->format() != expectedFormat) {
        *convertedFrame = frame.convertToFormat(expectedFormat);
        if (!frame.isNull() && convertedFrame->isNull()) {
            return false;
        }
        rgba = convertedFrame;
    }
    if (!frame.isNull() && rgba->isNull()) {
        return false;
    }

    const int width = rgba->width();
    const int height = rgba->height();
    if (width <= 0 || height <= 0) {
        return true;
    }

    const qint64 packedStride = static_cast<qint64>(width) * 4;
    const qint64 packedSize = packedStride * height;
    if (rgba->bytesPerLine() == packedStride) {
        if (packedSize > 0 && rgba->constBits() == nullptr) {
            return false;
        }
        *data = reinterpret_cast<const char*>(rgba->constBits());
        *size = packedSize;
        return true;
    }

    packedScratch->resize(static_cast<qsizetype>(packedSize));
    if (packedScratch->size() != packedSize || (packedSize > 0 && packedScratch->data() == nullptr)) {
        packedScratch->clear();
        return false;
    }
    for (int y = 0; y < height; ++y) {
        std::memcpy(
            packedScratch->data() + y * packedStride,
            rgba->constScanLine(y),
            static_cast<size_t>(packedStride)
        );
    }
    *data = packedScratch->constData();
    *size = packedScratch->size();
    return true;
}

}  // namespace miacode::video_export::detail
