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
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QProcess>
#include <QProgressDialog>
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
// Layered so the pause read clearly against both dark and bright chart
// backgrounds:
//   1) A faint full-frame black wash (alpha 70/255 ≈ 27%) that mutes the
//      whole playfield so it visibly "feels" paused.
//   2) A rounded translucent backdrop panel under the bars for contrast
//      on bright frames.
//   3) Two opaque white bars with a thin black outline so the bars
//      stay legible regardless of what shows through.
//
// `Format_RGBA8888` (straight alpha) is what the export pipeline feeds
// FFmpeg — QPainter on this format composes correctly through Qt's
// internal premultiplied path; the final straight-alpha output is what
// FFmpeg's `overlay=…:alpha=straight` filter expects.
void drawLeadInPauseOverlay(QImage* frame)
{
    if (frame == nullptr || frame->isNull()) {
        return;
    }
    const QSize sz = frame->size();
    const int shortSide = qMin(sz.width(), sz.height());
    if (shortSide <= 0) {
        return;
    }

    QPainter painter(frame);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    // Layer 1 — full-frame dim. SourceOver onto an opaque chart frame
    // pulls its perceived brightness down ~27%; onto a transparent base
    // it leaves a dark wash that FFmpeg's overlay filter then composites
    // over the chart background. Either way the playfield reads paused.
    painter.fillRect(frame->rect(), QColor(0, 0, 0, 70));

    // 2/3 of the original sizing — the previous version read as overly
    // dominant during the lead-in; scaling barHeight here cascades to
    // every other dimension because they are all derived from it.
    const int barHeight = qMax(8, qRound(shortSide * 0.22 * 2.0 / 3.0));
    const int barWidth = qMax(3, qRound(barHeight * 0.32));
    const int gap = qMax(2, qRound(barWidth * 0.65));
    const int totalWidth = barWidth * 2 + gap;
    const qreal x0 = (sz.width() - totalWidth) / 2.0;
    const qreal y0 = (sz.height() - barHeight) / 2.0;
    const qreal radius = qMax<qreal>(3.0, barWidth * 0.25);

    // Layer 2 — translucent backdrop panel behind the bars.
    const qreal panelPadX = barWidth * 0.7;
    const qreal panelPadY = barHeight * 0.18;
    const QRectF panelRect(
        x0 - panelPadX,
        y0 - panelPadY,
        totalWidth + panelPadX * 2.0,
        barHeight + panelPadY * 2.0
    );
    painter.setBrush(QColor(0, 0, 0, 110));
    painter.drawRoundedRect(panelRect, radius * 1.6, radius * 1.6);

    // Layer 3 — bars themselves: opaque white with a thin dark outline.
    painter.setPen(QPen(QColor(0, 0, 0, 160), 1.5));
    painter.setBrush(QColor(255, 255, 255, 230));
    painter.drawRoundedRect(QRectF(x0, y0, barWidth, barHeight), radius, radius);
    painter.drawRoundedRect(QRectF(x0 + barWidth + gap, y0, barWidth, barHeight), radius, radius);
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

    const double layoutSide = miacode::preview_video::layoutSquareSideForCanvasHeight(
        static_cast<double>(height),
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
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(
        staticMediaTargetRect(sourceImage.size(), outputSize, scaleMode),
        sourceImage,
        miacode::preview::scene::mediaSourceRect(sourceImage.size(), scaleMode)
    );
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
                           : QStringLiteral("fill")))
            .arg(stagedPath);
    }
    return true;
}

bool preparePackedRgbaFrame(
    const QImage& frame,
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

    const QImage* rgba = &frame;
    if (rgba->format() != QImage::Format_RGBA8888) {
        *convertedFrame = frame.convertToFormat(QImage::Format_RGBA8888);
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
