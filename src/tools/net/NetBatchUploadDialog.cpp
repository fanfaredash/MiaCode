#include "NetBatchUploadDialog.h"

#include "DialogLocalization.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCloseEvent>
#include <QCheckBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <utility>
#include <algorithm>

namespace miacode::net {
namespace {

constexpr char kPreferencesAppSection[] = "app";
constexpr char kRememberCredentialsKey[] = "net_upload_remember_credentials";
constexpr char kStoredUsernameKey[] = "net_upload_username";
constexpr char kStoredPasswordKey[] = "net_upload_password";

struct StoredCredentials {
    QString username;
    QString password;
    bool remember = false;
};

StoredCredentials loadStoredCredentials()
{
    const QJsonObject app = UiText::loadPreferencesObject()
        .value(QLatin1String(kPreferencesAppSection))
        .toObject();
    StoredCredentials credentials;
    credentials.remember = app.value(QLatin1String(kRememberCredentialsKey)).toBool(false);
    if (credentials.remember) {
        credentials.username = app.value(QLatin1String(kStoredUsernameKey)).toString();
        credentials.password = app.value(QLatin1String(kStoredPasswordKey)).toString();
    }
    return credentials;
}

void saveStoredCredentials(const QString& username, const QString& password, bool remember)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QLatin1String(kPreferencesAppSection)).toObject();
    app.insert(QLatin1String(kRememberCredentialsKey), remember);
    if (remember) {
        app.insert(QLatin1String(kStoredUsernameKey), username);
        app.insert(QLatin1String(kStoredPasswordKey), password);
    } else {
        app.remove(QLatin1String(kStoredUsernameKey));
        app.remove(QLatin1String(kStoredPasswordKey));
    }
    root.insert(QLatin1String(kPreferencesAppSection), app);
    UiText::savePreferencesObject(root);
}

QString uploadDialogStyleSheet()
{
    const UiTheme::Colors& c = UiTheme::colors();
    return UiTheme::preferencesDialogStyleSheet()
        + UiTheme::darkAwareCheckBoxStyleSheet()
        + QStringLiteral(
            "QTableWidget { background: %1; color: %2; gridline-color: %3; border: 1px solid %3; }"
            "QHeaderView::section { background: %4; color: %2; border: 1px solid %3; padding: 4px 6px; }"
            "QLineEdit { background: %5; color: %2; border: 1px solid %3; border-radius: 6px; padding: 4px 7px; }")
              .arg(c.cardBg.name(QColor::HexRgb))
              .arg(c.textPrimary.name(QColor::HexRgb))
              .arg(c.border.name(QColor::HexRgb))
              .arg(c.panelBg.name(QColor::HexRgb))
              .arg(c.inputBg.name(QColor::HexRgb));
}

void polishButton(QPushButton* button)
{
    button->ensurePolished();
    button->setFixedHeight(qMax(button->sizeHint().height(), 30) + 4);
}

QString assetSummary(const NetUploadJob& job)
{
    QStringList assets = {
        QFileInfo(job.chartPath).fileName(),
        QFileInfo(job.backgroundPath).fileName(),
        QFileInfo(job.trackPath).fileName(),
    };
    if (!job.videoPath.isEmpty()) {
        assets.append(QFileInfo(job.videoPath).fileName());
    }
    return assets.join(QStringLiteral(" / "));
}

}  // namespace

NetBatchUploadDialog::NetBatchUploadDialog(QWidget* parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    buildUi();
}

void NetBatchUploadDialog::buildUi()
{
    setWindowTitle(UiText::text(QStringLiteral("net.upload_title")));
    setWindowFlags(
        Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
        | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    resize(980, 620);
    UiDialogs::configureDialogPreviewShortcuts(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* form = new QGridLayout;
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(8);
    form->setColumnStretch(1, 1);
    form->setColumnStretch(3, 1);

    usernameEdit_ = new QLineEdit(this);
    passwordEdit_ = new QLineEdit(this);
    passwordEdit_->setEchoMode(QLineEdit::Password);
    rememberCredentialsCheck_ = new QCheckBox(
        UiText::text(QStringLiteral("net.upload_remember_credentials")),
        this);
    const StoredCredentials storedCredentials = loadStoredCredentials();
    usernameEdit_->setText(storedCredentials.username);
    passwordEdit_->setText(storedCredentials.password);
    rememberCredentialsCheck_->setChecked(storedCredentials.remember);
    rootDirectoryEdit_ = new QLineEdit(this);
    browseButton_ = new QPushButton(UiText::text(QStringLiteral("net.browse")), this);
    scanButton_ = new QPushButton(UiText::text(QStringLiteral("net.upload_add")), this);

    form->addWidget(new QLabel(UiText::text(QStringLiteral("net.upload_username")), this), 0, 0);
    form->addWidget(usernameEdit_, 0, 1);
    form->addWidget(new QLabel(UiText::text(QStringLiteral("net.upload_password")), this), 0, 2);
    form->addWidget(passwordEdit_, 0, 3);
    form->addWidget(rememberCredentialsCheck_, 0, 4, 1, 2);
    form->addWidget(new QLabel(UiText::text(QStringLiteral("net.upload_root_directory")), this), 1, 0);
    form->addWidget(rootDirectoryEdit_, 1, 1, 1, 3);
    form->addWidget(browseButton_, 1, 4);
    form->addWidget(scanButton_, 1, 5);
    root->addLayout(form);

    table_ = new QTableWidget(this);
    table_->setColumnCount(4);
    table_->setHorizontalHeaderLabels({
        UiText::text(QStringLiteral("net.upload_chart_folder")),
        UiText::text(QStringLiteral("net.upload_assets")),
        UiText::text(QStringLiteral("net.upload_path")),
        UiText::text(QStringLiteral("net.status")),
    });
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table_->setColumnWidth(0, 180);
    table_->setColumnWidth(1, 280);
    table_->setColumnWidth(3, 170);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    root->addWidget(table_, 1);

    auto* bottom = new QGridLayout;
    summaryLabel_ = new QLabel(UiText::text(QStringLiteral("net.upload_no_folders_scanned")), this);
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    removeSelectedButton_ = new QPushButton(UiText::text(QStringLiteral("net.upload_remove_selected")), this);
    clearQueueButton_ = new QPushButton(UiText::text(QStringLiteral("net.upload_clear_queue")), this);
    uploadButton_ = new QPushButton(UiText::text(QStringLiteral("net.upload_queue")), this);
    closeButton_ = new QPushButton(UiText::text(QStringLiteral("action.close")), this);
    bottom->addWidget(summaryLabel_, 0, 0, 1, 2);
    bottom->addWidget(removeSelectedButton_, 0, 2);
    bottom->addWidget(clearQueueButton_, 0, 3);
    bottom->addWidget(progressBar_, 1, 0, 1, 2);
    bottom->addWidget(uploadButton_, 1, 2);
    bottom->addWidget(closeButton_, 1, 3);
    root->addLayout(bottom);

    uploadButton_->setEnabled(false);
    removeSelectedButton_->setEnabled(false);
    clearQueueButton_->setEnabled(false);
    applyThemeStyles();

    connect(browseButton_, &QPushButton::clicked, this, [this]() { chooseRootDirectory(); });
    connect(scanButton_, &QPushButton::clicked, this, [this]() { addFolders(); });
    connect(removeSelectedButton_, &QPushButton::clicked, this, [this]() { removeSelectedRows(); });
    connect(clearQueueButton_, &QPushButton::clicked, this, [this]() { clearQueue(); });
    connect(uploadButton_, &QPushButton::clicked, this, [this]() {
        if (busy_) {
            cancelRequested_ = true;
            summaryLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
            return;
        }
        uploadQueue();
    });
    connect(closeButton_, &QPushButton::clicked, this, [this]() {
        if (busy_) {
            cancelRequested_ = true;
            summaryLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
            return;
        }
        close();
    });
}

void NetBatchUploadDialog::applyThemeStyles()
{
    setPalette(UiTheme::applicationPalette());
    setStyleSheet(uploadDialogStyleSheet());
    for (QPushButton* button : {
             browseButton_, scanButton_, removeSelectedButton_, clearQueueButton_, uploadButton_, closeButton_}) {
        if (button != nullptr) {
            polishButton(button);
        }
    }
}

void NetBatchUploadDialog::closeEvent(QCloseEvent* event)
{
    if (busy_) {
        cancelRequested_ = true;
        summaryLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void NetBatchUploadDialog::chooseRootDirectory()
{
    const QString directory = QFileDialog::getExistingDirectory(
        this,
        UiText::text(QStringLiteral("net.upload_choose_root_directory")),
        rootDirectoryEdit_->text().trimmed());
    if (!directory.isEmpty()) {
        rootDirectoryEdit_->setText(QDir::toNativeSeparators(directory));
        addFolders();
    }
}

void NetBatchUploadDialog::addFolders()
{
    const QString rootDirectory = QDir::fromNativeSeparators(rootDirectoryEdit_->text().trimmed());
    if (rootDirectory.isEmpty() || !QDir(rootDirectory).exists()) {
        QMessageBox::warning(
            this,
            windowTitle(),
            UiText::text(QStringLiteral("net.upload_invalid_root_directory")));
        return;
    }
    const QList<NetUploadJob> scanned = scanNetUploadFolders(rootDirectory);
    const int added = appendUniqueNetUploadJobs(&jobs_, scanned);
    populateTable();
    summaryLabel_->setText(
        UiText::text(QStringLiteral("net.upload_added_1_total_2"))
            .arg(added)
            .arg(jobs_.size()));
}

void NetBatchUploadDialog::populateTable()
{
    table_->setRowCount(jobs_.size());
    for (int row = 0; row < jobs_.size(); ++row) {
        const NetUploadJob& job = jobs_.at(row);
        table_->setItem(row, 0, new QTableWidgetItem(job.displayName));
        table_->setItem(row, 1, new QTableWidgetItem(assetSummary(job)));
        table_->setItem(row, 2, new QTableWidgetItem(QDir::toNativeSeparators(job.directoryPath)));
        table_->setItem(row, 3, new QTableWidgetItem(UiText::text(QStringLiteral("net.upload_pending"))));
    }
    const bool hasJobs = !jobs_.isEmpty();
    uploadButton_->setEnabled(hasJobs);
    removeSelectedButton_->setEnabled(hasJobs);
    clearQueueButton_->setEnabled(hasJobs);
}

void NetBatchUploadDialog::removeSelectedRows()
{
    QModelIndexList rows = table_->selectionModel()->selectedRows();
    std::sort(rows.begin(), rows.end(), [](const QModelIndex& lhs, const QModelIndex& rhs) {
        return lhs.row() > rhs.row();
    });
    for (const QModelIndex& index : rows) {
        jobs_.removeAt(index.row());
    }
    populateTable();
    summaryLabel_->setText(UiText::text(QStringLiteral("net.upload_queue_total_1")).arg(jobs_.size()));
}

void NetBatchUploadDialog::clearQueue()
{
    jobs_.clear();
    populateTable();
    summaryLabel_->setText(UiText::text(QStringLiteral("net.upload_no_folders_scanned")));
}

void NetBatchUploadDialog::setRowStatus(int row, const QString& status)
{
    if (row >= 0 && row < table_->rowCount() && table_->item(row, 3) != nullptr) {
        table_->item(row, 3)->setText(status);
    }
}

bool NetBatchUploadDialog::showFailureDetails(const QString& summary, int failedCount)
{
    QMessageBox box(
        QMessageBox::Warning,
        windowTitle(),
        UiText::text(QStringLiteral("net.upload_failed_count_1")).arg(failedCount),
        QMessageBox::NoButton,
        this);
    QString informativeText = summary;
    if (!failureDetails_.isEmpty()) {
        informativeText += QStringLiteral("\n\n")
            + UiText::text(QStringLiteral("net.upload_failure_details_available"));
        box.setDetailedText(failureDetails_.join(QStringLiteral("\n\n====================\n\n")));
    }
    box.setInformativeText(informativeText);
    QPushButton* retryButton = box.addButton(
        UiText::text(QStringLiteral("net.upload_retry_failed")),
        QMessageBox::AcceptRole);
    QPushButton* cancelButton = box.addButton(
        UiText::text(QStringLiteral("action.cancel")),
        QMessageBox::RejectRole);
    box.setDefaultButton(retryButton);
    box.setEscapeButton(cancelButton);
    box.exec();
    return box.clickedButton() == retryButton;
}

void NetBatchUploadDialog::setBusy(bool busy)
{
    busy_ = busy;
    usernameEdit_->setEnabled(!busy);
    passwordEdit_->setEnabled(!busy);
    rootDirectoryEdit_->setEnabled(!busy);
    rememberCredentialsCheck_->setEnabled(!busy);
    browseButton_->setEnabled(!busy);
    scanButton_->setEnabled(!busy);
    removeSelectedButton_->setEnabled(!busy && !jobs_.isEmpty());
    clearQueueButton_->setEnabled(!busy && !jobs_.isEmpty());
    uploadButton_->setText(
        busy ? UiText::text(QStringLiteral("net.upload_cancel"))
             : UiText::text(QStringLiteral("net.upload_queue")));
    uploadButton_->setEnabled(!jobs_.isEmpty());
    closeButton_->setText(
        busy ? UiText::text(QStringLiteral("action.cancel"))
             : UiText::text(QStringLiteral("action.close")));
}

void NetBatchUploadDialog::uploadQueue()
{
    const QString username = usernameEdit_->text().trimmed();
    const QString password = passwordEdit_->text();
    if (username.isEmpty() || password.isEmpty()) {
        QMessageBox::warning(
            this,
            windowTitle(),
            UiText::text(QStringLiteral("net.upload_credentials_required")));
        return;
    }

    if (jobs_.isEmpty()) {
        QMessageBox::information(
            this,
            windowTitle(),
            UiText::text(QStringLiteral("net.upload_no_folders_scanned")));
        return;
    }

    sessionUsername_ = username;
    sessionPassword_ = password;
    saveStoredCredentials(username, password, rememberCredentialsCheck_->isChecked());
    if (!rememberCredentialsCheck_->isChecked()) {
        passwordEdit_->clear();
    }

    QSet<int> rows;
    for (int row = 0; row < jobs_.size(); ++row) {
        rows.insert(row);
    }
    startUpload(rows);
}

void NetBatchUploadDialog::startUpload(const QSet<int>& rows)
{
    if (rows.isEmpty() || sessionUsername_.isEmpty() || sessionPassword_.isEmpty()) {
        return;
    }

    activeAttemptRows_ = rows;
    succeededAttemptRows_.clear();
    failedAttemptRows_.clear();
    retryCandidateRows_.clear();
    completedSummary_.clear();
    failureDetails_.clear();
    for (int row = 0; row < jobs_.size(); ++row) {
        jobs_[row].selected = rows.contains(row);
        if (jobs_[row].selected) {
            setRowStatus(row, UiText::text(QStringLiteral("net.upload_pending")));
            if (table_->item(row, 3) != nullptr) {
                table_->item(row, 3)->setToolTip(QString());
            }
        }
    }

    NetBatchUploadRequest request;
    request.jobs = jobs_;
    request.username = sessionUsername_;
    request.password = sessionPassword_;
    cancelRequested_ = false;
    progressBar_->setRange(0, rows.size());
    progressBar_->setValue(0);
    setBusy(true);

    auto* worker = new NetBatchUploadWorker(std::move(request), &cancelRequested_);
    uploadThread_ = new QThread(this);
    worker->moveToThread(uploadThread_);
    connect(uploadThread_, &QThread::started, worker, &NetBatchUploadWorker::run);
    connect(worker, &NetBatchUploadWorker::rowStatus, this, [this](int row, const QString& status) {
        setRowStatus(row, status);
    });
    connect(worker, &NetBatchUploadWorker::rowOutcome, this,
        [this](int row, bool succeeded) {
            if (succeeded) {
                succeededAttemptRows_.insert(row);
                failedAttemptRows_.remove(row);
            } else {
                failedAttemptRows_.insert(row);
            }
        });
    connect(worker, &NetBatchUploadWorker::failureDetail, this,
        [this](int row, const QString& summary, const QString& details) {
            failureDetails_.append(summary + QLatin1Char('\n') + details);
            if (row >= 0 && row < table_->rowCount() && table_->item(row, 3) != nullptr) {
                table_->item(row, 3)->setToolTip(details);
            }
        });
    connect(worker, &NetBatchUploadWorker::progress, progressBar_, &QProgressBar::setValue);
    connect(worker, &NetBatchUploadWorker::summary, summaryLabel_, &QLabel::setText);
    connect(worker, &NetBatchUploadWorker::finished, this,
        [this](int succeeded, int failed, bool canceled, const QString& fatalError) {
            if (!fatalError.isEmpty()) {
                completedSummary_ = fatalError;
            } else if (canceled) {
                completedSummary_ = UiText::text(QStringLiteral("net.upload_canceled"));
            } else {
                completedSummary_ = UiText::text(QStringLiteral("net.upload_complete_1_succeeded_2"))
                    .arg(succeeded)
                    .arg(failed);
            }
            summaryLabel_->setText(completedSummary_);
            retryCandidateRows_ = failedAttemptRows_;
            if (!fatalError.isEmpty()) {
                retryCandidateRows_ = activeAttemptRows_;
                for (int row : succeededAttemptRows_) {
                    retryCandidateRows_.remove(row);
                }
                for (int row : retryCandidateRows_) {
                    if (!failedAttemptRows_.contains(row)) {
                        setRowStatus(
                            row,
                            UiText::text(QStringLiteral("net.upload_not_uploaded_batch_stopped")));
                    }
                }
            }
        });
    connect(worker, &NetBatchUploadWorker::finished, uploadThread_, &QThread::quit);
    connect(worker, &NetBatchUploadWorker::finished, worker, &QObject::deleteLater);
    connect(uploadThread_, &QThread::finished, this, [this]() {
        setBusy(false);
        uploadThread_ = nullptr;
        const QSet<int> retryRows = retryCandidateRows_;
        retryCandidateRows_.clear();
        const bool retry = !retryRows.isEmpty()
            && showFailureDetails(completedSummary_, retryRows.size());
        if (retry) {
            QTimer::singleShot(0, this, [this, retryRows]() { startUpload(retryRows); });
        } else {
            sessionUsername_.clear();
            sessionPassword_.clear();
        }
    });
    connect(uploadThread_, &QThread::finished, uploadThread_, &QObject::deleteLater);
    uploadThread_->start();
}

}  // namespace miacode::net
