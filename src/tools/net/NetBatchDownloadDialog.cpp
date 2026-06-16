#include "NetBatchDownloadDialog.h"

#include "DialogLocalization.h"
#include "NetBatchDownloadWorker.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QCloseEvent>
#include <QDateEdit>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPaintEvent>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTextCharFormat>
#include <QThread>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <utility>

namespace miacode::net {
namespace {

class NetCalendarBorderOverlay : public QWidget {
public:
    explicit NetCalendarBorderOverlay(QWidget* parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        Q_UNUSED(event);

        const UiTheme::Colors& c = UiTheme::colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(QPen(c.borderStrong, 1.2));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.6, 0.6, -0.6, -0.6), 8.0, 8.0);
    }
};

class NetCalendarWidget : public QCalendarWidget {
public:
    explicit NetCalendarWidget(QWidget* parent = nullptr)
        : QCalendarWidget(parent)
        , borderOverlay_(new NetCalendarBorderOverlay(this))
    {
        setContentsMargins(1, 1, 1, 1);
    }

protected:
    void resizeEvent(QResizeEvent* event) override
    {
        QCalendarWidget::resizeEvent(event);
        borderOverlay_->setGeometry(rect());
        borderOverlay_->raise();
    }

private:
    NetCalendarBorderOverlay* borderOverlay_ = nullptr;
};

class NetDateEdit : public QDateEdit {
public:
    explicit NetDateEdit(const QDate& date, QWidget* parent = nullptr)
        : QDateEdit(date, parent)
    {
        setCalendarPopup(true);
        setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
        setKeyboardTracking(false);
        setCorrectionMode(QAbstractSpinBox::CorrectToNearestValue);
    }

protected:
    void wheelEvent(QWheelEvent* event) override
    {
        event->ignore();
    }

    void paintEvent(QPaintEvent* event) override
    {
        QDateEdit::paintEvent(event);

        const UiTheme::Colors& c = UiTheme::colors();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        QPen pen(isEnabled() ? c.textSecondary : c.textMuted, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
        painter.setPen(pen);

        const int centerX = width() - 12;
        const int centerY = height() / 2 + 1;
        painter.drawLine(QPoint(centerX - 4, centerY - 2), QPoint(centerX, centerY + 2));
        painter.drawLine(QPoint(centerX, centerY + 2), QPoint(centerX + 4, centerY - 2));
    }

    void stepBy(int steps) override
    {
        Q_UNUSED(steps);
    }

    StepEnabled stepEnabled() const override
    {
        return StepNone;
    }
};

QString trText(const char* zh, const char* en)
{
    return UiText::isChineseUi() ? QString::fromUtf8(zh) : QString::fromLatin1(en);
}

QString logTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

QString displayTimestamp(const QDateTime& utc)
{
    return utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString normalizedTagKeyword(const QString& tagKeyword)
{
    QString normalized = tagKeyword.trimmed();
    if (normalized.startsWith(QStringLiteral("tag:"), Qt::CaseInsensitive)) {
        normalized = normalized.mid(4).trimmed();
    }
    return normalized;
}

bool chartMatchesUserKeyword(const NetChartSummary& chart, const QString& username, Qt::CaseSensitivity caseSensitivity)
{
    const QString normalized = username.trimmed();
    if (normalized.isEmpty()) {
        return true;
    }
    return chart.uploader.compare(normalized, caseSensitivity) == 0;
}

bool chartMatchesTagKeyword(const NetChartSummary& chart, const QString& tagKeyword, Qt::CaseSensitivity caseSensitivity)
{
    const QString normalized = normalizedTagKeyword(tagKeyword);
    if (normalized.isEmpty()) {
        return true;
    }
    for (const QString& tag : chart.publicTags) {
        if (tag.contains(normalized, caseSensitivity)) {
            return true;
        }
    }
    return false;
}

QString netDialogStyleSheet()
{
    const UiTheme::Colors& c = UiTheme::colors();
    return UiTheme::preferencesDialogStyleSheet()
        + UiTheme::darkAwareCheckBoxStyleSheet()
        + QStringLiteral(
            "QTableWidget { background: %1; color: %2; gridline-color: %3; border: 1px solid %3; }"
            "QHeaderView::section { background: %4; color: %2; border: 1px solid %3; padding: 4px 6px; }"
            "QLineEdit, QDateEdit, QPlainTextEdit { background: %5; color: %2; border: 1px solid %3; border-radius: 6px; padding: 3px 6px; }"
            "QDateEdit { padding-right: 24px; }"
            "QDateEdit::drop-down { width: 22px; border: none; border-left: 1px solid %3; }"
            "QDateEdit::down-arrow { image: none; width: 12px; height: 12px; }"
            "QCalendarWidget { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; }"
            "QCalendarWidget QWidget#qt_calendar_navigationbar { background: %4; border: none; border-top-left-radius: 8px; border-top-right-radius: 8px; }"
            "QCalendarWidget QToolButton { background: transparent; color: %2; border: none; border-radius: 6px; margin: 3px; padding: 4px 8px; font-weight: 600; }"
            "QCalendarWidget QToolButton:hover { background: %6; }"
            "QCalendarWidget QToolButton:pressed { background: %7; color: %8; }"
            "QCalendarWidget QSpinBox { background: %5; color: %2; border: 1px solid %3; border-radius: 5px; padding: 2px 6px; }"
            "QCalendarWidget QAbstractItemView { background: %1; color: %2; selection-background-color: %7; selection-color: %8; outline: 0; }"
            "QCalendarWidget QAbstractItemView:disabled { color: %9; }"
            "QCalendarWidget QAbstractItemView:enabled { alternate-background-color: %1; }"
        )
              .arg(c.cardBg.name(QColor::HexRgb))
              .arg(c.textPrimary.name(QColor::HexRgb))
              .arg(c.border.name(QColor::HexRgb))
              .arg(c.panelBg.name(QColor::HexRgb))
              .arg(c.inputBg.name(QColor::HexRgb))
              .arg(c.menuHoverBg.name(QColor::HexRgb))
              .arg(c.accent.name(QColor::HexRgb))
              .arg(c.accentText.name(QColor::HexRgb))
              .arg(c.textMuted.name(QColor::HexRgb));
}

QCalendarWidget* createNetCalendar(QWidget* parent)
{
    auto* calendar = new NetCalendarWidget(parent);
    calendar->setGridVisible(false);
    calendar->setVerticalHeaderFormat(QCalendarWidget::NoVerticalHeader);
    calendar->setHorizontalHeaderFormat(QCalendarWidget::ShortDayNames);

    const UiTheme::Colors& c = UiTheme::colors();
    QPalette palette = calendar->palette();
    palette.setColor(QPalette::Window, c.cardBg);
    palette.setColor(QPalette::Base, c.cardBg);
    palette.setColor(QPalette::AlternateBase, c.cardBg);
    palette.setColor(QPalette::Text, c.textPrimary);
    palette.setColor(QPalette::WindowText, c.textPrimary);
    palette.setColor(QPalette::ButtonText, c.textPrimary);
    palette.setColor(QPalette::Disabled, QPalette::Text, c.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::WindowText, c.textMuted);
    palette.setColor(QPalette::Disabled, QPalette::ButtonText, c.textMuted);
    palette.setColor(QPalette::Highlight, c.accent);
    palette.setColor(QPalette::HighlightedText, c.accentText);
    calendar->setPalette(palette);

    QTextCharFormat weekdayFormat;
    weekdayFormat.setForeground(c.textSecondary);
    for (Qt::DayOfWeek day :
         {Qt::Monday, Qt::Tuesday, Qt::Wednesday, Qt::Thursday, Qt::Friday, Qt::Saturday, Qt::Sunday}) {
        calendar->setWeekdayTextFormat(day, weekdayFormat);
    }
    QTextCharFormat headerFormat;
    headerFormat.setForeground(c.textMuted);
    calendar->setHeaderTextFormat(headerFormat);
    return calendar;
}

}  // namespace

NetBatchDownloadDialog::NetBatchDownloadDialog(QWidget* parent)
    : QDialog(parent)
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    buildUi();
}

void NetBatchDownloadDialog::buildUi()
{
    setWindowTitle(trText("Net 批量下载", "Net Batch Download"));
    setWindowFlags(Qt::Window | Qt::WindowTitleHint | Qt::WindowSystemMenuHint | Qt::WindowMinimizeButtonHint | Qt::WindowCloseButtonHint);
    resize(1040, 640);
    setPalette(UiTheme::applicationPalette());
    setStyleSheet(netDialogStyleSheet());
    UiDialogs::configureDialogPreviewShortcuts(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* form = new QGridLayout;
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(8);
    form->setColumnStretch(1, 1);
    form->setColumnStretch(3, 1);
    form->setColumnStretch(9, 1);

    usernameEdit_ = new QLineEdit(this);
    tagEdit_ = new QLineEdit(this);
    startDateEdit_ = new NetDateEdit(QDate::currentDate().addMonths(-1), this);
    startDateEdit_->setCalendarWidget(createNetCalendar(startDateEdit_));
    endDateEdit_ = new NetDateEdit(QDate::currentDate(), this);
    endDateEdit_->setCalendarWidget(createNetCalendar(endDateEdit_));
    outputDirEdit_ = new QLineEdit(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation), this);
    auto* browseButton = new QPushButton(trText("浏览...", "Browse..."), this);
    fuzzyMatchCheck_ = new QCheckBox(trText("模糊大小写匹配", "Fuzzy case-insensitive match"), this);
    fuzzyMatchCheck_->setChecked(true);
    zipAfterDownloadCheck_ = new QCheckBox(trText("成功后额外生成 ZIP", "Also create ZIP after success"), this);
    zipAfterDownloadCheck_->setChecked(false);
    queryButton_ = new QPushButton(trText("查询", "Query"), this);

    form->addWidget(new QLabel(trText("用户 ID", "User ID"), this), 0, 0);
    form->addWidget(usernameEdit_, 0, 1);
    form->addWidget(new QLabel(QStringLiteral("Tag"), this), 0, 2);
    form->addWidget(tagEdit_, 0, 3);
    form->addWidget(new QLabel(trText("开始", "Start"), this), 0, 4);
    form->addWidget(startDateEdit_, 0, 5);
    form->addWidget(new QLabel(trText("结束", "End"), this), 0, 6);
    form->addWidget(endDateEdit_, 0, 7);
    form->addWidget(queryButton_, 0, 8);
    form->addWidget(fuzzyMatchCheck_, 0, 9);
    form->addWidget(new QLabel(trText("输出目录", "Output Directory"), this), 1, 0);
    form->addWidget(outputDirEdit_, 1, 1, 1, 7);
    form->addWidget(browseButton, 1, 8);
    form->addWidget(zipAfterDownloadCheck_, 1, 9);
    root->addLayout(form);

    table_ = new QTableWidget(this);
    table_->setColumnCount(8);
    table_->setHorizontalHeaderLabels({
        trText("选择", "Select"),
        trText("标题", "Title"),
        trText("曲师", "Artist"),
        trText("谱师", "Designer"),
        trText("等级", "Levels"),
        trText("上传时间", "Uploaded"),
        QStringLiteral("ID"),
        trText("状态", "Status"),
    });
    table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(6, QHeaderView::Interactive);
    table_->horizontalHeader()->setSectionResizeMode(7, QHeaderView::Stretch);
    table_->setColumnWidth(1, 300);
    table_->setColumnWidth(2, 150);
    table_->setColumnWidth(3, 150);
    table_->setColumnWidth(5, 150);
    table_->setColumnWidth(6, 90);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    root->addWidget(table_, 1);

    auto* bottom = new QGridLayout;
    summaryLabel_ = new QLabel(trText("输入用户 ID 或 Tag，再选择日期范围查询。", "Enter a user ID or tag, choose a date range, then query."), this);
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    selectAllButton_ = new QPushButton(trText("全选", "Select All"), this);
    clearSelectionButton_ = new QPushButton(trText("取消全选", "Clear Selection"), this);
    downloadButton_ = new QPushButton(trText("下载选中", "Download Selected"), this);
    logButton_ = new QPushButton(trText("查看日志", "Show Log"), this);
    closeButton_ = new QPushButton(trText("关闭", "Close"), this);
    bottom->addWidget(summaryLabel_, 0, 0, 1, 3);
    bottom->addWidget(progressBar_, 1, 0, 1, 3);
    bottom->addWidget(selectAllButton_, 0, 3);
    bottom->addWidget(clearSelectionButton_, 0, 4);
    bottom->addWidget(logButton_, 0, 5);
    bottom->addWidget(downloadButton_, 1, 3, 1, 2);
    bottom->addWidget(closeButton_, 1, 5);
    root->addLayout(bottom);

    logEdit_ = new QPlainTextEdit(this);
    logEdit_->setReadOnly(true);
    logEdit_->setMaximumBlockCount(1200);
    logEdit_->setVisible(false);
    logEdit_->setPlaceholderText(trText("查询和下载诊断日志会显示在这里。", "Query and download diagnostics will appear here."));
    root->addWidget(logEdit_);

    connect(browseButton, &QPushButton::clicked, this, [this]() { chooseOutputDirectory(); });
    connect(queryButton_, &QPushButton::clicked, this, [this]() { queryCharts(); });
    connect(selectAllButton_, &QPushButton::clicked, this, [this]() { selectAllRows(true); });
    connect(clearSelectionButton_, &QPushButton::clicked, this, [this]() { selectAllRows(false); });
    connect(logButton_, &QPushButton::clicked, this, [this]() { toggleLogVisible(); });
    connect(downloadButton_, &QPushButton::clicked, this, [this]() {
        if (busy_) {
            cancelRequested_ = true;
            summaryLabel_->setText(trText("正在取消...", "Canceling..."));
            return;
        }
        downloadSelected();
    });
    connect(closeButton_, &QPushButton::clicked, this, [this]() {
        if (busy_) {
            cancelRequested_ = true;
            summaryLabel_->setText(trText("正在取消...", "Canceling..."));
            return;
        }
        close();
    });
}

void NetBatchDownloadDialog::closeEvent(QCloseEvent* event)
{
    if (busy_) {
        cancelRequested_ = true;
        if (summaryLabel_ != nullptr) {
            summaryLabel_->setText(trText("正在取消...", "Canceling..."));
        }
        event->ignore();
        return;
    }
    QDialog::closeEvent(event);
}

void NetBatchDownloadDialog::setBusy(bool busy)
{
    busy_ = busy;
    usernameEdit_->setEnabled(!busy);
    tagEdit_->setEnabled(!busy);
    startDateEdit_->setEnabled(!busy);
    endDateEdit_->setEnabled(!busy);
    outputDirEdit_->setEnabled(!busy);
    fuzzyMatchCheck_->setEnabled(!busy);
    zipAfterDownloadCheck_->setEnabled(!busy);
    queryButton_->setEnabled(!busy);
    selectAllButton_->setEnabled(!busy);
    clearSelectionButton_->setEnabled(!busy);
    closeButton_->setText(busy ? trText("取消", "Cancel") : trText("关闭", "Close"));
    downloadButton_->setText(busy ? trText("取消下载", "Cancel Download") : trText("下载选中", "Download Selected"));
    downloadButton_->setEnabled(!jobs_.isEmpty());
}

void NetBatchDownloadDialog::appendLog(const QString& message)
{
    if (logEdit_ == nullptr) {
        return;
    }
    logEdit_->appendPlainText(QStringLiteral("[%1] %2").arg(logTimestamp(), message));
    if (!logVisible_) {
        logButton_->setText(trText("查看日志 *", "Show Log *"));
    }
    qApp->processEvents(QEventLoop::AllEvents, 20);
}

void NetBatchDownloadDialog::toggleLogVisible()
{
    logVisible_ = !logVisible_;
    const int currentWidth = width();
    logEdit_->setVisible(logVisible_);
    logButton_->setText(logVisible_ ? trText("隐藏日志", "Hide Log") : trText("查看日志", "Show Log"));
    if (logVisible_) {
        resize(currentWidth, qMax(height(), 760));
    }
}

void NetBatchDownloadDialog::chooseOutputDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        trText("选择输出目录", "Choose Output Directory"),
        outputDirEdit_->text().trimmed());
    if (!dir.isEmpty()) {
        outputDirEdit_->setText(QDir::toNativeSeparators(dir));
    }
}

void NetBatchDownloadDialog::queryCharts()
{
    const QString username = usernameEdit_->text().trimmed();
    const QString tag = tagEdit_->text().trimmed();
    const bool fuzzyMatch = fuzzyMatchCheck_->isChecked();
    const Qt::CaseSensitivity caseSensitivity = fuzzyMatch ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (username.isEmpty() && tag.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), trText("请输入用户 ID 或 Tag。", "Please enter a user ID or tag."));
        return;
    }

    setBusy(true);
    cancelRequested_ = false;
    progressBar_->setRange(0, 0);
    summaryLabel_->setText(trText("正在查询 Net...", "Querying Net..."));
    appendLog(trText("开始查询：用户=%1，tag=%2，日期=%3..%4，模糊大小写=%5", "Start query: user=%1, tag=%2, dates=%3..%4, fuzzy case=%5")
                  .arg(username)
                  .arg(tag.isEmpty() ? QStringLiteral("-") : tag)
                  .arg(startDateEdit_->date().toString(Qt::ISODate))
                  .arg(endDateEdit_->date().toString(Qt::ISODate))
                  .arg(fuzzyMatch ? QStringLiteral("yes") : QStringLiteral("no")));
    qApp->processEvents();

    QElapsedTimer elapsed;
    elapsed.start();
    QString error;
    NetQueryOptions options;
    options.fuzzyCaseInsensitive = fuzzyMatch;
    const QList<NetChartSummary> queriedCharts = client_.queryCharts(username, tag, options, &error);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    setBusy(false);

    if (!error.isEmpty()) {
        appendLog(trText("查询失败（%1 ms）：%2", "Query failed (%1 ms): %2").arg(elapsed.elapsed()).arg(error));
        QMessageBox::critical(this, windowTitle(), error);
        summaryLabel_->setText(trText("查询失败。", "Query failed."));
        return;
    }

    const QList<NetChartSummary> dateFiltered =
        filterChartsByLocalDateRange(queriedCharts, startDateEdit_->date(), endDateEdit_->date());
    QList<NetChartSummary> filtered;
    filtered.reserve(dateFiltered.size());
    for (const NetChartSummary& chart : dateFiltered) {
        if (chartMatchesUserKeyword(chart, username, caseSensitivity)
            && chartMatchesTagKeyword(chart, tag, caseSensitivity)) {
            filtered.append(chart);
        }
    }
    populateTable(filtered);
    summaryLabel_->setText(
        trText("找到 %1 个谱面（查询返回 %2 个）。", "Found %1 chart(s) from %2 returned chart(s).")
            .arg(filtered.size())
            .arg(queriedCharts.size()));
    appendLog(trText("查询完成（%1 ms）：接口返回 %2，日期筛选后 %3，本地 ID/Tag 筛选后 %4。", "Query complete (%1 ms): API returned %2, date filter kept %3, local ID/tag filter kept %4.")
                  .arg(elapsed.elapsed())
                  .arg(queriedCharts.size())
                  .arg(dateFiltered.size())
                  .arg(filtered.size()));
}

void NetBatchDownloadDialog::populateTable(const QList<NetChartSummary>& charts)
{
    jobs_.clear();
    table_->setRowCount(charts.size());
    for (int row = 0; row < charts.size(); ++row) {
        NetDownloadJob job;
        job.chart = charts.at(row);
        job.selected = true;
        job.status = trText("待下载", "Pending");
        jobs_.append(job);

        auto* check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        check->setCheckState(Qt::Checked);
        table_->setItem(row, 0, check);
        table_->setItem(row, 1, new QTableWidgetItem(job.chart.title));
        table_->setItem(row, 2, new QTableWidgetItem(job.chart.artist));
        table_->setItem(row, 3, new QTableWidgetItem(job.chart.designer));
        table_->setItem(row, 4, new QTableWidgetItem(formatLevels(job.chart.levels)));
        table_->setItem(row, 5, new QTableWidgetItem(displayTimestamp(job.chart.timestampUtc)));
        table_->setItem(row, 6, new QTableWidgetItem(job.chart.id));
        table_->setItem(row, 7, new QTableWidgetItem(job.status));
    }
    downloadButton_->setEnabled(!jobs_.isEmpty());
}

void NetBatchDownloadDialog::selectAllRows(bool selected)
{
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (QTableWidgetItem* item = table_->item(row, 0); item != nullptr) {
            item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void NetBatchDownloadDialog::setRowStatus(int row, const QString& status)
{
    if (row >= 0 && row < table_->rowCount() && table_->item(row, 7) != nullptr) {
        table_->item(row, 7)->setText(status);
    }
    qApp->processEvents(QEventLoop::AllEvents, 50);
}

void NetBatchDownloadDialog::downloadSelected()
{
    const QString outputDir = QDir::fromNativeSeparators(outputDirEdit_->text().trimmed());
    if (outputDir.isEmpty() || !QDir().mkpath(outputDir)) {
        QMessageBox::warning(this, windowTitle(), trText("请选择有效的输出目录。", "Please choose a valid output directory."));
        return;
    }

    int selectedCount = 0;
    for (int row = 0; row < jobs_.size(); ++row) {
        jobs_[row].selected = table_->item(row, 0) != nullptr && table_->item(row, 0)->checkState() == Qt::Checked;
        if (jobs_[row].selected) {
            ++selectedCount;
        }
    }
    if (selectedCount <= 0) {
        QMessageBox::information(this, windowTitle(), trText("没有选中的谱面。", "No charts are selected."));
        return;
    }

    setBusy(true);
    cancelRequested_ = false;
    progressBar_->setRange(0, selectedCount);
    progressBar_->setValue(0);
    NetBatchDownloadRequest request;
    request.jobs = jobs_;
    request.outputDirectory = outputDir;
    request.createZip = zipAfterDownloadCheck_->isChecked();
    appendLog(trText("开始下载队列：选中 %1，输出 %2，额外 ZIP=%3", "Start download queue: selected=%1, output=%2, extra ZIP=%3")
                  .arg(selectedCount)
                  .arg(outputDir)
                  .arg(request.createZip ? QStringLiteral("yes") : QStringLiteral("no")));

    auto* worker = new NetBatchDownloadWorker(std::move(request), &cancelRequested_);
    downloadThread_ = new QThread(this);
    worker->moveToThread(downloadThread_);

    connect(downloadThread_, &QThread::started, worker, &NetBatchDownloadWorker::run);
    connect(worker, &NetBatchDownloadWorker::rowStatus, this, [this](int row, const QString& status) {
        setRowStatus(row, status);
    });
    connect(worker, &NetBatchDownloadWorker::progress, this, [this](int completed) {
        progressBar_->setValue(completed);
    });
    connect(worker, &NetBatchDownloadWorker::summary, this, [this](const QString& message) {
        summaryLabel_->setText(message);
    });
    connect(worker, &NetBatchDownloadWorker::log, this, [this](const QString& message) {
        appendLog(message);
    });
    connect(worker, &NetBatchDownloadWorker::finished, this, [this](int succeeded, int failed, bool paused, bool canceled) {
        setBusy(false);
        downloadThread_ = nullptr;
        if (paused) {
            summaryLabel_->setText(trText("队列已暂停：Net/Cloudflare 阻断了请求。", "Queue paused: Net/Cloudflare blocked a request."));
            QMessageBox::warning(this, windowTitle(), summaryLabel_->text());
            return;
        }
        if (canceled) {
            summaryLabel_->setText(trText("下载已取消。", "Download canceled."));
            return;
        }
        summaryLabel_->setText(
            trText("下载完成：成功 %1，失败 %2。", "Download complete: %1 succeeded, %2 failed.")
                .arg(succeeded)
                .arg(failed));
    });
    connect(worker, &NetBatchDownloadWorker::finished, downloadThread_, &QThread::quit);
    connect(worker, &NetBatchDownloadWorker::finished, worker, &QObject::deleteLater);
    connect(downloadThread_, &QThread::finished, downloadThread_, &QObject::deleteLater);
    downloadThread_->start();
}

}  // namespace miacode::net
