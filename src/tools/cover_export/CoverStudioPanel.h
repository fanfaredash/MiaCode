#pragma once

#include <QWidget>
#include <QElapsedTimer>
#include <QIcon>
#include <QSize>
#include <QString>
#include <QVariantMap>

#include <memory>

#include "tools/cover_export/CoverComposerView.h"      // CoverComposerInputs, CoverExportResult, CoverComposerView
#include "tools/video_export/VideoExportController.h"    // IntroBannerSpec, VideoExportTask

class QCheckBox;
class QComboBox;
class QJsonObject;
class QLabel;
class QLineEdit;
class QPushButton;
class QResizeEvent;
class QSlider;
class QTimer;
class QWidget;

namespace miacode::cover_export {
class CoverLayer;
class CoverLayoutModel;
class SceneFrameRenderer;

// Sub-dialog launched from the video-export dialog's Font tab ("导出封面" /
// "Export Cover"). Hosts a LIVE WYSIWYG cover composer (CoverComposer.qml,
// embedded via QWidget::createWindowContainer): a full-bleed background layer
// (chart 曲绘 / custom image / transparent, blurred or crisp) plus the difficulty
// card and an optional chart-frame still as draggable/scalable layers with
// snap-to-centre/edge guides. The chart frame is a square playfield grab at a
// user-picked time, rendered in-process by SceneFrameRenderer. The same scene +
// normalized layout model drive the full-resolution export, so preview == export.
// On accept the caller drives exportCover(outputDir).
class CoverStudioPanel : public QWidget
{
    Q_OBJECT

public:
    // `task` supplies the difficulty-card banner (task.intro), the parsed note
    // markers + skin dir + render settings the chart-frame renderer needs, and the
    // content duration for the frame-picker range.
    CoverStudioPanel(const VideoExportTask& task, const QSize& initialSize, QWidget* parent = nullptr);
    ~CoverStudioPanel() override;

    // Render the composed cover at the chosen size and save it under
    // outputDirectory (PNG when transparent, JPG otherwise).
    miacode::cover_export::CoverExportResult exportCover(const QString& outputDirectory);

protected:
    // Intercepts ←/→ on the frame slider for the held-seek (below).
    bool eventFilter(QObject* watched, QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

public:
    miacode::cover_export::CoverLayoutModel* layoutModel() const { return model_; }
    QString activeLayerKey() const { return activeLayerKey_; }
    void setActiveLayerKey(const QString& key);
    void addChartFrameLayer();
    void duplicateActiveLayer();
    void removeActiveLayer();
    void moveActiveLayerUp();
    void moveActiveLayerDown();
    void moveActiveLayerToTop();
    void moveActiveLayerToBottom();
    void setActiveLayerVisible(bool visible);
    void setActiveLayerLocked(bool locked);
    void setActiveLayerOpacity(qreal opacity);
    void setActiveLayerSizeFraction(qreal sizeFraction);
    void setActiveLayerCenter(qreal nx, qreal ny);
    void setActiveLayerFrameSeconds(double seconds);
    void setActiveLayerFrameBgEnabled(bool enabled);
    void setActiveLayerFrameBgBrightness(qreal brightness);
    void stepActiveFrameBySeconds(double deltaSeconds);
    bool chartFrameAvailable() const { return chartFrameAvailable_; }
    double contentDurationSeconds() const { return contentDurationSeconds_; }
    QWidget* takeSettingsPanel(QWidget* parent = nullptr);
    void resetLayout();
    void saveLayout();
    void importLayout();
    void requestExport();
    void requestCancel();

signals:
    void exportRequested();
    void cancelRequested();
    void activeLayerChanged(const QString& key);
    void compositionChanged();

private:
    void pushInputs();
    void browseBackground();
    void syncControlEnabled();
    void resizePreviewToAspect();
    miacode::cover_export::CoverComposerInputs buildInputs() const;
    QSize currentSize() const;

    // B2 — layout save / import. The whole composition (size + background + card +
    // chart-frame settings + layer geometry) round-trips through one JSON file.
    // The same JSON also persists to app preferences (app.cover_export): saved on
    // export, restored on dialog open. `interactive=false` (the silent preference
    // restore) suppresses the fallback notice boxes an explicit import shows.
    QJsonObject exportCompositionJson() const;
    void applyCompositionJson(const QJsonObject& root, bool interactive = true);

    // Chart-frame picker.
    void onChartFrameToggled(bool on);
    void syncActiveLayerControls();
    miacode::cover_export::CoverLayer* activeLayer() const;
    miacode::cover_export::CoverLayer* activeChartFrameLayer() const;
    bool renderChartFrameNow();            // grab a still at the current playhead + push to the active frame layer; true if a still was produced
    bool renderChartFrameLayerNow(miacode::cover_export::CoverLayer* layer);
    int chartFrameRenderPx(const miacode::cover_export::CoverLayer* layer) const;        // square render size = layer-export px

    // A2 — live edit scene + visual play/pause. Scrubbing/playing only moves the
    // shared playhead and repaints the in-QML live scene (zero readback); the
    // offscreen grab is reserved for the export still.
    void applyFrameSeconds(double seconds);   // set playhead + repaint live scene + readout
    void togglePlayback();
    void startPlayback();
    void stopPlayback();
    void onPlayTick();

    // Held ←/→ seek on the frame slider — REUSES the preview transport's
    // acceleration/cap design (miacode::preview_interaction): an immediate
    // ±1/120 s step on press, then a 16 ms precise timer advancing by real
    // elapsed wall time × min(1 + heldSeconds, 3.0)× rate. Mirrors
    // MainWindow::TimelineSection::beginPreviewHeldSeek / applyPreviewHeldSeekTick.
    void beginFrameHeldSeek(int direction, int key);
    void stopFrameHeldSeek(int key = 0);
    void onFrameHeldSeekTick();
    void stepFrameBySeconds(double deltaSeconds);   // move playhead + slider, clamped

    IntroBannerSpec banner_;

    // Parsed :/intro/templates/maimai_banner.json, read ONCE in the ctor (not per
    // buildInputs() call) so live-preview keystrokes don't re-read the qrc + deep-
    // clone the template each time.
    QVariantMap cachedTemplate_;

    miacode::cover_export::CoverLayoutModel* model_ = nullptr;
    miacode::cover_export::CoverComposerView* composerView_ = nullptr;
    QString activeLayerKey_;
    QWidget* previewFrame_ = nullptr;
    QWidget* previewContainer_ = nullptr;
    QWidget* hiddenControlsHost_ = nullptr;
    // Height of the three stacked control groups (+ inter-group gaps); the preview
    // frame is sized to this so its bottom aligns with the 谱面帧 group bottom.

    QComboBox* sizeCombo_ = nullptr;
    QComboBox* backgroundCombo_ = nullptr;
    QLineEdit* backgroundPathEdit_ = nullptr;
    QPushButton* backgroundBrowse_ = nullptr;
    QCheckBox* blurCheck_ = nullptr;
    // Difficulty-card section: the card is opt-in like the chart frame (its
    // checkbox drives the card layer's `visible` on the shared model).
    QCheckBox* cardCheck_ = nullptr;
    // DX / SD chart type: drives which mode plate the card shows (でらっくす /
    // スタンダード) and, for SD, mirrors the tab shoulder to the right.
    QComboBox* cardModeCombo_ = nullptr;
    QCheckBox* cardShadowCheck_ = nullptr;
    QCheckBox* levelTextRenderCheck_ = nullptr;
    QComboBox* textOverflowCombo_ = nullptr;
    QPushButton* resetLayoutButton_ = nullptr;
    QPushButton* saveLayoutButton_ = nullptr;
    QPushButton* importLayoutButton_ = nullptr;

    // Chart-frame layer + its in-process renderer.
    std::unique_ptr<miacode::cover_export::SceneFrameRenderer> sceneFrameRenderer_;
    bool chartFrameAvailable_ = false;     // false when the difficulty has no notes / skin failed
    double contentDurationSeconds_ = 0.0;
    QCheckBox* chartFrameCheck_ = nullptr;
    QSlider* frameSlider_ = nullptr;       // value = milliseconds into the chart
    QLabel* frameTimeLabel_ = nullptr;
    QPushButton* playButton_ = nullptr;
    // Painted transport icons (slim bars / triangle in the theme text colour) —
    // font glyphs were rejected: ⏸ = color emoji, ❚❚ = too heavy/wide.
    QIcon playIcon_;
    QIcon pauseIcon_;
    // B1 — chart-frame inner-ring background (reuses the cover background image).
    QCheckBox* chartFrameBgCheck_ = nullptr;
    QSlider* chartFrameBgBrightnessSlider_ = nullptr;   // 0..100 → brightness 0..1
    bool rendering_ = false;               // re-entrancy guard (renderAt pumps the event loop)

    // Visual-only playback clock (no audio): a wall-clock timer advancing the
    // shared playhead while the live scene repaints. playWall_ measures elapsed
    // wall time so playback runs at real speed regardless of tick jitter.
    QTimer* playClock_ = nullptr;
    QElapsedTimer playWall_;
    double playStartSeconds_ = 0.0;        // playhead (s) when the current play run started
    bool playing_ = false;

    // Held ←/→ seek state (see beginFrameHeldSeek).
    QTimer* frameSeekHoldTimer_ = nullptr;
    QElapsedTimer frameSeekHoldElapsed_;
    int frameSeekHoldDirection_ = 0;
    int frameSeekHoldKey_ = 0;
    int frameSeekHoldLastElapsedMs_ = 0;
};

}  // namespace miacode::cover_export
