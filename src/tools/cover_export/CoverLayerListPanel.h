#pragma once

#include <QWidget>

class QListView;
class QPushButton;

namespace miacode::cover_export {

class CoverLayerListModel;
class CoverStudioPanel;

class CoverLayerListPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CoverLayerListPanel(CoverStudioPanel* studio, QWidget* parent = nullptr);

private:
    void syncSelectionFromStudio(const QString& key);

    CoverStudioPanel* studio_ = nullptr;
    CoverLayerListModel* model_ = nullptr;
    QListView* view_ = nullptr;
};

}  // namespace miacode::cover_export
