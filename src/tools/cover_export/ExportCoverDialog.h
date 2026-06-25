#pragma once

#include <QDialog>
#include <QSize>
#include <QString>

#include "tools/cover_export/CoverComposerView.h"

struct VideoExportTask;

namespace miacode::cover_export {
class CoverStudioPanel;
}

class ExportCoverDialog : public QDialog
{
    Q_OBJECT

public:
    ExportCoverDialog(const VideoExportTask& task, const QSize& initialSize, QWidget* parent = nullptr);

    miacode::cover_export::CoverExportResult exportCover(const QString& outputDirectory);

private:
    miacode::cover_export::CoverStudioPanel* studio_ = nullptr;
};
