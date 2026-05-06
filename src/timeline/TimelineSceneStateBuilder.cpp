#include "timeline/TimelineSceneStateBuilder.h"

#include <QFontMetrics>
#include <QtMath>

#include <algorithm>

#include "common/PreviewSkinConfig.h"
#include "common/TimelineThemeConfig.h"
#include "timeline/TimelineNoteAssets.h"

namespace {

using miacode::timeline::TimelineSceneBuildRequest;
using miacode::timeline::TimelineSceneGlyph;
using miacode::timeline::TimelineSceneGlyphShape;
using miacode::timeline::TimelineSceneLayoutMetrics;
using miacode::timeline::TimelineSceneLine;
using miacode::timeline::TimelineSceneRect;
using miacode::timeline::TimelineSceneState;
using miacode::timeline::TimelineSceneSprite;
using miacode::timeline::TimelineSceneTextLabel;
using miacode::timeline::TimelineSceneTriangle;
using miacode::timeline::TimelineThemeColors;

constexpr int kPlayableLaneCount = 8;
constexpr int kLaneCount = kPlayableLaneCount + 1;
constexpr int kHeaderHeight = 28;
constexpr int kLaneHeight = 20;
constexpr int kTimelineLeftMargin = 40;
constexpr int kTimelineTopMargin = 6;
constexpr int kTimelineRightPadding = 24;
constexpr int kTimelineHeaderLineLabelMinSpacingPx = 22;
constexpr int kTimelineHeaderMultiDigitLabelSideGapPx = 2;
constexpr qreal kTimelineHeaderSingleDigitFontScale = 0.9;
constexpr qreal kTimelineHeaderMultiDigitBaseFontScale = 0.8;
constexpr qreal kTimelineTopMarkerTipOffsetPx = 1.0;
constexpr qreal kTimelinePlaybackEntryMarkerHalfWidthPx = 6.0;
constexpr qreal kTimelinePlaybackEntryMarkerHeightPx = 8.0;
constexpr qreal kTimelineTopMarkerHalfWidthPerHeight =
    kTimelinePlaybackEntryMarkerHalfWidthPx / kTimelinePlaybackEntryMarkerHeightPx;
constexpr qreal kTimelineHeaderAnchorMarkerLegacyWidthFactor = 0.85;
constexpr qreal kTimelineHeaderAnchorMarkerLegacyHeightFactor = 0.7;
constexpr int kTimelineHeaderAnchorMarkerTextGapPx = 0;
constexpr int kNoteSize = 14;
constexpr int kTimelineMaxRenderedSubdivisionBeats = 32;
constexpr double kTimelineDisplayLeadInSeconds = 0.5;
constexpr double kTimelineHeaderLineAnchorToleranceSeconds = 1e-6;
constexpr qreal kTimelineBeatLineWidth = 1.2;
constexpr qreal kTimelineHoldThicknessRelativeToTap =
    static_cast<qreal>(miacode::preview_skin::kHoldWidthRelativeToTap);
constexpr qreal kTimelineTextHorizontalPadding = 2.0;
constexpr qreal kTimelineTextVerticalPadding = 1.0;

struct HeaderLineLabel {
    int lineNumber = 1;
    double second = 0.0;
    int screenX = 0;
};

QString laneLabelForIndex(int laneIndex)
{
    if (laneIndex >= 0 && laneIndex < kPlayableLaneCount) {
        return QString::number(laneIndex + 1);
    }
    return QString();
}

const miacode::timeline::TimelineNoteAssetSet& sceneNoteAssets()
{
    static const miacode::timeline::TimelineNoteAssetSet assets = miacode::timeline::loadTimelineNoteAssets();
    return assets;
}

QFont timelineLaneLabelFont()
{
    QFont laneLabelFont(QStringLiteral("Consolas"));
    laneLabelFont.setStyleHint(QFont::Monospace);
    laneLabelFont.setPointSize(10);
    laneLabelFont.setWeight(QFont::DemiBold);
    return laneLabelFont;
}

QSizeF timelineTextLogicalSize(const QFont& font, const QString& text)
{
    const QFontMetricsF metrics(font);
    return QSizeF(
        qMax<qreal>(1.0, qCeil(metrics.horizontalAdvance(text) + (kTimelineTextHorizontalPadding * 2.0))),
        qMax<qreal>(1.0, qCeil(metrics.height() + (kTimelineTextVerticalPadding * 2.0))));
}

double maxNavigableSecond(const TimelineSceneBuildRequest& request)
{
    double maxSecond = qMax(
        0.0,
        qMax(
            request.snapshot.maximumSecond,
            qMax(
                request.snapshot.durationSeconds,
                qMax(
                    request.playbackEntrySeconds,
                    qMax(request.playheadSeconds, qMax(request.cursorSeconds, 0.0))))));
    if (request.playheadUpperLimitSeconds > 0.0) {
        maxSecond = qMax(maxSecond, request.playheadUpperLimitSeconds);
    }
    if (request.waveformData && request.waveformData->durationSeconds > 0.0) {
        maxSecond = qMax(maxSecond, request.waveformData->durationSeconds);
    }
    return maxSecond;
}

double pixelsPerSecondForZoom(double zoomScale)
{
    return 120.0 * qMax(0.25, zoomScale);
}

int rawSecondToX(double second, double displayStartSeconds, double pixelsPerSecond)
{
    return kTimelineLeftMargin + qRound((second - displayStartSeconds) * pixelsPerSecond);
}

qreal rawSecondToXExact(double second, double displayStartSeconds, double pixelsPerSecond)
{
    return static_cast<qreal>(kTimelineLeftMargin) + static_cast<qreal>((second - displayStartSeconds) * pixelsPerSecond);
}

int rawContentWidth(double displayStartSeconds, double displayEndSeconds, double pixelsPerSecond)
{
    const double timelineSeconds = qMax(0.0, displayEndSeconds - displayStartSeconds);
    return kTimelineLeftMargin + static_cast<int>(timelineSeconds * pixelsPerSecond) + kTimelineRightPadding;
}

int secondToX(const TimelineSceneLayoutMetrics& metrics, double second)
{
    return rawSecondToX(second, metrics.displayStartSeconds, metrics.pixelsPerSecond)
        + metrics.leadingCenteringPadding;
}

qreal secondToXExact(const TimelineSceneLayoutMetrics& metrics, double second)
{
    return rawSecondToXExact(second, metrics.displayStartSeconds, metrics.pixelsPerSecond)
        + static_cast<qreal>(metrics.leadingCenteringPadding);
}

int secondToX(const TimelineSceneState& state, double second)
{
    return rawSecondToX(second, state.displayStartSeconds, state.pixelsPerSecond) + state.leadingCenteringPadding;
}

qreal secondToXExact(const TimelineSceneState& state, double second)
{
    return rawSecondToXExact(second, state.displayStartSeconds, state.pixelsPerSecond)
        + static_cast<qreal>(state.leadingCenteringPadding);
}

qreal muriMarkerAnchorY(
    const TimelineSceneState& state,
    const TimelineRenderLine& line,
    const TimelineRenderNote& note,
    double triggerSecond)
{
    const int startLane = qBound(1, note.lane, kPlayableLaneCount);
    const qreal startY = static_cast<qreal>(
        state.timelineTop + (startLane - 1) * kLaneHeight + kLaneHeight / 2);
    if (note.kind != TimelineRenderNoteKind::Slide && note.kind != TimelineRenderNoteKind::Wifi) {
        return startY;
    }

    const double startSecond = timelineRenderAbsoluteSecond(line, note.secondOffset);
    const double traceSecond = note.slideTraceSecondOffset >= 0.0
        ? timelineRenderAbsoluteSecond(line, note.slideTraceSecondOffset)
        : startSecond;
    const double endSecond = note.endSecondOffset >= 0.0
        ? timelineRenderAbsoluteSecond(line, note.endSecondOffset)
        : traceSecond;
    if (!(traceSecond > startSecond && endSecond > traceSecond)) {
        return startY;
    }

    const int endLane = qBound(1, note.endLane, kPlayableLaneCount);
    const qreal endY = static_cast<qreal>(
        state.timelineTop + (endLane - 1) * kLaneHeight + kLaneHeight / 2);
    if (triggerSecond <= traceSecond) {
        return startY;
    }
    if (triggerSecond >= endSecond) {
        return endY;
    }

    const qreal proportion = qBound<qreal>(
        0.0,
        static_cast<qreal>((triggerSecond - traceSecond) / (endSecond - traceSecond)),
        1.0
    );
    return startY + (endY - startY) * proportion;
}

double xToSecond(const TimelineSceneLayoutMetrics& metrics, int horizontalScrollValue, qreal x)
{
    return qMax(
        0.0,
        metrics.displayStartSeconds
            + ((x + horizontalScrollValue - metrics.leadingCenteringPadding - metrics.timelineLeft)
               / metrics.pixelsPerSecond));
}

double xToSecond(const TimelineSceneState& state, qreal x)
{
    return qMax(
        0.0,
        state.displayStartSeconds
            + ((x + state.horizontalScrollValue - state.leadingCenteringPadding - state.timelineLeft)
               / state.pixelsPerSecond));
}

int preferredRenderedSubdivisionBeats(int sourceSubdivisionBeats)
{
    const int normalizedSource = qMax(1, sourceSubdivisionBeats);
    if (normalizedSource <= kTimelineMaxRenderedSubdivisionBeats) {
        return normalizedSource;
    }
    for (int candidate = kTimelineMaxRenderedSubdivisionBeats; candidate >= 1; --candidate) {
        if ((normalizedSource % candidate) == 0) {
            return candidate;
        }
    }
    return 1;
}

bool shouldPaintTimelineBeatMarker(const TimelineRenderBeat& beat)
{
    const int sourceSubdivisionBeats = qMax(1, beat.subdivisionBeats);
    const int renderedSubdivisionBeats = preferredRenderedSubdivisionBeats(sourceSubdivisionBeats);
    if (renderedSubdivisionBeats >= sourceSubdivisionBeats) {
        return true;
    }
    const int stride = qMax(1, sourceSubdivisionBeats / renderedSubdivisionBeats);
    return (beat.subdivisionIndex % stride) == 0;
}

QFont scaledTimelineHeaderFont(const QFont& sourceFont, qreal scale)
{
    QFont scaledFont(sourceFont);
    const qreal clampedScale = qMax(0.1, scale);
    if (scaledFont.pointSizeF() > 0.0) {
        scaledFont.setPointSizeF(qMax(1.0, scaledFont.pointSizeF() * clampedScale));
    } else if (scaledFont.pointSize() > 0) {
        scaledFont.setPointSizeF(qMax(1.0, static_cast<qreal>(scaledFont.pointSize()) * clampedScale));
    } else if (scaledFont.pixelSize() > 0) {
        scaledFont.setPixelSize(qMax(1, qRound(static_cast<qreal>(scaledFont.pixelSize()) * clampedScale)));
    }
    return scaledFont;
}

qreal timelineHeaderLabelScale(const QFont& baseFont, int digitCount)
{
    if (digitCount <= 1) {
        return kTimelineHeaderSingleDigitFontScale;
    }
    const qreal multiDigitWidthBudget = qMax<qreal>(
        8.0,
        static_cast<qreal>(kTimelineHeaderLineLabelMinSpacingPx - kTimelineHeaderMultiDigitLabelSideGapPx));
    const QString widthSample(qMax(1, digitCount), QLatin1Char('8'));
    const qreal widthScale = multiDigitWidthBudget
        / qMax<qreal>(1.0, static_cast<qreal>(QFontMetricsF(baseFont).horizontalAdvance(widthSample)));
    return qMin(kTimelineHeaderMultiDigitBaseFontScale, widthScale);
}

int timelineHeaderLabelHalfWidthPx(const QFont& baseFont, const QString& labelText)
{
    if (labelText.isEmpty()) {
        return 0;
    }
    const QFont labelFont = scaledTimelineHeaderFont(baseFont, timelineHeaderLabelScale(baseFont, labelText.size()));
    return qCeil(QFontMetricsF(labelFont).horizontalAdvance(labelText) * 0.5) + 1;
}

void appendTrackLine(
    QVector<TimelineSceneLine>* lines,
    const QPointF& start,
    const QPointF& end,
    const QColor& color,
    qreal width)
{
    if (lines == nullptr) {
        return;
    }
    lines->append(TimelineSceneLine{start, end, color, width});
}

void appendGlyph(
    QVector<TimelineSceneGlyph>* glyphs,
    const QRectF& rect,
    TimelineSceneGlyphShape shape,
    const QColor& fillColor,
    const QColor& strokeColor = QColor(),
    qreal strokeWidth = 0.0,
    qreal radius = 0.0)
{
    if (glyphs == nullptr) {
        return;
    }
    TimelineSceneGlyph glyph;
    glyph.rect = rect;
    glyph.shape = shape;
    glyph.fillColor = fillColor;
    glyph.strokeColor = strokeColor;
    glyph.strokeWidth = strokeWidth;
    glyph.radius = radius;
    glyphs->append(glyph);
}

void appendSprite(
    QVector<TimelineSceneSprite>* sprites,
    const QPointF& center,
    const QString& spriteType,
    qreal scale = 1.0,
    qreal rotationDegrees = 0.0,
    bool mirrorX = false)
{
    if (sprites == nullptr || spriteType.isEmpty()) {
        return;
    }
    TimelineSceneSprite sprite;
    sprite.center = center;
    sprite.spriteType = spriteType;
    sprite.scale = scale;
    sprite.rotationDegrees = rotationDegrees;
    sprite.mirrorX = mirrorX;
    sprites->append(sprite);
}

TimelineSceneLayoutMetrics buildLayoutMetrics(const TimelineSceneBuildRequest& request)
{
    TimelineSceneLayoutMetrics metrics;
    metrics.viewportSize = request.viewportSize;
    metrics.timelineLeft = kTimelineLeftMargin;
    metrics.timelineTop = kHeaderHeight + kTimelineTopMargin;
    metrics.timelineHeight = kLaneCount * kLaneHeight;
    metrics.laneHeight = kLaneHeight;
    metrics.laneCount = kLaneCount;
    metrics.pixelsPerSecond = pixelsPerSecondForZoom(request.zoomScale);
    metrics.maxNavigableSecond = maxNavigableSecond(request);
    metrics.displayStartSeconds =
        qMin(-kTimelineDisplayLeadInSeconds, request.snapshot.minimumSecond - kTimelineDisplayLeadInSeconds);
    metrics.displayEndSeconds = qMax(metrics.displayStartSeconds + 1.0, metrics.maxNavigableSecond + 1.0);
    metrics.leadingCenteringPadding = qMax(
        0,
        (request.viewportSize.width() / 2)
            - rawSecondToX(0.0, metrics.displayStartSeconds, metrics.pixelsPerSecond));
    metrics.trailingCenteringPadding = qMax(
        0,
        rawSecondToX(metrics.maxNavigableSecond, metrics.displayStartSeconds, metrics.pixelsPerSecond)
                + (request.viewportSize.width() / 2)
            - rawContentWidth(metrics.displayStartSeconds, metrics.displayEndSeconds, metrics.pixelsPerSecond));
    metrics.contentWidth =
        rawContentWidth(metrics.displayStartSeconds, metrics.displayEndSeconds, metrics.pixelsPerSecond)
        + metrics.leadingCenteringPadding + metrics.trailingCenteringPadding;
    return metrics;
}

void applyLayoutMetrics(TimelineSceneState* state, const TimelineSceneLayoutMetrics& metrics)
{
    if (state == nullptr) {
        return;
    }
    state->viewportSize = metrics.viewportSize;
    state->timelineLeft = metrics.timelineLeft;
    state->timelineTop = metrics.timelineTop;
    state->timelineHeight = metrics.timelineHeight;
    state->laneHeight = metrics.laneHeight;
    state->laneCount = metrics.laneCount;
    state->leadingCenteringPadding = metrics.leadingCenteringPadding;
    state->trailingCenteringPadding = metrics.trailingCenteringPadding;
    state->contentWidth = metrics.contentWidth;
    state->displayStartSeconds = metrics.displayStartSeconds;
    state->displayEndSeconds = metrics.displayEndSeconds;
    state->pixelsPerSecond = metrics.pixelsPerSecond;
    state->maxNavigableSecond = metrics.maxNavigableSecond;
}

}  // namespace

namespace miacode::timeline {

TimelineSceneLayoutMetrics TimelineSceneStateBuilder::layoutMetrics(const TimelineSceneBuildRequest& request)
{
    return buildLayoutMetrics(request);
}

int TimelineSceneStateBuilder::maxHorizontalScrollValue(const TimelineSceneLayoutMetrics& metrics)
{
    return qMax(0, metrics.contentWidth - metrics.viewportSize.width());
}

int TimelineSceneStateBuilder::secondToSceneX(const TimelineSceneLayoutMetrics& metrics, double second)
{
    return ::secondToX(metrics, second);
}

TimelineSceneState TimelineSceneStateBuilder::build(const TimelineSceneBuildRequest& request)
{
    TimelineSceneState state;
    const TimelineSceneLayoutMetrics metrics = buildLayoutMetrics(request);
    applyLayoutMetrics(&state, metrics);
    state.horizontalScrollValue = qMax(0, request.horizontalScrollValue);
    state.headerLeftLimit = qMax(0, request.headerLeftLimit);
    state.headerRightLimit = request.headerRightLimit > 0 ? request.headerRightLimit : request.viewportSize.width();
    state.visibleStartSecond = xToSecond(state, state.timelineLeft);
    state.visibleEndSecond = xToSecond(state, request.viewportSize.width());
    state.appearanceRevision = request.appearanceRevision;
    state.gridRevision = request.gridRevision;
    state.waveformRevision = request.waveformRevision;
    state.headerRevision = request.headerRevision;
    state.notesRevision = request.notesRevision;
    state.overlayRevision = request.overlayRevision;
    state.overlayDynamicRevision = request.overlayDynamicRevision;

    const TimelineThemeColors theme = timelineThemeColors();
    const QFont laneLabelFont = timelineLaneLabelFont();
    state.baseBackgroundRects.append(TimelineSceneRect{
        QRectF(0.0, 0.0, request.viewportSize.width(), request.viewportSize.height()),
        theme.window,
    });
    {
        // Phase 9d-native polish — force the header-band fill opaque
        // so the native-rendered controls (zoom button + follow check
        // text) sit on a solid background. theme.header inherits a
        // semi-transparent alpha from the palette which lets
        // scrolling content leak through the text label area.
        QColor opaqueHeader = theme.header;
        opaqueHeader.setAlpha(255);
        state.baseBackgroundRects.append(TimelineSceneRect{
            QRectF(0.0, 0.0, request.viewportSize.width(), state.timelineTop),
            opaqueHeader,
        });
    }
    {
        // Phase 9a-fix4 — opaque sidebar (matches the frameRect below).
        QColor opaqueSidebar = theme.sidebar;
        opaqueSidebar.setAlpha(255);
        state.baseBackgroundRects.append(TimelineSceneRect{
            QRectF(0.0, state.timelineTop, state.timelineLeft, state.timelineHeight),
            opaqueSidebar,
        });
    }
    state.baseBackgroundRects.append(TimelineSceneRect{
        QRectF(state.timelineLeft, state.timelineTop, request.viewportSize.width() - state.timelineLeft, state.timelineHeight),
        theme.base,
    });
    {
        // Phase 9a-fix4 — force the sidebar fill to fully opaque alpha
        // so notes that scroll-translate into viewport-X < timelineLeft
        // can't bleed through during scroll.
        QColor opaqueSidebar = theme.sidebar;
        opaqueSidebar.setAlpha(255);
        const QRectF sidebarRect(
            0.0, state.timelineTop - 1.0,
            state.timelineLeft + 1.0, state.timelineHeight + 2.0);
        state.frameRects.append(TimelineSceneRect{sidebarRect, opaqueSidebar});
        // Phase 4d-fix — same rect, published as a separate field so
        // TimelineHeaderSource can re-emit it AFTER notes/sprites at
        // z=3. This masks any chart-content that scrolls into the
        // lane-label column (e.g. slide-trace arrows extending toward
        // the off-screen-left edge).
        state.sidebarMaskRect = TimelineSceneRect{sidebarRect, opaqueSidebar};
    }
    state.frameLines.append(TimelineSceneLine{
        QPointF(0.0, state.timelineTop - 1.0),
        QPointF(request.viewportSize.width(), state.timelineTop - 1.0),
        theme.border,
        1.0,
    });
    state.frameLines.append(TimelineSceneLine{
        QPointF(state.timelineLeft, state.timelineTop - 1.0),
        QPointF(state.timelineLeft, state.timelineTop + state.timelineHeight),
        theme.axis,
        1.0,
    });
    state.frameLines.append(TimelineSceneLine{
        QPointF(0.0, 0.0),
        QPointF(request.viewportSize.width() - 1.0, 0.0),
        theme.border,
        1.0,
    });
    state.frameLines.append(TimelineSceneLine{
        QPointF(0.0, 0.0),
        QPointF(0.0, state.timelineTop + state.timelineHeight),
        theme.border,
        1.0,
    });
    state.frameLines.append(TimelineSceneLine{
        QPointF(request.viewportSize.width() - 1.0, 0.0),
        QPointF(request.viewportSize.width() - 1.0, state.timelineTop + state.timelineHeight),
        theme.border,
        1.0,
    });
    state.frameLines.append(TimelineSceneLine{
        QPointF(0.0, state.timelineTop + state.timelineHeight),
        QPointF(request.viewportSize.width() - 1.0, state.timelineTop + state.timelineHeight),
        theme.border,
        1.0,
    });

    for (int lane = 0; lane < kLaneCount; ++lane) {
        const qreal y = state.timelineTop + lane * kLaneHeight;
        // Match the widget path: waveform is drawn first, then the semi-transparent
        // lane row fills are composited on top to get the final perceived color.
        state.laneOverlayRects.append(TimelineSceneRect{
            QRectF(state.timelineLeft, y, request.viewportSize.width() - state.timelineLeft, kLaneHeight),
            (lane % 2 == 0) ? theme.laneEven : theme.laneOdd,
        });
        TimelineSceneTextLabel label;
        label.text = laneLabelForIndex(lane);
        label.font = laneLabelFont;
        label.color = theme.label;
        label.logicalSize = timelineTextLogicalSize(label.font, label.text);
        const QFontMetricsF laneMetrics(label.font);
        const qreal textLeft = 4.0
            + qMax<qreal>(
                0.0,
                static_cast<qreal>(state.timelineLeft - 8) - laneMetrics.horizontalAdvance(label.text));
        const qreal textTop = y + 1.0
            + qMax<qreal>(0.0, (static_cast<qreal>(kLaneHeight - 1) - laneMetrics.height()) * 0.5);
        label.topLeft = QPointF(
            textLeft - kTimelineTextHorizontalPadding,
            textTop - kTimelineTextVerticalPadding);
        state.laneLabels.append(label);
    }

    if (request.waveformData && request.waveformData->durationSeconds > 0.0) {
        const miacode::waveform::WaveformLevel* waveformLevel =
            miacode::waveform::selectWaveformLevelForVisibleRange(
                *request.waveformData,
                qMax(0.001, state.visibleEndSecond - state.visibleStartSecond),
                qMax(1, request.viewportSize.width() - state.timelineLeft));
        if (waveformLevel != nullptr && !waveformLevel->columns.isEmpty()) {
            // Phase 7 — scroll-bucket culling. When the caller has
            // opted in via request.horizontalCullPaddingPx > 0 and
            // bumps revisions per scroll bucket (TimelineQuickItem
            // does), narrow the column range to viewport ± padding
            // seconds. When the caller hasn't opted in, fall back to
            // the full display range to preserve legacy behaviour
            // (the QSG waveform layer caches children by revision; if
            // we cull without forcing a rebuild on scroll, scrolled-
            // into areas would reveal empty space — see Phase 5
            // post-mortem).
            double cullStartSecond = state.displayStartSeconds;
            double cullEndSecond = state.displayEndSeconds;
            if (request.horizontalCullPaddingPx > 0 && state.pixelsPerSecond > 1e-6) {
                const double paddingSec =
                    static_cast<double>(request.horizontalCullPaddingPx) / state.pixelsPerSecond;
                cullStartSecond = qMax(0.0, state.visibleStartSecond - paddingSec);
                cullEndSecond = state.visibleEndSecond + paddingSec;
            }
            const QPair<int, int> visibleColumns =
                miacode::waveform::visibleWaveformColumnRange(
                    *waveformLevel,
                    qMax(0.0, cullStartSecond),
                    qMax(0.0, cullEndSecond));
            const qreal centerY = state.timelineTop + state.timelineHeight / 2.0;
            const qreal maxAmplitude = (qMax<qreal>(8.0, state.timelineHeight / 2.0 - 8.0) * 7.0) / 9.0;
            for (int index = visibleColumns.first; index < visibleColumns.second; ++index) {
                const miacode::waveform::WaveformColumn& column = waveformLevel->columns.at(index);
                if (qAbs(column.max - column.min) <= 1e-5f) {
                    continue;
                }
                const double columnStartSecond = waveformLevel->secondsPerColumn * static_cast<double>(index);
                const double columnEndSecond = columnStartSecond + waveformLevel->secondsPerColumn;
                int x0 = secondToSceneX(state, columnStartSecond);
                int x1 = secondToSceneX(state, columnEndSecond);
                if (x1 <= x0) {
                    x1 = x0 + 1;
                }
                const qreal topY = centerY - (qBound(-1.0f, column.max, 1.0f) * maxAmplitude);
                const qreal bottomY = centerY - (qBound(-1.0f, column.min, 1.0f) * maxAmplitude);
                state.waveformBars.append(TimelineSceneRect{
                    QRectF(x0, qMin(topY, bottomY), qMax(1, x1 - x0), qMax<qreal>(1.0, qAbs(bottomY - topY))),
                    theme.waveform,
                });
            }
        }
    }

    // Phase 7 — scroll-bucket grid culling. Same gate as the waveform
    // path: only cull when the caller has opted in via
    // horizontalCullPaddingPx > 0 (and is bucket-bumping revisions
    // upstream). The cull is in CONTENT-X space, expanded by
    // padding, so a grid line whose content X falls outside the
    // bucket window is dropped. When padding=0, no cull (legacy).
    qreal gridCullMinX = -std::numeric_limits<qreal>::infinity();
    qreal gridCullMaxX = std::numeric_limits<qreal>::infinity();
    if (request.horizontalCullPaddingPx > 0) {
        const qreal scrollX = static_cast<qreal>(state.horizontalScrollValue);
        const qreal padding = static_cast<qreal>(request.horizontalCullPaddingPx);
        gridCullMinX = scrollX + state.timelineLeft - padding - 2.0;
        gridCullMaxX = scrollX + state.viewportSize.width() + padding + 2.0;
    }
    const auto addGridLine = [&](double absoluteSecond, const QColor& color, qreal width, bool exactPosition) {
        const qreal x = (exactPosition ? secondToXExact(state, absoluteSecond)
                                       : static_cast<qreal>(secondToSceneX(state, absoluteSecond)));
        if (x < gridCullMinX || x > gridCullMaxX) {
            return;
        }
        state.gridLines.append(TimelineSceneLine{
            QPointF(x, state.timelineTop),
            QPointF(x, state.timelineTop + state.timelineHeight),
            color,
            width,
        });
    };

    TimelineVisibleLineRange beatRange;
    beatRange.begin = 0;
    beatRange.end = static_cast<int>(request.snapshot.lines.size());

    for (int lineIndex = beatRange.begin; lineIndex < beatRange.end; ++lineIndex) {
        const TimelineRenderLine& line = request.snapshot.lines.at(lineIndex);
        for (const TimelineRenderBeat& marker : line.beats) {
            if (shouldPaintTimelineBeatMarker(marker)) {
                addGridLine(timelineRenderAbsoluteSecond(line, marker.secondOffset), theme.gridMinor, 1.0, true);
            }
        }
    }
    for (double measureSecond : request.snapshot.measureLineSeconds) {
        addGridLine(measureSecond, theme.gridMajor, kTimelineBeatLineWidth, false);
    }

    if (request.snapshot.trailingMeasureLineStepSeconds > 1e-6) {
        double extensionSecond = request.snapshot.trailingMeasureLineStartSecond;
        if (!request.snapshot.measureLineSeconds.isEmpty()
            && extensionSecond <= request.snapshot.measureLineSeconds.constLast() + 1e-6) {
            const double delta = request.snapshot.measureLineSeconds.constLast() - extensionSecond;
            extensionSecond += request.snapshot.trailingMeasureLineStepSeconds
                * static_cast<double>(qMax<qint64>(1, static_cast<qint64>(qFloor(delta / request.snapshot.trailingMeasureLineStepSeconds)) + 1));
        }
        if (extensionSecond <= state.displayStartSeconds - 1.0 + 1e-6) {
            const double delta = (state.displayStartSeconds - 1.0) - extensionSecond;
            extensionSecond += request.snapshot.trailingMeasureLineStepSeconds
                * static_cast<double>(qMax<qint64>(1, static_cast<qint64>(qFloor(delta / request.snapshot.trailingMeasureLineStepSeconds)) + 1));
        }
        for (; extensionSecond <= state.displayEndSeconds + 1.0 + 1e-6;
             extensionSecond += request.snapshot.trailingMeasureLineStepSeconds) {
            addGridLine(extensionSecond, theme.gridMajor, kTimelineBeatLineWidth, false);
        }
    }

    QVector<HeaderLineLabel> headerLabels;
    if (!request.snapshot.lines.isEmpty()) {
        QVector<HeaderLineLabel> collapsed;
        for (auto it = request.snapshot.lines.cbegin(); it != request.snapshot.lines.cend(); ++it) {
            const TimelineRenderLine& line = *it;
            const int screenX = secondToSceneX(state, line.startSecond);
            HeaderLineLabel label{qMax(1, line.lineNumber), line.startSecond, screenX};
            if (!collapsed.isEmpty()) {
                HeaderLineLabel& previous = collapsed.last();
                if (previous.screenX == label.screenX
                    || qAbs(previous.second - label.second) <= kTimelineHeaderLineAnchorToleranceSeconds) {
                    previous = label;
                    continue;
                }
            }
            collapsed.append(label);
        }
        for (const HeaderLineLabel& label : collapsed) {
            if (!headerLabels.isEmpty()
                && label.screenX - headerLabels.constLast().screenX < kTimelineHeaderLineLabelMinSpacingPx) {
                continue;
            }
            headerLabels.append(label);
        }
    }

    const QFont oneDigitFont =
        scaledTimelineHeaderFont(request.headerLineNumberFont, kTimelineHeaderSingleDigitFontScale);
    const QFontMetricsF oneDigitMetrics(oneDigitFont);
    qreal singleDigitWidth = 0.0;
    for (QChar digit = QLatin1Char('0'); digit <= QLatin1Char('9'); digit = QChar(digit.unicode() + 1)) {
        singleDigitWidth = qMax(singleDigitWidth, static_cast<qreal>(oneDigitMetrics.horizontalAdvance(digit)));
    }
    const qreal markerTipY = static_cast<qreal>(state.timelineTop) - kTimelineTopMarkerTipOffsetPx;
    const qreal headerTextBottom = (static_cast<qreal>(state.timelineTop - oneDigitMetrics.height()) * 0.5)
        + oneDigitMetrics.height();
    const qreal markerHeight = qMin(
        qMax(0.0, markerTipY - headerTextBottom - kTimelineHeaderAnchorMarkerTextGapPx),
        singleDigitWidth * kTimelineHeaderAnchorMarkerLegacyWidthFactor * kTimelineHeaderAnchorMarkerLegacyHeightFactor);
    for (const HeaderLineLabel& label : headerLabels) {
        const QString labelText = QString::number(label.lineNumber);
        const QFont labelFont = scaledTimelineHeaderFont(
            request.headerLineNumberFont,
            timelineHeaderLabelScale(request.headerLineNumberFont, labelText.size()));
        const QFontMetricsF labelMetrics(labelFont);
        state.headerLabels.append(TimelineSceneTextLabel{
            labelText,
            QPointF(
                label.screenX - (labelMetrics.horizontalAdvance(labelText) * 0.5) - kTimelineTextHorizontalPadding,
                headerTextBottom - labelMetrics.height() - kTimelineTextVerticalPadding),
            labelFont,
            theme.textSecondary,
            timelineTextLogicalSize(labelFont, labelText),
        });
        if (markerHeight >= 2.0) {
            const qreal markerBaseY = markerTipY - markerHeight;
            const qreal markerHalfWidth = markerHeight * kTimelineTopMarkerHalfWidthPerHeight;
            state.headerMarkers.append(TimelineSceneTriangle{
                QPointF(label.screenX - markerHalfWidth, markerBaseY),
                QPointF(label.screenX + markerHalfWidth, markerBaseY),
                QPointF(label.screenX, markerTipY),
                theme.textSecondary,
            });
        }
    }

    // Phase 7 — scroll-bucket note culling. When the caller has opted
    // in via horizontalCullPaddingPx (and bumps notesRevision per
    // bucket), bisect the line range against the visible viewport
    // expanded by padding. This mirrors what legacy TimelineView
    // already does (TimelineView.Paint.cpp:44). When the caller
    // hasn't opted in, fall back to the full chart range to preserve
    // legacy behaviour.
    TimelineVisibleLineRange visibleNoteRange{0, static_cast<int>(request.snapshot.lines.size())};
    if (request.horizontalCullPaddingPx > 0 && state.pixelsPerSecond > 1e-6) {
        const QVector<double>& noteVisualPrefixMax =
            (request.showSlideTracks || !request.muriMarkersByLocation.isEmpty())
                ? request.snapshot.noteVisualEndPrefixMaxWithSlideTracks
                : request.snapshot.noteVisualEndPrefixMaxWithoutSlideTracks;
        if (noteVisualPrefixMax.size() == request.snapshot.lines.size()) {
            const double paddingSec =
                static_cast<double>(request.horizontalCullPaddingPx) / state.pixelsPerSecond;
            // 2 s extra slop on top of bucket padding — covers tail-
            // extending notes (long slides, fireworks) whose visual
            // range exceeds the head's second.
            const double slopSec = 2.0;
            visibleNoteRange = timelineRenderVisibleNoteLineRange(
                request.snapshot.lines,
                noteVisualPrefixMax,
                state.visibleStartSecond - paddingSec - slopSec,
                state.visibleEndSecond + paddingSec + slopSec);
        }
    }
    const QVector<TimelineVisibleNoteRef> visibleNoteRefs =
        timelineRenderVisibleNotePaintOrder(request.snapshot.lines, visibleNoteRange);

    struct TimelineHeadLayerNoteRef {
        TimelineVisibleNoteRef ref;
        double second = 0.0;
        int sourceSequence = 0;
    };
    QVector<TimelineHeadLayerNoteRef> holdTapHeadRefs;
    holdTapHeadRefs.reserve(visibleNoteRefs.size());
    for (int sequence = 0; sequence < visibleNoteRefs.size(); ++sequence) {
        const TimelineVisibleNoteRef& visibleRef = visibleNoteRefs.at(sequence);
        const TimelineRenderLine& line = request.snapshot.lines.at(visibleRef.lineIndex);
        const TimelineRenderNote& note = line.notes.at(visibleRef.noteIndex);
        switch (note.kind) {
        case TimelineRenderNoteKind::Tap:
        case TimelineRenderNoteKind::Hold:
        case TimelineRenderNoteKind::Slide:
        case TimelineRenderNoteKind::Wifi:
            holdTapHeadRefs.append(TimelineHeadLayerNoteRef{
                visibleRef,
                timelineRenderAbsoluteSecond(line, note.secondOffset),
                sequence,
            });
            break;
        default:
            break;
        }
    }
    std::sort(holdTapHeadRefs.begin(), holdTapHeadRefs.end(), [](const TimelineHeadLayerNoteRef& a, const TimelineHeadLayerNoteRef& b) {
        if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
            return a.second > b.second;
        }
        return a.sourceSequence > b.sourceSequence;
    });

    const auto noteSecond = [](const TimelineRenderLine& line, const TimelineRenderNote& note) {
        return timelineRenderAbsoluteSecond(line, note.secondOffset);
    };
    const auto noteEndSecond = [](const TimelineRenderLine& line, const TimelineRenderNote& note) {
        return note.endSecondOffset >= 0.0 ? timelineRenderAbsoluteSecond(line, note.endSecondOffset) : -1.0;
    };
    const auto noteTraceSecond = [](const TimelineRenderLine& line, const TimelineRenderNote& note) {
        return note.slideTraceSecondOffset >= 0.0 ? timelineRenderAbsoluteSecond(line, note.slideTraceSecondOffset) : -1.0;
    };
    const auto tapIconTypeForFlags = [](bool isBreak, bool isEach, bool isEx) {
        if (isEx && isBreak) return QStringLiteral("tap_break_ex");
        if (isEx && isEach) return QStringLiteral("tap_each_ex");
        if (isEx) return QStringLiteral("tap_ex");
        if (isBreak) return QStringLiteral("tap_break");
        if (isEach) return QStringLiteral("tap_each");
        return QStringLiteral("tap");
    };
    const auto holdIconTypeForFlags = [](bool isBreak, bool isEach, bool isEx) {
        if (isEx && isBreak) return QStringLiteral("hold_break_ex");
        if (isEx && isEach) return QStringLiteral("hold_each_ex");
        if (isEx) return QStringLiteral("hold_ex");
        if (isBreak) return QStringLiteral("hold_break");
        if (isEach) return QStringLiteral("hold_each");
        return QStringLiteral("hold");
    };
    const auto starIconTypeForFlags = [](bool isBreak, bool isEach, bool isEx, bool isDouble) {
        if (isEx && isBreak && isDouble) return QStringLiteral("star_break_ex_double");
        if (isEx && isBreak) return QStringLiteral("star_break_ex");
        if (isEx && isEach && isDouble) return QStringLiteral("star_each_ex_double");
        if (isEx && isEach) return QStringLiteral("star_each_ex");
        if (isEx && isDouble) return QStringLiteral("star_ex_double");
        if (isEx) return QStringLiteral("star_ex");
        if (isBreak && isDouble) return QStringLiteral("star_break_double");
        if (isBreak) return QStringLiteral("star_break");
        if (isEach && isDouble) return QStringLiteral("star_each_double");
        if (isDouble) return QStringLiteral("star_double");
        if (isEach) return QStringLiteral("star_each");
        return QStringLiteral("slide");
    };

    const auto appendNoteForRef = [&](const TimelineVisibleNoteRef& visibleRef, bool trackLayer) {
        const TimelineRenderLine& line = request.snapshot.lines.at(visibleRef.lineIndex);
        const TimelineRenderNote& note = line.notes.at(visibleRef.noteIndex);
        if (note.lane < 1 || note.lane > kLaneCount) {
            return;
        }

        const double startSecond = noteSecond(line, note);
        const double endSecond = noteEndSecond(line, note);
        const double traceSecond = noteTraceSecond(line, note);
        const quint64 locationId = timelineRenderLocationId(line, note);
        const QVector<TimelineMuriMarkerPlacement>* noteMuriMarkers = nullptr;
        if (const auto markersIt = request.muriMarkersByLocation.constFind(locationId);
            markersIt != request.muriMarkersByLocation.constEnd()) {
            noteMuriMarkers = &markersIt.value();
        }
        const bool isHold = note.kind == TimelineRenderNoteKind::Hold && endSecond >= startSecond;
        const bool isTouchHold = note.kind == TimelineRenderNoteKind::TouchHold && endSecond > startSecond;
        const bool isSlideLike = note.kind == TimelineRenderNoteKind::Slide || note.kind == TimelineRenderNoteKind::Wifi;
        const bool isSlideTrack = isSlideLike && traceSecond > startSecond && endSecond > traceSecond;

        const int x = secondToSceneX(state, startSecond);
        const int holdEndX = isHold ? secondToSceneX(state, endSecond) : x;
        const int slideStartX = isSlideTrack ? secondToSceneX(state, traceSecond) : x;
        const int slideEndX = isSlideTrack ? secondToSceneX(state, endSecond) : x;
        int extentLeft = x;
        int extentRight = x;
        if (isHold || isTouchHold) {
            const int endX = secondToSceneX(state, endSecond);
            extentLeft = qMin(extentLeft, endX);
            extentRight = qMax(extentRight, endX);
        }
        if (request.showSlideTracks && isSlideTrack) {
            extentLeft = qMin(extentLeft, qMin(slideStartX, slideEndX));
            extentRight = qMax(extentRight, qMax(slideStartX, slideEndX));
        }
        if (noteMuriMarkers != nullptr) {
            for (const TimelineMuriMarkerPlacement& markerPlacement : *noteMuriMarkers) {
                if (!markerPlacement.useTriggerSecond) {
                    continue;
                }
                const int markerX = secondToSceneX(state, markerPlacement.second);
                extentLeft = qMin(extentLeft, markerX);
                extentRight = qMax(extentRight, markerX);
            }
        }
        if (extentRight < state.timelineLeft - kNoteSize || extentLeft > state.contentWidth + kNoteSize) {
            return;
        }

        const int rowTop = state.timelineTop + (note.lane - 1) * kLaneHeight;
        const int rowCenterY = rowTop + (kLaneHeight / 2);
        const qreal baseIconScale = request.zoomScale <= 0.25 ? 0.5 : 1.0;
        QString iconType;
        switch (note.kind) {
        case TimelineRenderNoteKind::Tap:
            if (timelineRenderFlagSet(note, TimelineRenderFlagTapUsesStarMaterial)) {
                iconType = starIconTypeForFlags(
                    timelineRenderFlagSet(note, TimelineRenderFlagIsBreak),
                    timelineRenderFlagSet(note, TimelineRenderFlagIsEach),
                    timelineRenderFlagSet(note, TimelineRenderFlagIsEx),
                    timelineRenderFlagSet(note, TimelineRenderFlagTapStarDouble));
            } else {
                iconType = tapIconTypeForFlags(
                    timelineRenderFlagSet(note, TimelineRenderFlagIsBreak),
                    timelineRenderFlagSet(note, TimelineRenderFlagIsEach),
                    timelineRenderFlagSet(note, TimelineRenderFlagIsEx));
            }
            break;
        case TimelineRenderNoteKind::Hold:
            iconType = holdIconTypeForFlags(
                timelineRenderFlagSet(note, TimelineRenderFlagIsBreak),
                timelineRenderFlagSet(note, TimelineRenderFlagIsEach),
                timelineRenderFlagSet(note, TimelineRenderFlagIsEx));
            break;
        case TimelineRenderNoteKind::Touch:
            iconType = timelineRenderFlagSet(note, TimelineRenderFlagIsBreak)
                ? QStringLiteral("touch_break")
                : (timelineRenderFlagSet(note, TimelineRenderFlagIsEach) ? QStringLiteral("touch_each") : QStringLiteral("touch"));
            break;
        case TimelineRenderNoteKind::TouchHold:
            iconType = timelineRenderFlagSet(note, TimelineRenderFlagIsBreak)
                ? QStringLiteral("touch_hold_border_only_break")
                : (timelineRenderFlagSet(note, TimelineRenderFlagIsEach)
                       ? QStringLiteral("touch_hold_border_only_each")
                       : QStringLiteral("touch_hold_border_only"));
            break;
        case TimelineRenderNoteKind::Slide:
        case TimelineRenderNoteKind::Wifi:
            if (timelineRenderFlagSet(note, TimelineRenderFlagSlideHeadUsesTapMaterial)) {
                iconType = tapIconTypeForFlags(
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadBreak),
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadEach),
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadEx));
            } else {
                iconType = starIconTypeForFlags(
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadBreak),
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadEach),
                    timelineRenderFlagSet(note, TimelineRenderFlagHeadEx),
                    timelineRenderFlagSet(note, TimelineRenderFlagSameHeadSlide));
            }
            break;
        default:
            iconType = QStringLiteral("tap");
            break;
        }

        if (trackLayer) {
            if (!request.showSlideTracks || !isSlideTrack) {
                return;
            }
            const auto& noteAssets = sceneNoteAssets();
            const int startLane = qBound(1, note.lane, kPlayableLaneCount);
            const int endLane = qBound(1, note.endLane, kPlayableLaneCount);
            const int startY = state.timelineTop + (startLane - 1) * kLaneHeight + kLaneHeight / 2;
            const int endY = state.timelineTop + (endLane - 1) * kLaneHeight + kLaneHeight / 2;
            const qreal dx = static_cast<qreal>(slideEndX - slideStartX);
            const qreal dy = static_cast<qreal>(endY - startY);
            const qreal length = qSqrt(dx * dx + dy * dy);
            const bool forward = slideEndX >= slideStartX;
            QString baseTrackType = QStringLiteral("slide_track");
            if (timelineRenderFlagSet(note, TimelineRenderFlagTrackBreak)) {
                baseTrackType = QStringLiteral("slide_track_break");
            } else if (timelineRenderFlagSet(note, TimelineRenderFlagSlideEach)) {
                baseTrackType = QStringLiteral("slide_track_each");
            }
            const qreal trackScale = qBound<qreal>(0.25, request.zoomScale, 1.0);
            const QSize trackTargetSize =
                miacode::timeline::targetSizeForNoteType(noteAssets, baseTrackType, trackScale);
            if (!trackTargetSize.isValid()) {
                return;
            }
            const qreal angleDegrees = (!qFuzzyIsNull(dx) || !qFuzzyIsNull(dy))
                ? qRadiansToDegrees(qAtan2(dy, dx))
                : 0.0;
            const qreal minSpacing = trackScale <= 0.25 ? 1.0 : 4.0;
            const qreal spacing =
                qMax<qreal>(minSpacing, static_cast<qreal>(trackTargetSize.width()) * 0.72 + 1.0);
            const int steps = qMax(1, static_cast<int>(length / spacing));
            for (int step = 0; step <= steps; ++step) {
                const qreal t = static_cast<qreal>(step) / static_cast<qreal>(steps);
                const qreal cx = static_cast<qreal>(slideStartX) + dx * t;
                const qreal cy = static_cast<qreal>(startY) + dy * t;
                appendSprite(
                    &state.trackSprites,
                    QPointF(cx, cy),
                    baseTrackType,
                    trackScale,
                    angleDegrees,
                    forward);
            }
            return;
        }

        if (isHold) {
            state.holdSpans.append(TimelineSceneHoldSpan{
                extentLeft,
                extentRight,
                rowTop,
                kLaneHeight,
                iconType,
                baseIconScale,
                theme.holdBody,
                qMax<qreal>(3.0, kNoteSize * kTimelineHoldThicknessRelativeToTap * request.zoomScale),
            });
        }
        if (isTouchHold) {
            const QColor touchHoldColor = timelineRenderFlagSet(note, TimelineRenderFlagIsEach)
                ? QColor(255, 214, 64, 120)
                : QColor(44, 214, 255, 120);
            appendTrackLine(
                &state.touchHoldLines,
                QPointF(x, rowCenterY),
                QPointF(secondToSceneX(state, endSecond), rowCenterY),
                touchHoldColor,
                4.0);
        }
        if (timelineRenderFlagSet(note, TimelineRenderFlagIsFirework)
            && (note.kind == TimelineRenderNoteKind::Touch || note.kind == TimelineRenderNoteKind::TouchHold)) {
            const double triggerSecond =
                (note.kind == TimelineRenderNoteKind::TouchHold && endSecond > startSecond) ? endSecond : startSecond;
            const int fireLeft = secondToSceneX(state, triggerSecond);
            const int fireRight = secondToSceneX(state, triggerSecond + kTimelineFireworkDurationSeconds);
            const qreal rowHeight = qMax<qreal>(1.0, kLaneHeight - 4.0);
            const qreal bandHeight = rowHeight / static_cast<qreal>(theme.fireworkBands.size());
            for (int bandIndex = 0; bandIndex < static_cast<int>(theme.fireworkBands.size()); ++bandIndex) {
                const qreal bandTop = rowTop + 2.0 + bandHeight * static_cast<qreal>(bandIndex);
                state.fireworkBands.append(TimelineSceneRect{
                    QRectF(
                        fireLeft,
                        bandTop,
                        qMax(1, fireRight - fireLeft),
                        bandIndex + 1 == static_cast<int>(theme.fireworkBands.size())
                            ? ((rowTop + 2.0 + rowHeight) - bandTop)
                            : bandHeight),
                    theme.fireworkBands.at(bandIndex),
                });
            }
        }

        const bool shouldDrawHead = !isSlideLike || timelineRenderFlagSet(note, TimelineRenderFlagHasHeadStar);
        if (!isHold && shouldDrawHead) {
            const QSize iconTargetSize =
                miacode::timeline::targetSizeForNoteType(sceneNoteAssets(), iconType, baseIconScale);
            if (!iconTargetSize.isValid()) {
                return;
            }
            appendSprite(
                &state.noteSprites,
                QPointF(x, rowCenterY),
                iconType,
                baseIconScale);
        }

        if (noteMuriMarkers != nullptr) {
            for (const TimelineMuriMarkerPlacement& markerPlacement : *noteMuriMarkers) {
                const qreal markerX = markerPlacement.useTriggerSecond
                    ? secondToXExact(state, markerPlacement.second)
                    : static_cast<qreal>(x);
                const qreal markerY = markerPlacement.useTriggerSecond
                    ? muriMarkerAnchorY(state, line, note, markerPlacement.second)
                    : static_cast<qreal>(rowCenterY);
                appendGlyph(
                    &state.muriDots,
                    QRectF(markerX + 4.0, markerY - 8.0, 7.0, 7.0),
                    TimelineSceneGlyphShape::Circle,
                    theme.muriMarker);
                if (!state.muriDots.isEmpty()) {
                    TimelineSceneGlyph& glyph = state.muriDots.last();
                    glyph.locationId = locationId;
                    glyph.tooltipText = !markerPlacement.tooltipText.isEmpty()
                        ? markerPlacement.tooltipText
                        : request.muriMarkerTooltips.value(locationId);
                }
            }
        }
    };

    for (int refIndex = visibleNoteRefs.size() - 1; refIndex >= 0; --refIndex) {
        appendNoteForRef(visibleNoteRefs.at(refIndex), true);
    }
    for (const TimelineHeadLayerNoteRef& ref : holdTapHeadRefs) {
        appendNoteForRef(ref.ref, false);
    }
    for (const TimelineVisibleNoteRef& visibleRef : visibleNoteRefs) {
        const TimelineRenderLine& line = request.snapshot.lines.at(visibleRef.lineIndex);
        if (line.notes.at(visibleRef.noteIndex).kind == TimelineRenderNoteKind::Touch) {
            appendNoteForRef(visibleRef, false);
        }
    }
    for (const TimelineVisibleNoteRef& visibleRef : visibleNoteRefs) {
        const TimelineRenderLine& line = request.snapshot.lines.at(visibleRef.lineIndex);
        if (line.notes.at(visibleRef.noteIndex).kind == TimelineRenderNoteKind::TouchHold) {
            appendNoteForRef(visibleRef, false);
        }
    }

    const int entryX = secondToSceneX(state, request.playbackEntrySeconds);
    if (entryX > state.timelineLeft) {
        state.hasEntryMarker = true;
        const qreal tipY = static_cast<qreal>(state.timelineTop) - kTimelineTopMarkerTipOffsetPx;
        const qreal baseY = qMax<qreal>(0.0, tipY - kTimelinePlaybackEntryMarkerHeightPx);
        state.entryMarker = TimelineSceneTriangle{
            QPointF(entryX, tipY),
            QPointF(entryX - kTimelinePlaybackEntryMarkerHalfWidthPx, baseY),
            QPointF(entryX + kTimelinePlaybackEntryMarkerHalfWidthPx, baseY),
            theme.entryMarker,
        };
    }

    const int cursorX = secondToSceneX(state, request.cursorSeconds);
    if (cursorX > state.timelineLeft) {
        state.hasCursorLine = true;
        state.cursorLine = TimelineSceneLine{
            QPointF(cursorX, state.timelineTop),
            QPointF(cursorX, state.timelineTop + state.timelineHeight),
            theme.cursor,
            2.0,
        };
    }
    const int playheadX = secondToSceneX(state, request.playheadSeconds);
    if (!request.playheadIndicatorSuppressed && playheadX > state.timelineLeft) {
        state.hasPlayheadLine = true;
        state.playheadLine = TimelineSceneLine{
            QPointF(playheadX, state.timelineTop),
            QPointF(playheadX, state.timelineTop + state.timelineHeight),
            theme.playhead,
            2.0,
        };
    }
    if (request.dragActive) {
        const int dragCenterX = request.viewportSize.width() / 2;
        if (dragCenterX > state.timelineLeft) {
            state.hasDragCenterLine = true;
            state.dragCenterLine = TimelineSceneLine{
                QPointF(dragCenterX, state.timelineTop),
                QPointF(dragCenterX, state.timelineTop + state.timelineHeight),
                theme.playhead,
                2.0,
            };
        }
    }

    // Phase 9d-native — emit header control visuals (zoom button +
    // follow checkbox). Mirrors the QML ToolButton + CheckBox in
    // TimelineTabSurface.qml: zoom on the left at parent.left + 4,
    // follow on the right at parent.right - 8. Vertically centred in
    // the header band (y in [0, timelineTop)). Sized to roughly
    // match the QML controls.
    if (state.timelineTop > 4 && request.viewportSize.width() > 0) {
        state.hasHeaderControls = true;
        // Phase 9d-native polish — use the application's default UI
        // font (matches the menu-bar font, including Chinese fallback
        // chain) at the same pixel size and weight the QML controls use.
        QFont controlFont;
        controlFont.setPixelSize(12);
        controlFont.setWeight(QFont::DemiBold);
        const QFontMetricsF controlMetrics(controlFont);
        const int btnHeight = 22;
        const int btnY = qMax(0, (state.timelineTop - btnHeight) / 2);
        const QColor cardBg = theme.window.lightnessF() < 0.5
            ? QColor(31, 41, 55) : QColor(243, 244, 246);
        const QColor borderColor = theme.border;

        // ---- Zoom button (left) ----
        const QString zoomText = QStringLiteral("%1%").arg(
            qRound(request.zoomScale * 100.0));
        const qreal zoomTextW = controlMetrics.horizontalAdvance(zoomText);
        // Padding: 12 left for glyph slot, 8 right
        const int zoomBtnW = qRound(zoomTextW + 12 + 8);
        const int zoomBtnX = 4;
        state.zoomButtonBg = TimelineSceneRect{
            QRectF(zoomBtnX, btnY, zoomBtnW, btnHeight),
            cardBg,
        };
        state.zoomButtonBorder = TimelineSceneRect{
            // Drawn as a thin frame via 4 hairlines below; we keep one
            // descriptor for "the border colour" so the source can pick
            // it up. Width/height encode line thickness via
            // a sentinel: we use the rect itself plus 1-px hairlines
            // emitted by the source.
            QRectF(zoomBtnX, btnY, zoomBtnW, btnHeight),
            borderColor,
        };
        TimelineSceneTextLabel zoomLabel;
        zoomLabel.text = zoomText;
        zoomLabel.font = controlFont;
        zoomLabel.color = theme.label;
        zoomLabel.logicalSize = timelineTextLogicalSize(controlFont, zoomText);
        zoomLabel.topLeft = QPointF(
            zoomBtnX + 12 - kTimelineTextHorizontalPadding,
            btnY + (btnHeight - controlMetrics.height()) * 0.5
                - kTimelineTextVerticalPadding);
        state.zoomButtonLabel = zoomLabel;

        // ---- Follow checks (right) ----
        const QString followText = request.isChineseUi
            ? QStringLiteral("\u5149\u6807\u8ddf\u968f")
            : QStringLiteral("Cursor Follow");
        const QString progressFollowText = request.isChineseUi
            ? QStringLiteral("\u8fdb\u5ea6\u8ddf\u968f")
            : QStringLiteral("Progress Follow");
        const qreal followTextW = controlMetrics.horizontalAdvance(followText);
        const qreal progressFollowTextW = controlMetrics.horizontalAdvance(progressFollowText);
        const int followBoxSize = 14;
        const int followBoxToTextGap = 6;
        const int followTextRightPad = 4;  // air to the right of text
        const int followCheckGap = 10;
        const int followTotalW = qRound(followBoxSize + followBoxToTextGap
                                        + followTextW + followTextRightPad);
        const int progressFollowTotalW = qRound(followBoxSize + followBoxToTextGap
                                                + progressFollowTextW + followTextRightPad);
        const int progressFollowX = request.viewportSize.width() - 8 - progressFollowTotalW;
        const int followX = progressFollowX - followCheckGap - followTotalW;
        const int followBoxY = btnY + (btnHeight - followBoxSize) / 2;
        const QColor accent = theme.cursor;
        // Phase 9d-native polish — opaque backdrop covering indicator
        // + gap + text. Same colour as the header band so it visually
        // blends, but blocks line markers / waveform content from
        // showing through the text glyph gaps. No border (per user's
        // request). Pushed before the indicator in the source so the
        // checkbox box paints on top.
        QColor followBgColor = theme.header;
        followBgColor.setAlpha(255);
        state.followCheckBg = TimelineSceneRect{
            QRectF(followX, btnY, followTotalW, btnHeight),
            followBgColor,
        };
        state.followCheckIndicator = TimelineSceneRect{
            QRectF(followX, followBoxY, followBoxSize, followBoxSize),
            request.followPreviewEnabled ? accent : cardBg,
        };
        state.followCheckIndicatorBorder = TimelineSceneRect{
            QRectF(followX, followBoxY, followBoxSize, followBoxSize),
            request.followPreviewEnabled ? accent : borderColor,
        };
        state.followCheckChecked = request.followPreviewEnabled;
        if (request.followPreviewEnabled) {
            // Tick mark — diagonal hairline approximation. Drawn as a
            // small filled rect (cheap; readable at 14×14).
            const qreal tickX = followX + 3.0;
            const qreal tickY = followBoxY + followBoxSize * 0.55;
            state.followCheckMark = TimelineSceneRect{
                QRectF(tickX, tickY, 8.0, 2.0),
                QColor(255, 255, 255),
            };
        }
        TimelineSceneTextLabel followLabel;
        followLabel.text = followText;
        followLabel.font = controlFont;
        followLabel.color = theme.label;
        followLabel.logicalSize = timelineTextLogicalSize(controlFont, followText);
        followLabel.topLeft = QPointF(
            followX + followBoxSize + followBoxToTextGap
                - kTimelineTextHorizontalPadding,
            btnY + (btnHeight - controlMetrics.height()) * 0.5
                - kTimelineTextVerticalPadding);
        state.followCheckLabel = followLabel;
        state.progressFollowCheckBg = TimelineSceneRect{
            QRectF(progressFollowX, btnY, progressFollowTotalW, btnHeight),
            followBgColor,
        };
        state.progressFollowCheckIndicator = TimelineSceneRect{
            QRectF(progressFollowX, followBoxY, followBoxSize, followBoxSize),
            request.followProgressEnabled ? accent : cardBg,
        };
        state.progressFollowCheckIndicatorBorder = TimelineSceneRect{
            QRectF(progressFollowX, followBoxY, followBoxSize, followBoxSize),
            request.followProgressEnabled ? accent : borderColor,
        };
        state.progressFollowCheckChecked = request.followProgressEnabled;
        TimelineSceneTextLabel progressFollowLabel;
        progressFollowLabel.text = progressFollowText;
        progressFollowLabel.font = controlFont;
        progressFollowLabel.color = theme.label;
        progressFollowLabel.logicalSize = timelineTextLogicalSize(controlFont, progressFollowText);
        progressFollowLabel.topLeft = QPointF(
            progressFollowX + followBoxSize + followBoxToTextGap
                - kTimelineTextHorizontalPadding,
            btnY + (btnHeight - controlMetrics.height()) * 0.5
                - kTimelineTextVerticalPadding);
        state.progressFollowCheckLabel = progressFollowLabel;
    }

    return state;
}

int TimelineSceneStateBuilder::secondToSceneX(const TimelineSceneState& state, double second)
{
    return ::secondToX(state, second);
}

double TimelineSceneStateBuilder::sceneXToSecond(const TimelineSceneState& state, qreal x)
{
    return ::xToSecond(state, x);
}

}  // namespace miacode::timeline
