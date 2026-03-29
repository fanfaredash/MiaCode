#pragma once

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QToolButton>
#include <QVector>
#include <QEvent>

#include "timeline/TimelineRenderData.h"
#include "common/MuriTypes.h"

class TimelineView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TimelineView(QWidget* parent = nullptr);
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;
    void setHeaderLineNumberFont(const QFont& font);
    void setTimelineData(const TimelineRenderSnapshot& snapshot);
    void setWaveformData(const QVector<float>& peaks, double startSecond = 0.0, double durationSeconds = 0.0);
    void clear();
    void setPlayheadUpperLimitSeconds(double second);
    void setPlayheadSeconds(double second, bool centerView);
    void setCursorSeconds(double second, bool centerView = false);
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
    void noteNavigateRequested(int line, int col);
    void headerNavigateRequested(double second);
    void centerNavigateRequested(double second);
    void timelineDragStarted();
    void timelineUserInteractionStarted();
    void followPreviewToggled(bool enabled);

protected:
    bool viewportEvent(QEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
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

    struct HoveredNoteRef {
        const TimelineRenderLine* line = nullptr;
        const TimelineRenderNote* note = nullptr;
    };

    struct HoldPixmapParts {
        QPixmap cap;
        QPixmap leftHalf;
        QPixmap rightHalf;
        QImage bodySlice;
        int rightHalfOffset = 0;
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
    void updateCursorToViewportCenter(bool emitNavigate = true);
    void suppressPlayheadIndicatorForInteraction();
    void restorePlayheadIndicatorAfterInteraction();
    bool effectiveShowSlideTracks() const;
    void cycleZoomPreset();
    void applyZoomPresetIndex(int nextIndex, double anchorSecond);
    void stepZoomPreset(int deltaSteps, double anchorSecond);
    bool handleAltZoomWheel(QWheelEvent* event);
    void updateZoomButtonAppearance();
    void layoutHeaderButtons();
    int lineNumberForSecond(double second) const;
    VisibleLineRange visibleLineRange(double startSecond, double endSecond) const;
    void updateTimelineMarkerStrip(double oldSecond, double newSecond, int halfWidth);
    const QPixmap& iconForType(const QString& type) const;
    const QPixmap& transformedIconForType(
        const QString& type,
        qreal scale = 1.0,
        qreal rotationDegrees = 0.0,
        bool mirrorX = false) const;
    const HoldPixmapParts& holdPixmapPartsForType(const QString& type, qreal scale) const;
    void loadNoteIcons();
    HoveredNoteRef nearestNoteForViewportPos(const QPointF& pos) const;
    int minimumContentHeightForCurrentDevice() const;
    void refreshMinimumHeightForCurrentDevice();

    QVector<TimelineRenderLine> lines_;
    double durationSeconds_ = 0.0;
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
    mutable QHash<QString, QPixmap> transformedIconCache_;
    mutable QHash<QString, HoldPixmapParts> holdPixmapPartsCache_;
    QVector<float> waveformPeaks_;
    double waveformStartSeconds_ = 0.0;
    double waveformDurationSeconds_ = 0.0;
    QToolButton* zoomButton_ = nullptr;
    QCheckBox* followPreviewCheckBox_ = nullptr;
    QFont headerLineNumberFont_;
    QVector<double> zoomPresets_;
    QVector<double> buttonZoomPresets_;
    int zoomPresetIndex_ = 0;
    bool timelineDragActive_ = false;
    int timelineDragStartX_ = 0;
    int timelineDragStartScrollValue_ = 0;
    bool playheadIndicatorSuppressed_ = false;
    QTimer* playheadIndicatorRestoreTimer_ = nullptr;
};
