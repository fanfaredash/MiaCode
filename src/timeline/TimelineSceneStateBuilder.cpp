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

int secondToX(const TimelineSceneState& state, double second)
{
    return rawSecondToX(second, state.displayStartSeconds, state.pixelsPerSecond) + state.leadingCenteringPadding;
}

qreal secondToXExact(const TimelineSceneState& state, double second)
{
    return rawSecondToXExact(second, state.displayStartSeconds, state.pixelsPerSecond)
        + static_cast<qreal>(state.leadingCenteringPadding);
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

}  // namespace

namespace miacode::timeline {

TimelineSceneState TimelineSceneStateBuilder::build(const TimelineSceneBuildRequest& request)
{
    TimelineSceneState state;
    state.viewportSize = request.viewportSize;
    state.timelineLeft = kTimelineLeftMargin;
    state.timelineTop = kHeaderHeight + kTimelineTopMargin;
    state.timelineHeight = kLaneCount * kLaneHeight;
    state.laneHeight = kLaneHeight;
    state.laneCount = kLaneCount;
    state.horizontalScrollValue = qMax(0, request.horizontalScrollValue);
    state.pixelsPerSecond = pixelsPerSecondForZoom(request.zoomScale);
    state.maxNavigableSecond = maxNavigableSecond(request);
    state.displayStartSeconds =
        qMin(-kTimelineDisplayLeadInSeconds, request.snapshot.minimumSecond - kTimelineDisplayLeadInSeconds);
    state.displayEndSeconds = qMax(state.displayStartSeconds + 1.0, state.maxNavigableSecond + 1.0);
    state.leadingCenteringPadding = qMax(
        0,
        (request.viewportSize.width() / 2) - rawSecondToX(0.0, state.displayStartSeconds, state.pixelsPerSecond));
    state.trailingCenteringPadding = qMax(
        0,
        rawSecondToX(state.maxNavigableSecond, state.displayStartSeconds, state.pixelsPerSecond)
                + (request.viewportSize.width() / 2)
            - rawContentWidth(state.displayStartSeconds, state.displayEndSeconds, state.pixelsPerSecond));
    state.contentWidth =
        rawContentWidth(state.displayStartSeconds, state.displayEndSeconds, state.pixelsPerSecond)
        + state.leadingCenteringPadding + state.trailingCenteringPadding;
    state.visibleStartSecond = xToSecond(state, state.timelineLeft);
    state.visibleEndSecond = xToSecond(state, request.viewportSize.width());
    state.appearanceRevision = request.appearanceRevision;
    state.gridRevision = request.gridRevision;
    state.waveformRevision = request.waveformRevision;
    state.headerRevision = request.headerRevision;
    state.notesRevision = request.notesRevision;
    state.overlayRevision = request.overlayRevision;

    const TimelineThemeColors theme = timelineThemeColors();
    const QFont laneLabelFont = timelineLaneLabelFont();
    state.baseBackgroundRects.append(TimelineSceneRect{
        QRectF(0.0, 0.0, request.viewportSize.width(), request.viewportSize.height()),
        theme.window,
    });
    state.baseBackgroundRects.append(TimelineSceneRect{
        QRectF(0.0, 0.0, request.viewportSize.width(), state.timelineTop),
        theme.header,
    });
    state.baseBackgroundRects.append(TimelineSceneRect{
        QRectF(0.0, state.timelineTop, state.timelineLeft, state.timelineHeight),
        theme.sidebar,
    });
    state.baseBackgroundRects.append(TimelineSceneRect{
        QRectF(state.timelineLeft, state.timelineTop, request.viewportSize.width() - state.timelineLeft, state.timelineHeight),
        theme.base,
    });
    state.frameRects.append(TimelineSceneRect{
        QRectF(0.0, state.timelineTop - 1.0, state.timelineLeft + 1.0, state.timelineHeight + 2.0),
        theme.sidebar,
    });
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
            const QPair<int, int> visibleColumns =
                miacode::waveform::visibleWaveformColumnRange(
                    *waveformLevel,
                    qMax(0.0, state.visibleStartSecond),
                    qMax(0.0, state.visibleEndSecond));
            const qreal centerY = state.timelineTop + state.timelineHeight / 2.0;
            const qreal maxAmplitude = (qMax<qreal>(8.0, state.timelineHeight / 2.0 - 8.0) * 7.0) / 9.0;
            for (int index = visibleColumns.first; index < visibleColumns.second; ++index) {
                const miacode::waveform::WaveformColumn& column = waveformLevel->columns.at(index);
                if (qAbs(column.max - column.min) <= 1e-5f) {
                    continue;
                }
                const double columnStartSecond = waveformLevel->secondsPerColumn * static_cast<double>(index);
                const double columnEndSecond = columnStartSecond + waveformLevel->secondsPerColumn;
                int x0 = secondToSceneX(state, columnStartSecond) - state.horizontalScrollValue;
                int x1 = secondToSceneX(state, columnEndSecond) - state.horizontalScrollValue;
                if (x1 <= x0) {
                    x1 = x0 + 1;
                }
                if (x1 < state.timelineLeft || x0 > request.viewportSize.width()) {
                    continue;
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

    const auto addGridLine = [&](double absoluteSecond, const QColor& color, qreal width, bool exactPosition) {
        const qreal x = (exactPosition ? secondToXExact(state, absoluteSecond)
                                       : static_cast<qreal>(secondToSceneX(state, absoluteSecond)))
            - static_cast<qreal>(state.horizontalScrollValue);
        if (x < state.timelineLeft - 1 || x > request.viewportSize.width()) {
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
    TimelineVisibleLineRange noteRange;
    if (!request.snapshot.lines.isEmpty()) {
        const auto beginIt = std::lower_bound(
            request.snapshot.lines.cbegin(),
            request.snapshot.lines.cend(),
            state.visibleStartSecond - 1.0,
            [](const TimelineRenderLine& line, double targetSecond) {
                return line.startSecond < targetSecond;
            });
        const auto endIt = std::upper_bound(
            request.snapshot.lines.cbegin(),
            request.snapshot.lines.cend(),
            state.visibleEndSecond + 1.0,
            [](double targetSecond, const TimelineRenderLine& line) {
                return targetSecond < line.startSecond;
            });
        beatRange.begin = static_cast<int>(std::distance(request.snapshot.lines.cbegin(), beginIt));
        beatRange.end = static_cast<int>(std::distance(request.snapshot.lines.cbegin(), endIt));

        const QVector<double>& noteVisualPrefixMax = request.showSlideTracks
            ? request.snapshot.noteVisualEndPrefixMaxWithSlideTracks
            : request.snapshot.noteVisualEndPrefixMaxWithoutSlideTracks;
        noteRange = noteVisualPrefixMax.size() == request.snapshot.lines.size()
            ? timelineRenderVisibleNoteLineRange(
                  request.snapshot.lines,
                  noteVisualPrefixMax,
                  state.visibleStartSecond - 2.0,
                  state.visibleEndSecond + 2.0)
            : beatRange;
    }

    for (int lineIndex = beatRange.begin; lineIndex < beatRange.end; ++lineIndex) {
        const TimelineRenderLine& line = request.snapshot.lines.at(lineIndex);
        for (const TimelineRenderBeat& marker : line.beats) {
            if (shouldPaintTimelineBeatMarker(marker)) {
                addGridLine(timelineRenderAbsoluteSecond(line, marker.secondOffset), theme.gridMinor, 1.0, true);
            }
        }
    }
    for (double measureSecond : request.snapshot.measureLineSeconds) {
        if (measureSecond >= state.visibleStartSecond - 1.0 && measureSecond <= state.visibleEndSecond + 1.0) {
            addGridLine(measureSecond, theme.gridMajor, kTimelineBeatLineWidth, false);
        }
    }

    if (request.snapshot.trailingMeasureLineStepSeconds > 1e-6) {
        double extensionSecond = request.snapshot.trailingMeasureLineStartSecond;
        if (!request.snapshot.measureLineSeconds.isEmpty()
            && extensionSecond <= request.snapshot.measureLineSeconds.constLast() + 1e-6) {
            const double delta = request.snapshot.measureLineSeconds.constLast() - extensionSecond;
            extensionSecond += request.snapshot.trailingMeasureLineStepSeconds
                * static_cast<double>(qMax<qint64>(1, static_cast<qint64>(qFloor(delta / request.snapshot.trailingMeasureLineStepSeconds)) + 1));
        }
        if (extensionSecond <= state.visibleStartSecond - 1.0 + 1e-6) {
            const double delta = (state.visibleStartSecond - 1.0) - extensionSecond;
            extensionSecond += request.snapshot.trailingMeasureLineStepSeconds
                * static_cast<double>(qMax<qint64>(1, static_cast<qint64>(qFloor(delta / request.snapshot.trailingMeasureLineStepSeconds)) + 1));
        }
        for (; extensionSecond <= state.visibleEndSecond + 1.0 + 1e-6;
             extensionSecond += request.snapshot.trailingMeasureLineStepSeconds) {
            addGridLine(extensionSecond, theme.gridMajor, kTimelineBeatLineWidth, false);
        }
    }

    QVector<HeaderLineLabel> headerLabels;
    if (!request.snapshot.lines.isEmpty()) {
        const auto beginIt = std::lower_bound(
            request.snapshot.lines.cbegin(),
            request.snapshot.lines.cend(),
            state.visibleStartSecond - kTimelineHeaderLineAnchorToleranceSeconds,
            [](const TimelineRenderLine& line, double targetSecond) {
                return line.startSecond < targetSecond;
            });
        const auto endIt = std::upper_bound(
            request.snapshot.lines.cbegin(),
            request.snapshot.lines.cend(),
            state.visibleEndSecond + kTimelineHeaderLineAnchorToleranceSeconds,
            [](double targetSecond, const TimelineRenderLine& line) {
                return targetSecond < line.startSecond;
            });

        QVector<HeaderLineLabel> collapsed;
        for (auto it = beginIt; it != endIt; ++it) {
            const TimelineRenderLine& line = *it;
            const int screenX = secondToSceneX(state, line.startSecond) - state.horizontalScrollValue;
            if (screenX < state.timelineLeft - 1 || screenX > request.viewportSize.width()) {
                continue;
            }
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
            const QString labelText = QString::number(label.lineNumber);
            const int labelHalfWidth = timelineHeaderLabelHalfWidthPx(request.headerLineNumberFont, labelText);
            if (label.screenX - labelHalfWidth < request.headerLeftLimit
                || label.screenX + labelHalfWidth > request.headerRightLimit) {
                continue;
            }
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

    const TimelineVisibleLineRange visibleNoteRange{noteRange.begin, noteRange.end};
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
        const bool isHold = note.kind == TimelineRenderNoteKind::Hold && endSecond >= startSecond;
        const bool isTouchHold = note.kind == TimelineRenderNoteKind::TouchHold && endSecond > startSecond;
        const bool isSlideLike = note.kind == TimelineRenderNoteKind::Slide || note.kind == TimelineRenderNoteKind::Wifi;
        const bool isSlideTrack = isSlideLike && traceSecond > startSecond && endSecond > traceSecond;

        const int x = secondToSceneX(state, startSecond) - state.horizontalScrollValue;
        const int holdEndX = isHold ? (secondToSceneX(state, endSecond) - state.horizontalScrollValue) : x;
        const int slideStartX = isSlideTrack ? (secondToSceneX(state, traceSecond) - state.horizontalScrollValue) : x;
        const int slideEndX = isSlideTrack ? (secondToSceneX(state, endSecond) - state.horizontalScrollValue) : x;
        int extentLeft = x;
        int extentRight = x;
        if (isHold || isTouchHold) {
            const int endX = secondToSceneX(state, endSecond) - state.horizontalScrollValue;
            extentLeft = qMin(extentLeft, endX);
            extentRight = qMax(extentRight, endX);
        }
        if (request.showSlideTracks && isSlideTrack) {
            extentLeft = qMin(extentLeft, qMin(slideStartX, slideEndX));
            extentRight = qMax(extentRight, qMax(slideStartX, slideEndX));
        }
        if (extentRight < state.timelineLeft - kNoteSize || extentLeft > request.viewportSize.width() + kNoteSize) {
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
                QPointF(secondToSceneX(state, endSecond) - state.horizontalScrollValue, rowCenterY),
                touchHoldColor,
                4.0);
        }
        if (timelineRenderFlagSet(note, TimelineRenderFlagIsFirework)
            && (note.kind == TimelineRenderNoteKind::Touch || note.kind == TimelineRenderNoteKind::TouchHold)) {
            const double triggerSecond =
                (note.kind == TimelineRenderNoteKind::TouchHold && endSecond > startSecond) ? endSecond : startSecond;
            const int fireLeft = secondToSceneX(state, triggerSecond) - state.horizontalScrollValue;
            const int fireRight =
                secondToSceneX(state, triggerSecond + kTimelineFireworkDurationSeconds) - state.horizontalScrollValue;
            if (fireRight >= state.timelineLeft && fireLeft <= request.viewportSize.width()) {
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

        if (request.muriMarkerLocationIds.contains(timelineRenderLocationId(line, note))) {
            const quint64 locationId = timelineRenderLocationId(line, note);
            appendGlyph(
                &state.muriDots,
                QRectF(x + 4.0, rowCenterY - 8.0, 7.0, 7.0),
                TimelineSceneGlyphShape::Circle,
                theme.muriMarker);
            if (!state.muriDots.isEmpty()) {
                TimelineSceneGlyph& glyph = state.muriDots.last();
                glyph.locationId = locationId;
                glyph.tooltipText = request.muriMarkerTooltips.value(locationId);
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

    const int entryX = secondToSceneX(state, request.playbackEntrySeconds) - state.horizontalScrollValue;
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

    const int cursorX = secondToSceneX(state, request.cursorSeconds) - state.horizontalScrollValue;
    if (cursorX > state.timelineLeft) {
        state.hasCursorLine = true;
        state.cursorLine = TimelineSceneLine{
            QPointF(cursorX, state.timelineTop),
            QPointF(cursorX, state.timelineTop + state.timelineHeight),
            theme.cursor,
            2.0,
        };
    }
    const int playheadX = secondToSceneX(state, request.playheadSeconds) - state.horizontalScrollValue;
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
