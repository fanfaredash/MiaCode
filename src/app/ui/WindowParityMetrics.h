#pragma once

#include <QtGlobal>

namespace miacode::window_parity {

constexpr int kInitialWindowWidth = 1280;
constexpr int kInitialWindowHeight = 800;
constexpr int kInitialWindowFloorWidth = 960;
constexpr int kInitialWindowFloorHeight = 640;

constexpr int kOutlineCollapsedWidth = 20;
constexpr int kOutlineExpandedMinWidth = 120;
constexpr int kOutlineExpandedDefaultWidth = 190;
constexpr int kOutlineListPadding = 6;
constexpr int kOutlineRowHorizontalPadding = 12;
constexpr int kOutlineRowVerticalPadding = 4;
constexpr int kOutlineRowRadius = 6;
constexpr int kOutlineDeleteButtonSize = 18;
constexpr int kOutlineDeleteIconSize = 12;
constexpr int kOutlineFallbackRowHeight = 36;

constexpr int kToolbarLeadingSpacerWidth = 6;
constexpr int kToolbarActionButtonWidth = 64;
constexpr int kToolbarActionButtonHorizontalPadding = 20;

constexpr int kMetadataPageMarginLeft = 12;
constexpr int kMetadataPageMarginTop = 8;
constexpr int kMetadataPageMarginRight = 12;
constexpr int kMetadataPageMarginBottom = 12;
constexpr int kMetadataCardRadius = 8;
constexpr int kMetadataCardMarginHorizontal = 14;
constexpr int kMetadataCardMarginTop = 12;
constexpr int kMetadataCardMarginBottom = 14;
constexpr int kMetadataSectionSpacing = 12;
constexpr int kMetadataFormHorizontalSpacing = 8;
constexpr int kMetadataFormVerticalSpacing = 10;
constexpr int kFirstFieldWidth = 98;

constexpr int kEditorHeaderMarginX = 12;
constexpr int kEditorHeaderMarginY = 8;
constexpr int kEditorHeaderSpacing = 10;

constexpr int kEmbeddedPreviewPanelMinWidth = 320;
constexpr qreal kEmbeddedPreviewPanelWidthRatio = 0.50;
constexpr int kEmbeddedPreviewPanelWidthMax = 900;
constexpr int kPreviewPanelMarginX = 8;
constexpr int kPreviewPanelMarginTop = 12;
constexpr int kPreviewPanelMarginBottom = 12;
constexpr int kPreviewCanvasControlGap = 10;
constexpr int kPreviewStatsBottomGap = 12;
constexpr int kPreviewControlStatsGap = 10;
constexpr int kPreviewControlStatsCardMinWidth = 280;
constexpr int kPreviewControlButtonMinHeight = 28;
constexpr int kPreviewControlButtonPaddingX = 8;
constexpr int kPreviewControlButtonPaddingY = 5;
constexpr int kPreviewSpeedButtonWidth = 72;
constexpr int kPreviewControlCardRadius = 10;
constexpr int kPreviewStatsCardRadius = 10;
constexpr int kPreviewStatsChipRadius = 9;
constexpr int kPreviewStatsChipPaddingX = 8;
constexpr int kPreviewStatsChipPaddingY = 2;
constexpr int kPreviewStatsChipHeight = 30;
constexpr int kPreviewStatsHorizontalSpacing = 10;
constexpr int kPreviewStatsVerticalSpacing = 6;
constexpr int kPreviewStatsWideLayoutCols = 3;
constexpr int kPreviewStatsNarrowLayoutCols = 2;
constexpr int kPreviewStatsWideLayoutMinChipWidth = 118;
constexpr int kPreviewStatsGridMarginTop = 2;
constexpr int kPreviewStatsGridMarginBottom = 2;
constexpr int kTimelineHeaderHeight = 28;
constexpr int kTimelineTopMargin = 6;
constexpr int kTimelineLaneHeight = 20;
constexpr int kTimelineLaneCount = 9;
constexpr int kTimelineBottomPadding = 20;

constexpr int kPreviewFullscreenHintTopMargin = 28;
constexpr int kPreviewFullscreenOverlaySideMargin = 18;
constexpr int kPreviewFullscreenOverlayBottomMargin = 24;
constexpr int kPreviewFullscreenOverlayMaxWidth = 10000;
constexpr int kPreviewFullscreenOverlayHideOffset = 20;
constexpr int kPreviewFullscreenControlsRevealHotzoneHeight = 120;
constexpr int kPreviewFullscreenControlsAutoHideDelayMs = 1600;
constexpr int kPreviewFullscreenHintAutoHideDelayMs = 2200;

struct PreviewStatsLayout {
    int columns = kPreviewStatsWideLayoutCols;
    int rows = 2;
    int minCardHeight = 0;
    int wideLayoutThreshold = 0;
};

struct PreviewPanelLayout {
    int previewX = 0;
    int previewY = 0;
    int previewWidth = 0;
    int previewHeight = 0;
    int controlX = 0;
    int controlY = 0;
    int controlWidth = 0;
    int statsX = 0;
    int statsY = 0;
    int statsWidth = 0;
    int statsHeight = 0;
    int statsHostWidth = 0;
    int minimumStatsHeight = 0;
};

PreviewStatsLayout computePreviewStatsLayout(
    int hostWidth,
    int itemCount = 6,
    int horizontalSpacing = kPreviewStatsHorizontalSpacing,
    int verticalSpacing = kPreviewStatsVerticalSpacing,
    int chipHeight = kPreviewStatsChipHeight,
    int gridMarginTop = kPreviewStatsGridMarginTop,
    int gridMarginBottom = kPreviewStatsGridMarginBottom
);

int computePreviewPanelTargetWidth(
    int availableWidth,
    int availableHeight,
    int leftMinWidth,
    int controlHeight,
    int minimumStatsHeight,
    double aspectRatio
);

int computePreviewPanelTargetWidthForAdaptiveStats(
    int availableWidth,
    int availableHeight,
    int leftMinWidth,
    int controlHeight,
    double aspectRatio
);

PreviewPanelLayout computePreviewPanelLayout(
    int panelWidth,
    int panelHeight,
    int controlHeight,
    double aspectRatio
);

int computeTimelineMinimumContentHeight();
int computeBottomTabsDeviceHeight(int timelineHeight, int tabBarHeight, int frameWidth);

}  // namespace miacode::window_parity
