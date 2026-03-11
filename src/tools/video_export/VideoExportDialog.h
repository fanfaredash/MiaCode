#pragma once

#include <QDialog>

#include "VideoExportController.h"

class QCheckBox;
class QComboBox;
class QLineEdit;
class QPushButton;
class PreviewCanvas;

class VideoExportDialog : public QDialog
{
    Q_OBJECT

public:
    VideoExportDialog(const VideoExportTask& baseTask, const PreviewCanvas* sourceCanvas, QWidget* parent = nullptr);

private:
    void browseOutputPath();
    void startExport();
    bool applyUiToTask(VideoExportTask* task, QString* errorMessage) const;

    VideoExportTask baseTask_;
    const PreviewCanvas* sourceCanvas_ = nullptr;
    QLineEdit* outputPathEdit_ = nullptr;
    QComboBox* resolutionCombo_ = nullptr;
    QCheckBox* showTimestampCheck_ = nullptr;
    QPushButton* exportButton_ = nullptr;
};

