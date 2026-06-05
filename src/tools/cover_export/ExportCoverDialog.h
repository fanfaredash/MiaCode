#pragma once

#include <QDialog>
#include <QSize>

class QCheckBox;
class QComboBox;

// Sub-dialog launched from the video-export dialog's Font tab ("导出封面" /
// "Export Cover"). Collects the cover-image size and a transparent-background
// toggle, then hands them back to the caller which drives IntroCoverExporter.
//
// The size choices mirror the video-export resolution presets but are tracked
// independently; the initial selection is seeded from the current video size.
class ExportCoverDialog : public QDialog
{
    Q_OBJECT

public:
    explicit ExportCoverDialog(const QSize& initialSize, QWidget* parent = nullptr);

    QSize selectedSize() const { return selectedSize_; }
    bool transparentBackground() const { return transparentBackground_; }

private:
    void accept() override;

    QComboBox* sizeCombo_ = nullptr;
    QCheckBox* transparentCheck_ = nullptr;
    QSize selectedSize_;
    bool transparentBackground_ = false;
};
