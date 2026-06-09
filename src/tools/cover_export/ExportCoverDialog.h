#pragma once

#include <QDialog>
#include <QSize>
#include <QString>
#include <QVariantMap>

#include "tools/cover_export/CoverComposerView.h"      // CoverComposerInputs, CoverExportResult, CoverComposerView
#include "tools/video_export/VideoExportController.h"    // IntroBannerSpec

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class QWidget;

namespace miacode::cover_export { class CoverLayoutModel; }

// Sub-dialog launched from the video-export dialog's Font tab ("导出封面" /
// "Export Cover"). Hosts a LIVE WYSIWYG cover composer (CoverComposer.qml,
// embedded via QWidget::createWindowContainer): a full-bleed background layer
// (chart 曲绘 / custom image / transparent, blurred or crisp) plus the difficulty
// card as a draggable/scalable layer with snap-to-centre/edge guides. The same
// scene + normalized layout model drive the full-resolution export, so preview ==
// export. On accept the caller drives exportCover(outputDir).
class ExportCoverDialog : public QDialog
{
    Q_OBJECT

public:
    ExportCoverDialog(const IntroBannerSpec& banner, const QSize& initialSize, QWidget* parent = nullptr);
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
};
