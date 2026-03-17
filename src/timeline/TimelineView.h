#pragma once

#include <QAbstractScrollArea>
#include <QCheckBox>
#include <QFont>
#include <QHash>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QToolButton>
#include <QVector>
#include <QEvent>

struct TimelineBeatMarker {
    double second = 0.0;
    bool major = false;
    int sourceLine = 1;
};

struct TimelineNoteMarker {
    double second = 0.0;
    double endSecond = -1.0;
    double slideTraceSecond = -1.0;
    double availableSecond = -1.0;
    int sourceLine = 1;
    int sourceCol = 1;
    int lane = 1;
    int endLane = 1;
    QString type;
    QString slideTrackKey;
    QStringList slideSegmentKeys;
    QVector<double> slideSegmentShootSeconds;
    QVector<double> slideSegmentDurations;
    QVector<QVector<QPointF>> slideSegmentPoints;
    QVector<QVector<double>> slideSegmentAngles;
    QVector<QVector<QPointF>> wifiLanePoints;
    QVector<QVector<double>> wifiLaneAngles;
    QVector<QVector<QVector<QPointF>>> slideTrackAreaPoints;
    QVector<QVector<QVector<double>>> slideTrackAreaRotations;
    QVector<QVector<double>> slideTrackAreaThresholds;
    QVector<QVector<QVector<double>>> slideTrackAreaCheckpoints;
    QVector<QVector<QVector<int>>> slideTrackAreaCutIndices;
    QVector<QVector<QPointF>> wifiTrackAreaPoints;
    QVector<QVector<double>> wifiTrackAreaRotations;
    QVector<QVector<int>> wifiTrackAreaImageIndices;
    QVector<double> wifiTrackAreaThresholds;
    QVector<QVector<double>> wifiTrackAreaCheckpoints;
    QPointF touchPoint;
    QString touchPad;
    bool isEach = false;
    bool isBreak = false;
    bool isEx = false;
    bool isFirework = false;
    bool onSlide = false;
    bool slideHead = false;
    bool tailOnSlideHead = false;
    bool slideEach = false;
    bool sameHeadSlide = false;
    bool beforeSlide = false;
    bool afterSlide = false;
    bool headEach = false;
    bool headBreak = false;
    bool headEx = false;
    bool trackBreak = false;
    bool hasHeadStar = true;
};

class TimelineView : public QAbstractScrollArea
{
    Q_OBJECT

public:
    explicit TimelineView(QWidget* parent = nullptr);
    QSize minimumSizeHint() const override;
    QSize sizeHint() const override;
    void setHeaderLineNumberFont(const QFont& font);
    void setTimelineData(
        const QVector<TimelineBeatMarker>& beats,
        const QVector<TimelineNoteMarker>& notes,
        double durationSeconds
    );
    void setWaveformData(const QVector<float>& peaks, double startSecond = 0.0, double durationSeconds = 0.0);
    void clear();
    void setPlayheadUpperLimitSeconds(double second);
    void setPlayheadSeconds(double second, bool centerView);
    void setCursorSeconds(double second);
    double playheadSeconds() const;
    double durationSeconds() const;
    void setShowSlideTracks(bool show);
    bool showSlideTracks() const;
    double zoomScale() const;
    void setFollowPreviewEnabled(bool enabled);
    bool followPreviewEnabled() const;
    void refreshTheme();

signals:
    void playheadChanged(double second);
    void noteNavigateRequested(int line, int col);
    void headerNavigateRequested(double second);
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
    void updateDisplayBounds();
    void updateHorizontalRange();
    int contentWidth() const;
    int timelineLeft() const;
    int timelineTop() const;
    int laneHeight() const;
    int timelineHeight() const;
    int notePixelSize() const;
    int secondToX(double second) const;
    double xToSecond(int x) const;
    bool effectiveShowSlideTracks() const;
    void cycleZoomPreset();
    void updateZoomButtonAppearance();
    void layoutHeaderButtons();
    int lineNumberForSecond(double second) const;
    QPixmap iconForType(const QString& type) const;
    void loadNoteIcons();
    const TimelineNoteMarker* nearestNoteForViewportPos(const QPointF& pos) const;
    int minimumContentHeightForCurrentDevice() const;
    void refreshMinimumHeightForCurrentDevice();

    QVector<TimelineBeatMarker> beats_;
    QVector<TimelineNoteMarker> notes_;
    double durationSeconds_ = 0.0;
    double playheadSeconds_ = 0.0;
    double cursorSeconds_ = 0.0;
    double playheadUpperLimitSeconds_ = -1.0;
    double displayStartSeconds_ = -0.5;
    double displayEndSeconds_ = 1.0;
    double pixelsPerSecond_ = 120.0;
    bool showSlideTracks_ = true;
    QHash<QString, QPixmap> noteIcons_;
    QVector<float> waveformPeaks_;
    double waveformStartSeconds_ = 0.0;
    double waveformDurationSeconds_ = 0.0;
    QToolButton* zoomButton_ = nullptr;
    QCheckBox* followPreviewCheckBox_ = nullptr;
    QFont headerLineNumberFont_;
    QVector<double> zoomPresets_{0.25, 0.5, 1.0, 1.5, 2.0};
    int zoomPresetIndex_ = 2;
    bool timelineDragActive_ = false;
    int timelineDragStartX_ = 0;
    int timelineDragStartScrollValue_ = 0;
};
