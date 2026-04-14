#include "WindowParityMetrics.h"

#include <QtMath>

namespace miacode::window_parity {

PreviewStatsLayout computePreviewStatsLayout(
    int hostWidth,
    int itemCount,
    int horizontalSpacing,
    int verticalSpacing,
    int chipHeight,
    int gridMarginTop,
    int gridMarginBottom
)
{
    PreviewStatsLayout layout;
    const int resolvedItemCount = qMax(1, itemCount);
    layout.wideLayoutThreshold = kPreviewStatsWideLayoutCols * kPreviewStatsWideLayoutMinChipWidth
        + qMax(0, kPreviewStatsWideLayoutCols - 1) * qMax(0, horizontalSpacing);
    const bool useWideLayout = hostWidth >= layout.wideLayoutThreshold;
    layout.columns = qMin(
        resolvedItemCount,
        useWideLayout ? kPreviewStatsWideLayoutCols : kPreviewStatsNarrowLayoutCols
    );
    layout.rows = qMax(1, (resolvedItemCount + layout.columns - 1) / layout.columns);
    layout.minCardHeight = 16
        + qMax(0, gridMarginTop)
        + qMax(0, gridMarginBottom)
        + layout.rows * qMax(0, chipHeight)
        + qMax(0, layout.rows - 1) * qMax(0, verticalSpacing);
    return layout;
}

int computePreviewPanelTargetWidth(
    int availableWidth,
    int availableHeight,
    int leftMinWidth,
    int controlHeight,
    int minimumStatsHeight,
    double aspectRatio
)
{
    const int resolvedAvailableWidth = qMax(0, availableWidth);
    const int resolvedLeftMinWidth = qMax(0, leftMinWidth);
    const int minimumRightWidth =
        (resolvedAvailableWidth >= resolvedLeftMinWidth + kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2)
        ? (kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2)
        : qMin(resolvedAvailableWidth, kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2);
    const int rightMaxWidth =
        (resolvedAvailableWidth >= resolvedLeftMinWidth + minimumRightWidth)
        ? qMin(kEmbeddedPreviewPanelWidthMax, resolvedAvailableWidth - resolvedLeftMinWidth)
        : resolvedAvailableWidth;
    const int preferredRightMaxWidth = qMax(minimumRightWidth, rightMaxWidth);
    int preferredRightWidth = qRound(static_cast<qreal>(resolvedAvailableWidth) * kEmbeddedPreviewPanelWidthRatio);
    preferredRightWidth = qMin(preferredRightWidth, preferredRightMaxWidth);
    preferredRightWidth = qMax(preferredRightWidth, qMin(kEmbeddedPreviewPanelMinWidth, preferredRightMaxWidth));

    int targetRightWidth = preferredRightWidth;
    const double safeAspectRatio = aspectRatio > 0.001 ? aspectRatio : 1.0;
    for (int iteration = 0; iteration < 3; ++iteration) {
        const int panelContentWidth = qMax(0, targetRightWidth - kPreviewPanelMarginX * 2);
        const int availablePreviewHeight = qMax(
            0,
            qMax(0, availableHeight)
                - kPreviewPanelMarginTop
                - kPreviewCanvasControlGap
                - qMax(0, controlHeight)
                - kPreviewControlStatsGap
                - qMax(0, minimumStatsHeight)
                - kPreviewStatsBottomGap
        );
        const int heightLimitedWidth = qMax(
            0,
            qRound(static_cast<qreal>(availablePreviewHeight) * safeAspectRatio) + kPreviewPanelMarginX * 2
        );
        const int nextRightWidth = qMin(targetRightWidth, qMax(minimumRightWidth, heightLimitedWidth));
        if (nextRightWidth == targetRightWidth) {
            break;
        }
        Q_UNUSED(panelContentWidth);
        targetRightWidth = nextRightWidth;
    }

    return qBound(minimumRightWidth, targetRightWidth, rightMaxWidth);
}

int computePreviewPanelTargetWidthForAdaptiveStats(
    int availableWidth,
    int availableHeight,
    int leftMinWidth,
    int controlHeight,
    double aspectRatio
)
{
    const int resolvedAvailableWidth = qMax(0, availableWidth);
    const int resolvedLeftMinWidth = qMax(0, leftMinWidth);
    const int minimumRightWidth =
        (resolvedAvailableWidth >= resolvedLeftMinWidth + kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2)
        ? (kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2)
        : qMin(resolvedAvailableWidth, kPreviewControlStatsCardMinWidth + kPreviewPanelMarginX * 2);
    const int rightMaxWidth =
        (resolvedAvailableWidth >= resolvedLeftMinWidth + minimumRightWidth)
        ? qMin(kEmbeddedPreviewPanelWidthMax, resolvedAvailableWidth - resolvedLeftMinWidth)
        : resolvedAvailableWidth;
    if (rightMaxWidth <= 0) {
        return 0;
    }

    const auto minimumStatsHeightForRightWidth = [](int rightWidth) {
        const int statsHostWidth = qMax(0, rightWidth - kPreviewPanelMarginX * 2 - 16);
        return computePreviewStatsLayout(statsHostWidth).minCardHeight;
    };

    int targetRightWidth = qBound(
        minimumRightWidth,
        computePreviewPanelTargetWidth(
            resolvedAvailableWidth,
            qMax(0, availableHeight),
            resolvedLeftMinWidth,
            qMax(0, controlHeight),
            minimumStatsHeightForRightWidth(rightMaxWidth),
            aspectRatio
        ),
        rightMaxWidth
    );
    for (int iteration = 0; iteration < 4; ++iteration) {
        const int nextRightWidth = qBound(
            minimumRightWidth,
            computePreviewPanelTargetWidth(
                resolvedAvailableWidth,
                qMax(0, availableHeight),
                resolvedLeftMinWidth,
                qMax(0, controlHeight),
                minimumStatsHeightForRightWidth(targetRightWidth),
                aspectRatio
            ),
            rightMaxWidth
        );
        if (nextRightWidth == targetRightWidth) {
            break;
        }
        targetRightWidth = nextRightWidth;
    }

    return targetRightWidth;
}

PreviewPanelLayout computePreviewPanelLayout(
    int panelWidth,
    int panelHeight,
    int controlHeight,
    double aspectRatio
)
{
    PreviewPanelLayout layout;
    const int contentX = kPreviewPanelMarginX;
    const int contentY = kPreviewPanelMarginTop;
    const int contentWidth = qMax(0, panelWidth - kPreviewPanelMarginX * 2);
    const int statsHostWidth = qMax(0, contentWidth - 16);
    const PreviewStatsLayout statsLayout = computePreviewStatsLayout(statsHostWidth);
    const int availablePreviewHeight = qMax(
        0,
        qMax(0, panelHeight)
            - kPreviewPanelMarginTop
            - kPreviewCanvasControlGap
            - qMax(0, controlHeight)
            - kPreviewControlStatsGap
            - statsLayout.minCardHeight
            - kPreviewStatsBottomGap
    );
    const double safeAspectRatio = aspectRatio > 0.001 ? aspectRatio : 1.0;
    const int previewWidth = qMax(
        1,
        qMin(contentWidth, qRound(static_cast<qreal>(availablePreviewHeight) * safeAspectRatio))
    );
    const int previewHeight = qMax(1, qRound(static_cast<qreal>(previewWidth) / safeAspectRatio));
    const int previewX = contentX + qMax(0, (contentWidth - previewWidth) / 2);
    const int controlY = contentY + previewHeight + kPreviewCanvasControlGap;
    const int statsAreaY = controlY + qMax(0, controlHeight) + kPreviewControlStatsGap;
    const int statsAreaHeight = qMax(
        0,
        qMax(0, panelHeight) - statsAreaY - kPreviewStatsBottomGap
    );
    const int statsHeight = qMin(statsLayout.minCardHeight, statsAreaHeight);
    const int statsY = statsAreaY + qMax(0, (statsAreaHeight - statsHeight) / 2);

    layout.previewX = previewX;
    layout.previewY = contentY;
    layout.previewWidth = previewWidth;
    layout.previewHeight = previewHeight;
    layout.controlX = contentX;
    layout.controlY = controlY;
    layout.controlWidth = contentWidth;
    layout.statsX = contentX;
    layout.statsY = statsY;
    layout.statsWidth = contentWidth;
    layout.statsHeight = statsHeight;
    layout.statsHostWidth = statsHostWidth;
    layout.minimumStatsHeight = statsLayout.minCardHeight;
    return layout;
}

int computeBottomTabsDeviceHeight(int timelineHeight, int tabBarHeight, int frameWidth)
{
    return qMax(0, timelineHeight)
        + qMax(0, tabBarHeight)
        + qMax(0, frameWidth) * 2;
}

}  // namespace miacode::window_parity
