#pragma once

#include "NetClient.h"

#include <QDialog>
#include <QList>

#include <atomic>

class QCheckBox;
class QDateEdit;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QThread;
class QCloseEvent;

namespace miacode::net {

class NetBatchDownloadDialog : public QDialog {
public:
    explicit NetBatchDownloadDialog(QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void setBusy(bool busy);
    void queryCharts();
    void downloadSelected();
    void selectAllRows(bool selected);
    void chooseOutputDirectory();
    void populateTable(const QList<NetChartSummary>& charts);
    void setRowStatus(int row, const QString& status);
    void appendLog(const QString& message);
    void toggleLogVisible();

    NetClient client_;
    QList<NetDownloadJob> jobs_;
    std::atomic_bool cancelRequested_ = false;
    bool busy_ = false;
    bool logVisible_ = false;
    QThread* downloadThread_ = nullptr;

    QLineEdit* usernameEdit_ = nullptr;
    QLineEdit* tagEdit_ = nullptr;
    QLineEdit* titleEdit_ = nullptr;
    QDateEdit* startDateEdit_ = nullptr;
    QDateEdit* endDateEdit_ = nullptr;
    QLineEdit* outputDirEdit_ = nullptr;
    QCheckBox* fuzzyMatchCheck_ = nullptr;
    QCheckBox* zipAfterDownloadCheck_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QPlainTextEdit* logEdit_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* queryButton_ = nullptr;
    QPushButton* selectAllButton_ = nullptr;
    QPushButton* clearSelectionButton_ = nullptr;
    QPushButton* downloadButton_ = nullptr;
    QPushButton* logButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
};

}  // namespace miacode::net
