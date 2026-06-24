#pragma once

#include <QMainWindow>
#include <QSize>

#include "tools/cover_export/CoverComposerView.h"

struct VideoExportTask;

namespace miacode::cover_export {

class CoverStudioPanel;

class CoverStudioWindow : public QMainWindow
{
    Q_OBJECT

public:
    CoverStudioWindow(const VideoExportTask& task,
                      const QSize& initialSize,
                      const QString& outputDirectory,
                      QWidget* parent = nullptr);

private:
    void fitToScreen(QWidget* parent);
    void exportNow();

    CoverStudioPanel* studio_ = nullptr;
    QString outputDirectory_;
};

}  // namespace miacode::cover_export
