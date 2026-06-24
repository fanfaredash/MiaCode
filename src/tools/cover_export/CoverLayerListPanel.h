#pragma once

#include <QWidget>

class QEvent;
class QListView;
class QObject;
class QPoint;
class QPushButton;

namespace miacode::cover_export {

class CoverLayerListModel;
class CoverStudioPanel;

class CoverLayerListPanel : public QWidget
{
    Q_OBJECT

public:
    explicit CoverLayerListPanel(CoverStudioPanel* studio, QWidget* parent = nullptr);

protected:
    // Delete / Backspace on the focused list → delete the active layer (which then
    // auto-selects the neighbour), so the key can be held to clear frames.
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void syncSelectionFromStudio(const QString& key);
    void showContextMenu(const QPoint& pos);

    CoverStudioPanel* studio_ = nullptr;
    CoverLayerListModel* model_ = nullptr;
    QListView* view_ = nullptr;
};

}  // namespace miacode::cover_export
