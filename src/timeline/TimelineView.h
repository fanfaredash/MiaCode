#pragma once

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QElapsedTimer>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QSet>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVector>
#include <QEvent>

#include <memory>

#include "timeline/TimelineRenderData.h"
#include "common/MuriTypes.h"

namespace miacode::waveform {
struct WaveformData;
}

class TimelineView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TimelineView(QWidget* parent = nullptr);
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;
    void setHeaderLineNumberFont(const QFont& font);
    void setTimelineData(const TimelineRenderSnapshot& snapshot);
    void setWaveformData(const std::shared_ptr<const miacode::waveform::WaveformData>& waveformData);
    void clear();
    void setPlaybackEntrySeconds(double second);
    double playbackEntrySeconds() const;
    void setPlayheadUpperLimitSeconds(double second);
    void setPlayheadSeconds(double second, bool centerView);
    void setCursorSeconds(double second, bool centerView = false);
    void focusPlayhead(bool centerView = false);
    void focusCursor(bool centerView = false);
    double playheadSeconds() const;
    double cursorSeconds() const;
    double durationSeconds() const;
    void setShowSlideTracks(bool show);
    bool showSlideTracks() const;
    void setMuriAnalysisReport(const MuriAnalysisReport& report);
    double zoomScale() const;
    void setFollowPreviewEnabled(bool enabled);
    bool followPreviewEnabled() const;
    void refreshTheme();

signals:
    void playheadChanged(double second);
    void headerNavigateRequested(double second);
    void centerNavigateRequested(double second);
    void timelineDragStarted();
    void timelineDragFinished(double second);
    void timelineUserInteractionStarted();
    void followPreviewToggled(bool enabled);
    void previewPlayPauseRequested();

protected:
    bool viewportEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void scrollContentsBy(int dx, int dy) override;

private:
    struct VisibleLineRange {
        int begin = 0;
        int end = 0;
    };

    struct HeaderLineLabel {
        int lineNumber = 1;
        double second = 0.0;
        int screenX = 0;
    };

    struct HoldPixmapParts {
        QPixmap leftCap;
        QPixmap rightCap;
        QImage bodySlice;
    };

    enum class FocusTarget {
        Playhead,
        Cursor,
    };

    void updateDisplayBounds();
    void updateHorizontalRange();
    int contentWidth() const;
    int rawContentWidth() const;
    int timelineLeft() const;
    int timelineTop() const;
    int laneHeight() const;
    int timelineHeight() const;
    int notePixelSize() const;
    int rawSecondToX(double second) const;
    int secondToX(double second) const;
    double xToSecond(int x) const;
    double maxNavigableSecond() const;
    int leadingCenteringPadding() const;
    int trailingCenteringPadding() const;
    double viewportCenterSecond() const;
    void updatePlayheadToViewportCenter(bool emitNavigate = true);
    bool playheadNearViewportCenter() const;
    void suppressPlayheadIndicatorForInteraction();
    void restorePlayheadIndicatorAfterInteraction(bool immediate = false);
    bool effectiveShowSlideTracks() const;
    void cycleZoomPreset();
    void applyZoomPresetIndex(int nextIndex, double anchorSecond);
    void stepZoomPreset(int deltaSteps, double anchorSecond);
    bool handleAltZoomWheel(QWheelEvent* event);
    void updateZoomButtonAppearance();
    void layoutHeaderButtons();
    QVector<HeaderLineLabel> visibleHeaderLineLabels(
        double startSecond,
        double endSecond,
        int xOffset,
        int headerLeftLimit,
        int headerRightLimit) const;
    VisibleLineRange visibleLineRange(double startSecond, double endSecond) const;
    void updateTimelineMarkerStrip(double oldSecond, double newSecond, int halfWidth);
    const QPixmap& iconForType(const QString& type) const;
    int iconBasePixelSizeForType(const QString& type) const;
    QSize targetSizeForIconType(const QString& type, qreal scale = 1.0) const;
    const QPixmap& transformedIconForType(
        const QString& type,
        qreal scale = 1.0,
        qreal rotationDegrees = 0.0,
        bool mirrorX = false) const;
    qreal holdScaleForBaseIconScale(const QString& type, qreal baseIconScale) const;
    const HoldPixmapParts& holdPixmapPartsForType(const QString& type, qreal scale) const;
    void loadNoteIcons();
    void prewarmTransformedIconCache();
    int minimumContentHeightForCurrentDevice() const;
    void refreshMinimumHeightForCurrentDevice();
    bool stepViewportBySeconds(double deltaSeconds);
    void beginHeldHorizontalKeyScroll(int direction, int key);
    void stopHeldHorizontalKeyScroll(int key = 0);
    void applyHeldHorizontalKeyScrollTick();

    QVector<TimelineRenderLine> lines_;
    QVector<double> measureLineSeconds_;
    QVector<double> noteVisualEndPrefixMaxWithSlideTracks_;
    QVector<double> noteVisualEndPrefixMaxWithoutSlideTracks_;
    double trailingMeasureLineStartSecond_ = 0.0;
    double trailingMeasureLineStepSeconds_ = 0.0;
    double durationSeconds_ = 0.0;
    double playbackEntrySeconds_ = 0.0;
    double playheadSeconds_ = 0.0;
    double cursorSeconds_ = 0.0;
    double playheadUpperLimitSeconds_ = -1.0;
    double minimumDataSecond_ = 0.0;
    double maximumDataSecond_ = 0.0;
    double displayStartSeconds_ = -0.5;
    double displayEndSeconds_ = 1.0;
    double pixelsPerSecond_ = 120.0;
    bool showSlideTracks_ = true;
    QSet<quint64> muriMarkerLocationIds_;
    QHash<QString, QPixmap> noteIcons_;
    QHash<QString, int> noteIconBasePixelSizes_;
    mutable QHash<QString, QPixmap> transformedIconCache_;
    mutable QHash<QString, HoldPixmapParts> holdPixmapPartsCache_;
    std::shared_ptr<const miacode::waveform::WaveformData> waveformData_;
    QToolButton* zoomButton_ = nullptr;
    QCheckBox* followPreviewCheckBox_ = nullptr;
    QFont headerLineNumberFont_;
    QVector<double> zoomPresets_;
    QVector<double> buttonZoomPresets_;
    int zoomPresetIndex_ = 0;
    FocusTarget focusTarget_ = FocusTarget::Playhead;
    bool timelineDragActive_ = false;
    int timelineDragStartX_ = 0;
    int timelineDragStartScrollValue_ = 0;
    int heldHorizontalKeyScrollDirection_ = 0;
    int heldHorizontalKeyScrollKey_ = 0;
    int heldHorizontalKeyScrollLastElapsedMs_ = 0;
    double heldHorizontalKeyScrollRemainderPixels_ = 0.0;
    bool playheadIndicatorSuppressed_ = false;
    quint64 debugPaintSampleCount_ = 0;
    qint64 debugPaintTotalElapsedNs_ = 0;
    qint64 debugPaintMaxElapsedNs_ = 0;
    quint64 debugScrollEventCount_ = 0;
    QElapsedTimer heldHorizontalKeyScrollElapsed_;
    QTimer* playheadIndicatorRestoreTimer_ = nullptr;
    QTimer* heldHorizontalKeyScrollTimer_ = nullptr;
};
