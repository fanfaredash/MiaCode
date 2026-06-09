#pragma once

#include <QImage>
#include <QObject>
#include <QPointer>
#include <QSize>
#include <QString>
#include <QVariantMap>

class QQmlEngine;
class QQuickItem;
class QQuickWindow;
class QWidget;
class PreviewQuickSceneRoot;

namespace miacode::preview::scene {
struct PreviewFrameState;
}

namespace miacode::cover_export {

class CoverLayoutModel;

struct CoverExportResult {
    bool success = false;
    QString outputPath;       // absolute path of the saved image (on success)
    QString errorMessage;     // populated on failure
};

enum class CoverBackgroundMode {
    Jacket = 0,        // backdrop = chart 曲绘 (opaque, JPG)
    Custom = 1,        // backdrop = a custom image (opaque, JPG)
    Transparent = 2,   // no backdrop; card over alpha (PNG)
};

// Presentation inputs — everything EXCEPT the per-layer layout (which lives in
// CoverLayoutModel). Built once from the dialog controls and applied identically
// to the live preview and the export render, so they are pixel-identical bar
// resolution.
struct CoverComposerInputs {
    QVariantMap templateMap;     // parsed :/intro/templates/maimai_banner.json
    QVariantMap trackOverrides;  // title/artist/designer/level/difficulty/bpm/mode/lvRenderMode/stillTextMode
    QString jacketPath;          // chart 曲绘 (jacket slot + default backdrop); empty -> logo
    QString backgroundPath;      // custom backdrop (Custom mode only)
    CoverBackgroundMode backgroundMode = CoverBackgroundMode::Jacket;
    bool blurBackground = true;
    bool cardShadow = false;
    // B1 — chart-frame inner-ring background. When enabled (and the background is
    // not Transparent), the chart-frame playfield disk shows the SAME background
    // image (曲绘 / custom), crisp, circular-masked to the ring, dimmed by
    // brightness; the outer ring stays transparent. diskDiameter is the ring
    // diameter / square side (from SceneFrameRenderer::playfieldDiskDiameterFraction).
    bool chartFrameBackground = false;
    double chartFrameBgBrightness = 0.8;   // 0..1 (1 = full bright; maps to MultiEffect.brightness − 1)
    double chartFrameDiskDiameter = 0.0;   // 0 = unknown → no disk drawn
};

// Live, embeddable WYSIWYG composer. Owns a bare QQuickWindow hosting
// CoverComposer.qml — embedded into a QWidget via QWidget::createWindowContainer
// — that renders continuously on the in-process D3D11 RHI. Dragging/scaling
// mutates the shared CoverLayoutModel with zero readback.
//
//   ⚠ Two hard constraints (each cost a crash to learn — keep both):
//     1. NO QQuickView (re-registers QtQuick modules in the packaged layout →
//        crash). A bare QQmlEngine + QQmlComponent parented into a plain
//        QQuickWindow is used, then embedded via createWindowContainer.
//     2. NO forced OpenGL / manual QRhi — runs in-process on the app's RHI.
//
// GUI-thread only; not thread-safe.
class CoverComposerView : public QObject
{
    Q_OBJECT

public:
    explicit CoverComposerView(CoverLayoutModel* model, QObject* parent = nullptr);
    ~CoverComposerView() override;
    CoverComposerView(const CoverComposerView&) = delete;
    CoverComposerView& operator=(const CoverComposerView&) = delete;

    // Build (once) and return the QWidget embedding the live Quick window. The
    // caller owns layout: size the container to the chosen OUTPUT aspect so the
    // normalized layout matches the export exactly.
    QWidget* createContainer(QWidget* parent);

    // Push presentation inputs to the live scene.
    void setInputs(const CoverComposerInputs& inputs);

    // ---- A2: live chart-frame edit scene -------------------------------------
    // Hand the live edit scene the shared, already-bootstrapped frame state
    // (owned by the dialog's SceneFrameRenderer). The in-QML PreviewQuickSceneRoot
    // reads it directly, so scrubbing/playback only moves the playhead — no grab.
    // Must be called BEFORE the user can enable the chart-frame layer.
    void setChartFrameState(const miacode::preview::scene::PreviewFrameState* frameState);

    // Repaint the live edit scene at the current playhead (call after moving the
    // shared playhead via SceneFrameRenderer::setPlayheadSeconds). No-op until the
    // live scene is bound. Zero readback — just schedules a QSG frame.
    void refreshLiveChartScene();

    // Drop the live scene's reference to the shared frame state. The dialog calls
    // this in its destructor body, BEFORE the SceneFrameRenderer (which owns the
    // frame state) is torn down, so the live QQuickItem can never read freed state.
    void detachLiveChartScene();

    // Called from QML (CoverComposer.qml's chart-frame Loader) when the live
    // PreviewQuickSceneRoot is created / destroyed. bind configures it (layer
    // flags, DComp fallback, shared frame state); unbind clears our pointer.
    Q_INVOKABLE void bindLiveChartScene(QObject* sceneRoot);
    Q_INVOKABLE void unbindLiveChartScene();

    bool isValid() const { return root_ != nullptr; }
    QString lastError() const { return lastError_; }

private:
    bool ensureLoaded();
    void applyInputs();
    void syncRootSize();

    CoverLayoutModel* model_ = nullptr;   // not owned
    QQmlEngine* engine_ = nullptr;
    QQuickWindow* window_ = nullptr;
    QQuickItem* root_ = nullptr;
    QWidget* container_ = nullptr;
    CoverComposerInputs inputs_;
    QString lastError_;

    // A2 live edit scene. chartFrameState_ is borrowed (owned by the dialog's
    // SceneFrameRenderer); liveChartScene_ is owned by QML (the Loader), tracked
    // with QPointer so a teardown clears it safely.
    const miacode::preview::scene::PreviewFrameState* chartFrameState_ = nullptr;
    QPointer<PreviewQuickSceneRoot> liveChartScene_;
};

// Off-screen full-resolution render of the SAME composer scene + model, for
// export. Transient bare QQuickWindow + grabWindow() (in-process D3D11). Returns
// a null QImage and sets *errorMessage on failure. Transparent mode -> ARGB32,
// otherwise RGB32, normalised to fullSize at dpr 1.0.
QImage renderCoverComposite(CoverLayoutModel* model,
                            const CoverComposerInputs& inputs,
                            const QSize& fullSize,
                            QString* errorMessage);

// renderCoverComposite + save: PNG (alpha) when transparent, otherwise JPG, to
// outputDirectory as card.png / card.jpg (adds "(1)","(2)" … on collision).
CoverExportResult exportCoverComposite(CoverLayoutModel* model,
                                       const CoverComposerInputs& inputs,
                                       const QSize& fullSize,
                                       const QString& outputDirectory);

}  // namespace miacode::cover_export
