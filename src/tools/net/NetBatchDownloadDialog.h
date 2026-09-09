#pragma once

#include "NetClient.h"

#include <QDialog>
#include <QList>

#include <atomic>
#include <functional>

class QCheckBox;
class QComboBox;
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

struct NetBatchDownloadRequest;

class NetBatchDownloadDialog : public QDialog {
public:
    using OnlinePreviewHandler = std::function<bool(const QString& chartPath)>;

    explicit NetBatchDownloadDialog(QWidget* parent = nullptr, OnlinePreviewHandler onlinePreviewHandler = {});

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void setBusy(bool busy);
    void queryCharts();
    void testConnection();
    void downloadSelected();
    void onlinePreview(const QString& chartId);
    void startDownloadRequest(NetBatchDownloadRequest request, int workCount, const QString& previewChartPath = {});
    void selectAllRows(bool selected);
    void chooseOutputDirectory();
    void populateTable(const QList<NetChartSummary>& charts);
    void rebuildTable();
    void syncSelectionsFromTable();
    void applyCurrentSort();
    void setChartStatus(const QString& chartId, const QString& status);
    void appendLog(const QString& message);
    void toggleLogVisible();

    NetClient client_;
    OnlinePreviewHandler onlinePreviewHandler_;
    QList<NetDownloadJob> jobs_;
    std::atomic_bool cancelRequested_ = false;
    bool busy_ = false;
    bool connectionProbeActive_ = false;
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
    QCheckBox* downloadPvCheck_ = nullptr;
    QComboBox* sortCombo_ = nullptr;
    QLabel* networkStatusLabel_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QPlainTextEdit* logEdit_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* queryButton_ = nullptr;
    QPushButton* networkTestButton_ = nullptr;
    QPushButton* selectAllButton_ = nullptr;
    QPushButton* clearSelectionButton_ = nullptr;
    QPushButton* downloadButton_ = nullptr;
    QPushButton* logButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
};

}  // namespace miacode::net
