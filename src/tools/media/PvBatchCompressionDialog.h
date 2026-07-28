#pragma once

#include "PvBatchCompressionScanner.h"

#include <QDialog>

#include <atomic>

class QCloseEvent;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QThread;

namespace miacode::media {

class PvBatchCompressionDialog : public QDialog {
public:
    explicit PvBatchCompressionDialog(QWidget* parent = nullptr);

    void applyThemeStyles();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void chooseDirectory();
    void addFolder();
    void populateTable();
    void removeSelectedRows();
    void clearQueue();
    void startCompression();
    void setBusy(bool busy);
    void setRowStatus(int row, const QString& status);

    QList<PvCompressionJob> jobs_;
    std::atomic_bool cancelRequested_ = false;
    bool busy_ = false;
    QThread* workerThread_ = nullptr;

    QLineEdit* directoryEdit_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QPushButton* removeSelectedButton_ = nullptr;
    QPushButton* clearQueueButton_ = nullptr;
    QPushButton* compressButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
};

}  // namespace miacode::media
