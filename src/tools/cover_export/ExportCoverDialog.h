#pragma once

#include <QDialog>
#include <QSize>
#include <QString>
#include <QVariantMap>

#include <memory>

#include "tools/cover_export/CoverComposerView.h"      // CoverComposerInputs, CoverExportResult, CoverComposerView
#include "tools/video_export/VideoExportController.h"    // IntroBannerSpec, VideoExportTask

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QTimer;
class QWidget;

namespace miacode::cover_export {
class CoverLayoutModel;
class SceneFrameRenderer;
}

// Sub-dialog launched from the video-export dialog's Font tab ("导出封面" /
// "Export Cover"). Hosts a LIVE WYSIWYG cover composer (CoverComposer.qml,
// embedded via QWidget::createWindowContainer): a full-bleed background layer
// (chart 曲绘 / custom image / transparent, blurred or crisp) plus the difficulty
// card and an optional chart-frame still as draggable/scalable layers with
// snap-to-centre/edge guides. The chart frame is a square playfield grab at a
// user-picked time, rendered in-process by SceneFrameRenderer. The same scene +
// normalized layout model drive the full-resolution export, so preview == export.
// On accept the caller drives exportCover(outputDir).
class ExportCoverDialog : public QDialog
{
    Q_OBJECT

public:
    // `task` supplies the difficulty-card banner (task.intro), the parsed note
    // markers + skin dir + render settings the chart-frame renderer needs, and the
    // content duration for the frame-picker range.
    ExportCoverDialog(const VideoExportTask& task, const QSize& initialSize, QWidget* parent = nullptr);
    ~ExportCoverDialog() override;

    // Render the composed cover at the chosen size and save it under
    // outputDirectory (PNG when transparent, JPG otherwise).
    miacode::cover_export::CoverExportResult exportCover(const QString& outputDirectory);

private:
    void pushInputs();
    void browseBackground();
    void syncControlEnabled();
    void resizePreviewToAspect();
    miacode::cover_export::CoverComposerInputs buildInputs() const;
    QSize currentSize() const;

    // Chart-frame picker.
    void onChartFrameToggled(bool on);
    bool renderChartFrameNow();            // grab at the current slider time + push to the model; true if a still was produced
    void scheduleChartFrameRender();       // debounced renderChartFrameNow
    int chartFrameRenderPx() const;        // square render size = layer-export px

    IntroBannerSpec banner_;

    // Parsed :/intro/templates/maimai_banner.json, read ONCE in the ctor (not per
    // buildInputs() call) so live-preview keystrokes don't re-read the qrc + deep-
    // clone the template each time.
    QVariantMap cachedTemplate_;

    miacode::cover_export::CoverLayoutModel* model_ = nullptr;
    miacode::cover_export::CoverComposerView* composerView_ = nullptr;
    QWidget* previewContainer_ = nullptr;

    QComboBox* sizeCombo_ = nullptr;
    QComboBox* backgroundCombo_ = nullptr;
    QLineEdit* backgroundPathEdit_ = nullptr;
    QPushButton* backgroundBrowse_ = nullptr;
    QCheckBox* blurCheck_ = nullptr;
    QCheckBox* cardShadowCheck_ = nullptr;
    QCheckBox* levelTextRenderCheck_ = nullptr;
    QComboBox* textOverflowCombo_ = nullptr;
    QPushButton* resetLayoutButton_ = nullptr;

    // Chart-frame layer + its in-process renderer.
    std::unique_ptr<miacode::cover_export::SceneFrameRenderer> sceneFrameRenderer_;
    bool chartFrameAvailable_ = false;     // false when the difficulty has no notes / skin failed
    double contentDurationSeconds_ = 0.0;
    QCheckBox* chartFrameCheck_ = nullptr;
    QSlider* frameSlider_ = nullptr;       // value = milliseconds into the chart
    QLabel* frameTimeLabel_ = nullptr;
    QTimer* frameDebounce_ = nullptr;
    bool rendering_ = false;               // re-entrancy guard (renderAt pumps the event loop)
};
