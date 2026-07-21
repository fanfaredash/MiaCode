#pragma once

#include <QMainWindow>
#include <QSize>

#include "tools/cover_export/CoverComposerView.h"

class QMenu;
class QObject;
class QEvent;
class QCloseEvent;
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
    ~CoverStudioWindow() override;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    // Persist the composition while every child widget is still alive, before
    // WA_DeleteOnClose tears the widget tree down (the panel's option groups are
    // reparented into this window, so a destructor-time save would read freed widgets).
    void closeEvent(QCloseEvent* event) override;

private:
    void fitToScreen(QWidget* parent);
    void exportNow();
    // 布局 menu actions.
    void confirmAndReset();
    void saveCurrentAsPreset();
    void managePresets();
    void rebuildPresetMenu(QMenu* menu);
    void rebuildRecentMenu(QMenu* menu);

    CoverStudioPanel* studio_ = nullptr;
    QString outputDirectory_;
};

}  // namespace miacode::cover_export
