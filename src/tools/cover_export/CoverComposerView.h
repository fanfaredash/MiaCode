#pragma once

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>
#include <QVariantMap>

class QQmlEngine;
class QQuickItem;
class QQuickWindow;
class QWidget;

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
