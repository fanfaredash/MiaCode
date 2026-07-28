#pragma once

#include "NetBatchUploadWorker.h"

#include <QDialog>
#include <QList>
#include <QSet>
#include <QStringList>

#include <atomic>

class QCloseEvent;
class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QTableWidget;
class QThread;

namespace miacode::net {

class NetBatchUploadDialog : public QDialog {
public:
    explicit NetBatchUploadDialog(QWidget* parent = nullptr);

    void applyThemeStyles();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    void buildUi();
    void chooseRootDirectory();
    void addFolders();
    void populateTable();
    void removeSelectedRows();
    void clearQueue();
    void uploadQueue();
    void startUpload(const QSet<int>& rows);
    void setBusy(bool busy);
    void setRowStatus(int row, const QString& status);
    bool showFailureDetails(const QString& summary, int failedCount);

    QList<NetUploadJob> jobs_;
    std::atomic_bool cancelRequested_ = false;
    bool busy_ = false;
    QThread* uploadThread_ = nullptr;
    QStringList failureDetails_;
    QSet<int> activeAttemptRows_;
    QSet<int> succeededAttemptRows_;
    QSet<int> failedAttemptRows_;
    QSet<int> retryCandidateRows_;
    QString completedSummary_;
    QString sessionUsername_;
    QString sessionPassword_;

    QLineEdit* usernameEdit_ = nullptr;
    QLineEdit* passwordEdit_ = nullptr;
    QLineEdit* rootDirectoryEdit_ = nullptr;
    QCheckBox* rememberCredentialsCheck_ = nullptr;
    QTableWidget* table_ = nullptr;
    QLabel* summaryLabel_ = nullptr;
    QProgressBar* progressBar_ = nullptr;
    QPushButton* browseButton_ = nullptr;
    QPushButton* scanButton_ = nullptr;
    QPushButton* removeSelectedButton_ = nullptr;
    QPushButton* clearQueueButton_ = nullptr;
    QPushButton* uploadButton_ = nullptr;
    QPushButton* closeButton_ = nullptr;
};

}  // namespace miacode::net
