#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMetaObject>
#include <QPointer>
#include <QQuickItem>
#include <QTimer>

#include <memory>

#include "timeline/TimelineSceneState.h"

class QHoverEvent;
class TimelineQuickStateBridge;
class TimelineQuickTextureCache;
class TimelineQuickGridLayer;
class TimelineQuickWaveformLayer;
class TimelineQuickHeaderLayer;
class TimelineQuickGridLinesLayer;
class TimelineQuickNotesLayer;
class TimelineQuickOverlayLayer;

namespace miacode::preview::dcomp {
class TimelineRenderView;
}

class TimelineQuickItem : public QQuickItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* stateBridge READ stateBridgeObject WRITE setStateBridgeObject NOTIFY stateBridgeChanged)
    Q_PROPERTY(int headerLeftLimit READ headerLeftLimit WRITE setHeaderLeftLimit NOTIFY headerInsetsChanged)
    Q_PROPERTY(int headerRightLimit READ headerRightLimit WRITE setHeaderRightLimit NOTIFY headerInsetsChanged)
    Q_PROPERTY(qreal zoomScale READ zoomScale NOTIFY zoomScaleChanged)
    Q_PROPERTY(bool followPreviewEnabled READ followPreviewEnabled WRITE setFollowPreviewEnabled NOTIFY followPreviewEnabledChanged)
    Q_PROPERTY(bool viewportLockEnabled READ viewportLockEnabled WRITE setViewportLockEnabled NOTIFY viewportLockEnabledChanged)
    Q_PROPERTY(bool followProgressEnabled READ followProgressEnabled WRITE setFollowProgressEnabled NOTIFY followProgressEnabledChanged)
    Q_PROPERTY(int timelineTop READ timelineTop NOTIFY sceneMetricsChanged)
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)

public:
    explicit TimelineQuickItem(QQuickItem* parent = nullptr);
    ~TimelineQuickItem() override;

    TimelineQuickStateBridge* stateBridge() const;
    QObject* stateBridgeObject() const;
    void setStateBridge(TimelineQuickStateBridge* stateBridge);
    void setStateBridgeObject(QObject* stateBridgeObject);

    int headerLeftLimit() const;
    void setHeaderLeftLimit(int value);
    int headerRightLimit() const;
    void setHeaderRightLimit(int value);

    qreal zoomScale() const;
    bool followPreviewEnabled() const;
    void setFollowPreviewEnabled(bool enabled);
    bool viewportLockEnabled() const;
    void setViewportLockEnabled(bool enabled);
    bool followProgressEnabled() const;
    void setFollowProgressEnabled(bool enabled);
    int timelineTop() const;
    bool isReady() const;

    Q_INVOKABLE void cycleZoomPreset();
    Q_INVOKABLE void refreshTheme();

signals:
    void stateBridgeChanged();
    void headerInsetsChanged();
    void zoomScaleChanged();
    void followPreviewEnabledChanged();
    void viewportLockEnabledChanged();
    void followProgressEnabledChanged();
    void sceneMetricsChanged();
    void readyChanged();
    void timelineSurfaceReady();
    void playheadChanged(double second);
    void headerNavigateRequested(double second);
    void timelineWheelNavigateRequested(double second);
    void centerNavigateRequested(double second);
    void timelineDragStarted();
    void timelineDragFinished(double second);
    void timelineUserInteractionStarted();
    void followPreviewToggled(bool enabled);
    void viewportLockToggled(bool enabled);
    void followProgressToggled(bool enabled);
    void previewPlayPauseRequested();

protected:
    QSGNode* updatePaintNode(QSGNode* oldNode, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) override;
    void hoverMoveEvent(QHoverEvent* event) override;
    void hoverLeaveEvent(QHoverEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void keyReleaseEvent(QKeyEvent* event) override;

private:
    void syncSourceState();
    void updateReadyState(bool ready);
    bool canBecomeReady() const;
    miacode::timeline::TimelineSceneState currentSceneState() const;
    double clampSceneSecond(double second) const;
    double viewportCenterSecondForScroll(int horizontalScrollValue) const;
    bool playheadNearViewportCenter() const;
    void beginHeldHorizontalKeyScroll(int direction, int key);
    void stopHeldHorizontalKeyScroll(int key = 0);
    void applyHeldHorizontalKeyScrollTick();
    QPointer<TimelineQuickStateBridge> stateBridge_;
    QMetaObject::Connection bridgeRenderStateConnection_;
    QMetaObject::Connection bridgePlayheadConnection_;
    int headerLeftLimit_ = 0;
    int headerRightLimit_ = 0;
    qreal cachedZoomScale_ = 0.5;
    bool cachedFollowPreviewEnabled_ = false;
    bool cachedViewportLockEnabled_ = false;
    bool cachedFollowProgressEnabled_ = true;
    int cachedTimelineTop_ = 0;
    bool ready_ = false;
    quint64 appearanceRevision_ = 0;
    qreal cachedDevicePixelRatio_ = 0.0;
    // Phase-4e-old-opt — was QString built via per-paint label-name
    // concat; replaced by a 64-bit hash. `0` is sentinel for
    // "uninitialised" (no chart loaded yet); a real chart will hash to
    // some non-zero value with overwhelming probability.
    quint64 cachedThemeSignature_ = 0;
    bool cachedThemeSignatureValid_ = false;
    bool pendingThemeInvalidation_ = false;
    bool pendingDprInvalidation_ = false;
    mutable bool cachedSceneStateValid_ = false;
    mutable miacode::timeline::TimelineSceneState cachedSceneState_;
    mutable QSize cachedSceneBuildViewportSize_;
    // Phase 7 — scroll-bucket viewport culling. The cache is rebuilt
    // when the user scrolls into a new bucket (= one viewport-width
    // wide). Combined with the bucket-bumped revisions in
    // applyDynamicSceneState, the QSG layers see new revisions on
    // bucket transitions and rebuild their children with the freshly
    // emitted (culled) primitives. Within a bucket, the per-frame
    // transform handles the small offset and no rebuild fires.
    // INT_MIN is the "never built" sentinel so the first call always
    // rebuilds.
    mutable int cachedScrollBucket_ = INT_MIN;
    // Phase 9d-native polish — header-control state participates in
    // the cache key so the native zoom-button text + follow-check tick
    // update on click rather than waiting for a playback tick to bump
    // an unrelated revision.
    mutable bool cachedSceneBuildFollowPreviewEnabled_ = false;
    mutable bool cachedSceneBuildFollowProgressEnabled_ = true;
    mutable double cachedSceneBuildZoomScale_ = -1.0;  // sentinel: forces first build
    mutable double cachedSceneBuildContentScale_ = -1.0;
    mutable int cachedSceneBuildHeaderLeftLimit_ = 0;
    mutable int cachedSceneBuildHeaderRightLimit_ = 0;
    mutable quint64 cachedSceneBuildAppearanceRevision_ = 0;
    mutable quint64 cachedSceneBuildGridRevision_ = 0;
    mutable quint64 cachedSceneBuildWaveformRevision_ = 0;
    mutable quint64 cachedSceneBuildHeaderRevision_ = 0;
    mutable quint64 cachedSceneBuildNotesRevision_ = 0;
    mutable quint64 cachedSceneBuildOverlayRevision_ = 0;
    mutable quint64 sceneStateRebuildCount_ = 0;
    // Per-second timing of updatePaintNode itself, so we can locate the
    // actual cost (currentSceneState walk, layer updateNode work, QSG
    // sync handoff). Logged once per second to avoid spamming the
    // hot path. Tells us whether the GUI-thread block we see in
    // PreviewDCompSurface::presented_gap_stats is coming from this
    // updatePaintNode running long, or from the QSG infrastructure
    // surrounding it.
    mutable qint64 updatePaintNodeCount_ = 0;
    mutable qint64 updatePaintNodeSumNs_ = 0;
    mutable qint64 updatePaintNodeMaxNs_ = 0;
    mutable qint64 updatePaintNodeLastLogMs_ = 0;
    bool dragActive_ = false;
    int dragStartX_ = 0;
    int dragStartScrollValue_ = 0;
    int lastPaintedHorizontalScrollValue_ = -1;
    int heldHorizontalKeyScrollDirection_ = 0;
    int heldHorizontalKeyScrollKey_ = 0;
    int heldHorizontalKeyScrollLastElapsedMs_ = 0;
    double heldHorizontalKeyScrollRemainderPixels_ = 0.0;
    QElapsedTimer heldHorizontalKeyScrollElapsed_;
    QTimer heldHorizontalKeyScrollTimer_;
    std::unique_ptr<TimelineQuickTextureCache> textures_;
    std::unique_ptr<TimelineQuickGridLayer> gridLayer_;
    std::unique_ptr<TimelineQuickWaveformLayer> waveformLayer_;
    std::unique_ptr<TimelineQuickHeaderLayer> headerLayer_;
    std::unique_ptr<TimelineQuickGridLinesLayer> gridLinesLayer_;
    std::unique_ptr<TimelineQuickNotesLayer> notesLayer_;
    std::unique_ptr<TimelineQuickOverlayLayer> overlayLayer_;

    // Phase 3c — DComp render view for the timeline pane. Nullptr when
    // previewTimelineUseDCompEnabled() returned false at construction.
    // Lives next to the QSG paint code: the QSG path keeps drawing
    // (so the editor stays functional if DComp fails), and the DComp
    // top-level popup HWND overlays the QSG output. Phase 3e drops
    // the QSG path entirely once timeline-DComp ships its remaining
    // primitive types.
    std::unique_ptr<miacode::preview::dcomp::TimelineRenderView> dcompView_;
    QMetaObject::Connection dcompWindowConnection_;
    void pushSceneStateToDComp();
};
