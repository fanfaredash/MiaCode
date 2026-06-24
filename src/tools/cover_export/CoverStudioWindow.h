#pragma once

#include <QMainWindow>
#include <QSize>

#include "tools/cover_export/CoverComposerView.h"

class QMenu;
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
    // 布局 menu actions.
    void confirmAndReset();
    void rebuildRecentMenu(QMenu* menu);

    CoverStudioPanel* studio_ = nullptr;
    QString outputDirectory_;
};

}  // namespace miacode::cover_export
