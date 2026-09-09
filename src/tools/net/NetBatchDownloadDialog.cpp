#include "NetBatchDownloadDialog.h"

#include "DialogLocalization.h"
#include "NetBatchDownloadWorker.h"
#include "UiComponents.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAbstractItemView>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QCalendarWidget>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateEdit>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
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
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <array>
#include <utility>

namespace miacode::net {
namespace {

constexpr char kPreferencesAppSection[] = "app";
constexpr char kLastNetBatchOutputDirKey[] = "last_net_batch_output_dir";
constexpr int kPreviewColumn = 7;
constexpr qint64 kSlowConnectionThresholdMs = 1000;
constexpr std::array<int, 8> kDownloadTableColumnWeights = {5, 20, 15, 15, 10, 18, 7, 10};

QIcon makePlayIcon(const QColor& color)
{
    constexpr int kSize = 16;
    constexpr qreal kDpr = 2.0;
    QPixmap pixmap(qRound(kSize * kDpr), qRound(kSize * kDpr));
    pixmap.setDevicePixelRatio(kDpr);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    constexpr qreal kHeight = 10.0;
    constexpr qreal kWidth = 9.0;
    const qreal left = (kSize - kWidth) / 2.0 + 0.5;
    const qreal top = (kSize - kHeight) / 2.0;
    QPainterPath triangle;
    triangle.moveTo(left, top);
    triangle.lineTo(left, top + kHeight);
    triangle.lineTo(left + kWidth, top + kHeight / 2.0);
    triangle.closeSubpath();
    painter.fillPath(triangle, color);
    return QIcon(pixmap);
}

class NetDownloadTable : public QTableWidget {
public:
    explicit NetDownloadTable(QWidget* parent = nullptr)
        : QTableWidget(parent)
    {
    }

    void scheduleColumnProportions()
    {
        if (initialColumnProportionsApplied_ || columnProportionsPending_) {
            return;
        }
        columnProportionsPending_ = true;
        QTimer::singleShot(0, this, [this]() {
            columnProportionsPending_ = false;
            initialColumnProportionsApplied_ = applyColumnProportions();
        });
    }

protected:
    bool viewportEvent(QEvent* event) override
    {
        const bool handled = QTableWidget::viewportEvent(event);
        if (event->type() == QEvent::Resize) {
            scheduleColumnProportions();
        }
        return handled;
    }

private:
    bool applyColumnProportions()
    {
        QHeaderView* const header = horizontalHeader();
        const int availableWidth = viewport() != nullptr ? viewport()->width() : 0;
        if (header == nullptr
            || availableWidth <= 0
            || columnCount() != static_cast<int>(kDownloadTableColumnWeights.size())) {
            return false;
        }

        constexpr int totalWeight = 100;
        int cumulativeWeight = 0;
        int previousBoundary = 0;
        for (int column = 0; column < columnCount(); ++column) {
            cumulativeWeight += kDownloadTableColumnWeights.at(column);
            const int boundary = column == columnCount() - 1
                ? availableWidth
                : (availableWidth * cumulativeWeight + totalWeight / 2) / totalWeight;
            header->resizeSection(column, boundary - previousBoundary);
            previousBoundary = boundary;
        }
        return true;
    }

    bool initialColumnProportionsApplied_ = false;
    bool columnProportionsPending_ = false;
};

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

QString logTimestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

QString displayTimestamp(const QDateTime& utc)
{
    return utc.toLocalTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString defaultOutputDirectory()
{
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (!desktop.trimmed().isEmpty()) {
        return desktop;
    }
    return QDir::homePath();
}

QString storedOutputDirectory()
{
    const QJsonObject root = UiText::loadPreferencesObject();
    const QString stored = root.value(QLatin1String(kPreferencesAppSection))
        .toObject()
        .value(QLatin1String(kLastNetBatchOutputDirKey))
        .toString()
        .trimmed();
    if (!stored.isEmpty()) {
        return stored;
    }
    return defaultOutputDirectory();
}

void saveStoredOutputDirectory(const QString& directoryPath)
{
    const QString normalized = QDir::cleanPath(QDir::fromNativeSeparators(directoryPath.trimmed()));
    if (normalized.isEmpty()) {
        return;
    }
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QLatin1String(kPreferencesAppSection)).toObject();
    if (app.value(QLatin1String(kLastNetBatchOutputDirKey)).toString() == normalized) {
        return;
    }
    app.insert(QLatin1String(kLastNetBatchOutputDirKey), normalized);
    root.insert(QLatin1String(kPreferencesAppSection), app);
    UiText::savePreferencesObject(root);
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

bool chartMatchesTitleKeyword(const NetChartSummary& chart, const QString& titleKeyword, Qt::CaseSensitivity caseSensitivity)
{
    const QString normalized = titleKeyword.trimmed();
    if (normalized.isEmpty()) {
        return true;
    }
    return chart.title.contains(normalized, caseSensitivity);
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

QString onlinePreviewSessionCacheRoot()
{
    const QString tempRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    static QTemporaryDir sessionCache(
        QDir(tempRoot).filePath(QStringLiteral("MiaCode-net-preview-XXXXXX")));
    return sessionCache.isValid() ? sessionCache.path() : QString();
}

bool onlinePreviewCacheIsComplete(const QString& chartDirectory, bool requireVideo = false)
{
    const QDir directory(chartDirectory);
    for (const QString& name : {QStringLiteral("maidata.txt"), QStringLiteral("track.mp3"), QStringLiteral("bg.jpg")}) {
        const QFileInfo file(directory.filePath(name));
        if (!file.isFile() || file.size() <= 0) {
            return false;
        }
    }
    if (requireVideo) {
        const QFileInfo video(directory.filePath(QStringLiteral("pv.mp4")));
        if (!video.isFile() || video.size() <= 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

NetBatchDownloadDialog::NetBatchDownloadDialog(QWidget* parent, OnlinePreviewHandler onlinePreviewHandler)
    : QDialog(parent)
    , onlinePreviewHandler_(std::move(onlinePreviewHandler))
{
    setAttribute(Qt::WA_DeleteOnClose, true);
    buildUi();
}

void NetBatchDownloadDialog::buildUi()
{
    setWindowTitle(UiText::text(QStringLiteral("net.net_batch_download")));
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
    titleEdit_ = new QLineEdit(this);
    startDateEdit_ = new NetDateEdit(QDate::currentDate().addMonths(-1), this);
    startDateEdit_->setCalendarWidget(createNetCalendar(startDateEdit_));
    endDateEdit_ = new NetDateEdit(QDate::currentDate(), this);
    endDateEdit_->setCalendarWidget(createNetCalendar(endDateEdit_));
    outputDirEdit_ = new QLineEdit(QDir::toNativeSeparators(storedOutputDirectory()), this);
    auto* browseButton = new QPushButton(UiText::text(QStringLiteral("net.browse")), this);
    fuzzyMatchCheck_ = new QCheckBox(UiText::text(QStringLiteral("net.fuzzy_case_insensitive_match")), this);
    fuzzyMatchCheck_->setChecked(true);
    zipAfterDownloadCheck_ = new QCheckBox(UiText::text(QStringLiteral("net.also_create_zip_after_success")), this);
    zipAfterDownloadCheck_->setChecked(false);
    downloadPvCheck_ = new QCheckBox(UiText::text(QStringLiteral("net.download_pv")), this);
    downloadPvCheck_->setChecked(true);
    networkTestButton_ = new QPushButton(UiText::text(QStringLiteral("net.test_connection")), this);
    networkStatusLabel_ = new QLabel(UiText::text(QStringLiteral("net.connection_not_tested")), this);
    sortCombo_ = miacode::ui::createDialogComboBox(
        this, 6, Qt::AlignLeft | Qt::AlignVCenter);
    sortCombo_->addItem(UiText::text(QStringLiteral("net.sort_level_ascending")),
        static_cast<int>(NetDownloadSortOrder::LevelAscending));
    sortCombo_->addItem(UiText::text(QStringLiteral("net.sort_level_descending")),
        static_cast<int>(NetDownloadSortOrder::LevelDescending));
    sortCombo_->addItem(UiText::text(QStringLiteral("net.sort_uploaded_newest")),
        static_cast<int>(NetDownloadSortOrder::UploadedNewest));
    sortCombo_->addItem(UiText::text(QStringLiteral("net.sort_uploaded_oldest")),
        static_cast<int>(NetDownloadSortOrder::UploadedOldest));
    sortCombo_->addItem(UiText::text(QStringLiteral("net.sort_status_ascending")),
        static_cast<int>(NetDownloadSortOrder::StatusAscending));
    sortCombo_->addItem(UiText::text(QStringLiteral("net.sort_status_descending")),
        static_cast<int>(NetDownloadSortOrder::StatusDescending));
    sortCombo_->setCurrentIndex(2);
    miacode::ui::applyDialogComboBoxStyle(sortCombo_, 6);
    queryButton_ = new QPushButton(UiText::text(QStringLiteral("net.query")), this);

    const std::array<QWidget*, 9> measuredFormControls = {
        usernameEdit_, tagEdit_, titleEdit_, startDateEdit_, endDateEdit_,
        outputDirEdit_, browseButton, queryButton_, networkTestButton_};
    int formControlHeight = sortCombo_->minimumHeight();
    for (QWidget* control : measuredFormControls) {
        control->ensurePolished();
        formControlHeight = qMax(
            formControlHeight, qMax(control->sizeHint().height(), 30) + 4);
    }
    for (QWidget* control : measuredFormControls) {
        control->setFixedHeight(formControlHeight);
    }
    sortCombo_->setFixedHeight(formControlHeight);

    form->addWidget(new QLabel(UiText::text(QStringLiteral("net.user_id")), this), 0, 0);
    form->addWidget(usernameEdit_, 0, 1);
    form->addWidget(new QLabel(QStringLiteral("Tag"), this), 0, 2);
    form->addWidget(tagEdit_, 0, 3);
    form->addWidget(new QLabel(UiText::text(QStringLiteral("net.song_title")), this), 0, 4);
    form->addWidget(titleEdit_, 0, 5);
    form->addWidget(new QLabel(UiText::text(QStringLiteral("net.start")), this), 0, 6);
    form->addWidget(startDateEdit_, 0, 7);
    form->addWidget(new QLabel(UiText::text(QStringLiteral("net.end")), this), 0, 8);
    form->addWidget(endDateEdit_, 0, 9);
    form->addWidget(queryButton_, 0, 10);
    form->addWidget(fuzzyMatchCheck_, 0, 11);
    form->addWidget(new QLabel(UiText::text(QStringLiteral("net.output_directory")), this), 1, 0);
    form->addWidget(outputDirEdit_, 1, 1, 1, 9);
    form->addWidget(browseButton, 1, 10);
    form->addWidget(zipAfterDownloadCheck_, 1, 11);
    form->addWidget(new QLabel(UiText::text(QStringLiteral("net.sort_by")), this), 2, 0);
    form->addWidget(sortCombo_, 2, 1);
    auto* connectionTestRow = new QHBoxLayout;
    connectionTestRow->setContentsMargins(0, 0, 0, 0);
    connectionTestRow->addWidget(networkTestButton_);
    connectionTestRow->addWidget(networkStatusLabel_);
    connectionTestRow->addStretch(1);
    form->addLayout(connectionTestRow, 2, 2, 1, 9);
    form->addWidget(downloadPvCheck_, 2, 11);
    root->addLayout(form);

    table_ = new NetDownloadTable(this);
    table_->setColumnCount(8);
    table_->setHorizontalHeaderLabels({
        UiText::text(QStringLiteral("net.select")),
        UiText::text(QStringLiteral("net.title")),
        UiText::text(QStringLiteral("net.artist")),
        UiText::text(QStringLiteral("net.designer")),
        UiText::text(QStringLiteral("net.levels")),
        UiText::text(QStringLiteral("net.uploaded")),
        UiText::text(QStringLiteral("net.status")),
        UiText::text(QStringLiteral("net.online_preview")),
    });
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table_->setSelectionMode(QAbstractItemView::NoSelection);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setAlternatingRowColors(true);
    root->addWidget(table_, 1);

    auto* bottom = new QGridLayout;
    summaryLabel_ = new QLabel(UiText::text(QStringLiteral("net.enter_a_user_id_or")), this);
    progressBar_ = new QProgressBar(this);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    selectAllButton_ = new QPushButton(UiText::text(QStringLiteral("net.select_all")), this);
    clearSelectionButton_ = new QPushButton(UiText::text(QStringLiteral("net.clear_selection")), this);
    downloadButton_ = new QPushButton(UiText::text(QStringLiteral("net.download_selected")), this);
    logButton_ = new QPushButton(UiText::text(QStringLiteral("net.show_log")), this);
    closeButton_ = new QPushButton(UiText::text(QStringLiteral("action.close")), this);
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
    logEdit_->setPlaceholderText(UiText::text(QStringLiteral("net.query_and_download_diagnostics_will")));
    root->addWidget(logEdit_);

    connect(browseButton, &QPushButton::clicked, this, [this]() { chooseOutputDirectory(); });
    connect(queryButton_, &QPushButton::clicked, this, [this]() { queryCharts(); });
    connect(networkTestButton_, &QPushButton::clicked, this, [this]() { testConnection(); });
    connect(sortCombo_, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        syncSelectionsFromTable();
        applyCurrentSort();
        rebuildTable();
    });
    connect(selectAllButton_, &QPushButton::clicked, this, [this]() { selectAllRows(true); });
    connect(clearSelectionButton_, &QPushButton::clicked, this, [this]() { selectAllRows(false); });
    connect(logButton_, &QPushButton::clicked, this, [this]() { toggleLogVisible(); });
    connect(downloadButton_, &QPushButton::clicked, this, [this]() {
        if (busy_) {
            cancelRequested_ = true;
            summaryLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
            return;
        }
        downloadSelected();
    });
    connect(closeButton_, &QPushButton::clicked, this, [this]() {
        if (busy_) {
            cancelRequested_ = true;
            if (connectionProbeActive_) {
                networkTestButton_->setEnabled(false);
                networkStatusLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
            } else {
                summaryLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
            }
            return;
        }
        close();
    });

    // Return/Enter must not implicitly query, start/repeat a download, cancel,
    // or close the dialog based on whichever button happened to keep focus.
    // Actions in this tool are explicit clicks only.
    const QList<QPushButton*> actionButtons = findChildren<QPushButton*>();
    for (QPushButton* button : actionButtons) {
        button->setAutoDefault(false);
        button->setDefault(false);
    }
}

void NetBatchDownloadDialog::closeEvent(QCloseEvent* event)
{
    if (busy_) {
        cancelRequested_ = true;
        if (connectionProbeActive_ && networkStatusLabel_ != nullptr) {
            networkStatusLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
        } else if (summaryLabel_ != nullptr) {
            summaryLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
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
    titleEdit_->setEnabled(!busy);
    startDateEdit_->setEnabled(!busy);
    endDateEdit_->setEnabled(!busy);
    outputDirEdit_->setEnabled(!busy);
    fuzzyMatchCheck_->setEnabled(!busy);
    zipAfterDownloadCheck_->setEnabled(!busy);
    downloadPvCheck_->setEnabled(!busy);
    sortCombo_->setEnabled(!busy);
    queryButton_->setEnabled(!busy);
    networkTestButton_->setEnabled(!busy || connectionProbeActive_);
    selectAllButton_->setEnabled(!busy);
    clearSelectionButton_->setEnabled(!busy);
    closeButton_->setText(
        busy ? UiText::text(QStringLiteral("action.cancel")) : UiText::text(QStringLiteral("action.close")));
    downloadButton_->setText(
        busy && !connectionProbeActive_
            ? UiText::text(QStringLiteral("net.cancel_download"))
            : UiText::text(QStringLiteral("net.download_selected")));
    downloadButton_->setEnabled(!connectionProbeActive_ && !jobs_.isEmpty());
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (QWidget* previewButton = table_->cellWidget(row, kPreviewColumn); previewButton != nullptr) {
            previewButton->setEnabled(!busy);
        }
    }
}

void NetBatchDownloadDialog::appendLog(const QString& message)
{
    if (logEdit_ == nullptr) {
        return;
    }
    logEdit_->appendPlainText(QStringLiteral("[%1] %2").arg(logTimestamp(), message));
    if (!logVisible_) {
        logButton_->setText(UiText::text(QStringLiteral("net.show_log_2")));
    }
    qApp->processEvents(QEventLoop::AllEvents, 20);
}

void NetBatchDownloadDialog::toggleLogVisible()
{
    logVisible_ = !logVisible_;
    const int currentWidth = width();
    logEdit_->setVisible(logVisible_);
    logButton_->setText(logVisible_ ? UiText::text(QStringLiteral("net.hide_log")) : UiText::text(QStringLiteral("net.show_log")));
    if (logVisible_) {
        resize(currentWidth, qMax(height(), 760));
    }
}

void NetBatchDownloadDialog::chooseOutputDirectory()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this,
        UiText::text(QStringLiteral("net.choose_output_directory")),
        outputDirEdit_->text().trimmed());
    if (!dir.isEmpty()) {
        outputDirEdit_->setText(QDir::toNativeSeparators(dir));
        saveStoredOutputDirectory(dir);
    }
}

void NetBatchDownloadDialog::testConnection()
{
    if (connectionProbeActive_) {
        cancelRequested_ = true;
        networkTestButton_->setEnabled(false);
        networkStatusLabel_->setText(UiText::text(QStringLiteral("net.canceling")));
        return;
    }
    if (busy_) {
        return;
    }

    cancelRequested_ = false;
    connectionProbeActive_ = true;
    networkTestButton_->setText(UiText::text(QStringLiteral("net.cancel_connection_test")));
    networkStatusLabel_->setText(UiText::text(QStringLiteral("net.testing_connection")));
    progressBar_->setRange(0, 0);
    setBusy(true);
    appendLog(UiText::text(QStringLiteral("net.connection_test_started")));

    const NetConnectionProbeResult result = client_.probeConnection(&cancelRequested_);

    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    connectionProbeActive_ = false;
    setBusy(false);
    networkTestButton_->setText(UiText::text(QStringLiteral("net.test_connection")));

    QString status;
    if (result.canceled) {
        status = UiText::text(QStringLiteral("net.connection_test_canceled"));
    } else if (result.ok && result.elapsedMs >= kSlowConnectionThresholdMs) {
        status = UiText::text(QStringLiteral("net.connection_slow_1_ms")).arg(result.elapsedMs);
    } else if (result.ok) {
        status = UiText::text(QStringLiteral("net.connection_normal_1_ms")).arg(result.elapsedMs);
    } else if (result.blockingResponse) {
        status = UiText::text(QStringLiteral("net.connection_blocked"));
    } else if (result.timedOut) {
        status = UiText::text(QStringLiteral("net.connection_timeout"));
    } else {
        status = UiText::text(QStringLiteral("net.connection_failed"));
    }
    networkStatusLabel_->setText(status);
    appendLog(UiText::text(QStringLiteral("net.connection_test_result_1_http_2_ms_3_4"))
                  .arg(status)
                  .arg(result.statusCode)
                  .arg(result.elapsedMs)
                  .arg(result.errorMessage.isEmpty() ? QStringLiteral("-") : result.errorMessage));
}

void NetBatchDownloadDialog::queryCharts()
{
    const QString username = usernameEdit_->text().trimmed();
    const QString tag = tagEdit_->text().trimmed();
    const QString title = titleEdit_->text().trimmed();
    const bool fuzzyMatch = fuzzyMatchCheck_->isChecked();
    const Qt::CaseSensitivity caseSensitivity = fuzzyMatch ? Qt::CaseInsensitive : Qt::CaseSensitive;
    if (username.isEmpty() && tag.isEmpty() && title.isEmpty()) {
        QMessageBox::warning(this, windowTitle(), UiText::text(QStringLiteral("net.please_enter_a_user_id")));
        return;
    }

    setBusy(true);
    cancelRequested_ = false;
    progressBar_->setRange(0, 0);
    summaryLabel_->setText(UiText::text(QStringLiteral("net.querying_net")));
    appendLog(UiText::text(QStringLiteral("net.start_query_user_1_tag"))
                  .arg(username.isEmpty() ? QStringLiteral("-") : username)
                  .arg(tag.isEmpty() ? QStringLiteral("-") : tag)
                  .arg(title.isEmpty() ? QStringLiteral("-") : title)
                  .arg(startDateEdit_->date().toString(Qt::ISODate))
                  .arg(endDateEdit_->date().toString(Qt::ISODate))
                  .arg(fuzzyMatch ? QStringLiteral("yes") : QStringLiteral("no")));
    qApp->processEvents();

    QElapsedTimer elapsed;
    elapsed.start();
    QString error;
    NetQueryOptions options;
    options.fuzzyCaseInsensitive = fuzzyMatch;
    options.titleKeyword = title;
    const QList<NetChartSummary> queriedCharts = client_.queryCharts(username, tag, options, &error);
    progressBar_->setRange(0, 100);
    progressBar_->setValue(0);
    setBusy(false);

    if (!error.isEmpty()) {
        appendLog(UiText::text(QStringLiteral("net.query_failed_1_ms_2")).arg(elapsed.elapsed()).arg(error));
        QMessageBox::critical(this, windowTitle(), error);
        summaryLabel_->setText(UiText::text(QStringLiteral("net.query_failed")));
        return;
    }

    const QList<NetChartSummary> dateFiltered =
        filterChartsByLocalDateRange(queriedCharts, startDateEdit_->date(), endDateEdit_->date());
    QList<NetChartSummary> filtered;
    filtered.reserve(dateFiltered.size());
    for (const NetChartSummary& chart : dateFiltered) {
        if (chartMatchesUserKeyword(chart, username, caseSensitivity)
            && chartMatchesTagKeyword(chart, tag, caseSensitivity)
            && chartMatchesTitleKeyword(chart, title, caseSensitivity)) {
            filtered.append(chart);
        }
    }
    populateTable(filtered);
    summaryLabel_->setText(
        UiText::text(QStringLiteral("net.found_1_chart_s_from"))
            .arg(filtered.size())
            .arg(queriedCharts.size()));
    appendLog(UiText::text(QStringLiteral("net.query_complete_1_ms_api"))
                  .arg(elapsed.elapsed())
                  .arg(queriedCharts.size())
                  .arg(dateFiltered.size())
                  .arg(filtered.size()));
}

void NetBatchDownloadDialog::populateTable(const QList<NetChartSummary>& charts)
{
    jobs_.clear();
    jobs_.reserve(charts.size());
    for (int row = 0; row < charts.size(); ++row) {
        NetDownloadJob job;
        job.chart = charts.at(row);
        job.selected = true;
        job.status = UiText::text(QStringLiteral("net.pending"));
        jobs_.append(job);
    }
    applyCurrentSort();
    rebuildTable();
    downloadButton_->setEnabled(!jobs_.isEmpty());
}

void NetBatchDownloadDialog::rebuildTable()
{
    table_->clearContents();
    table_->setRowCount(jobs_.size());
    for (int row = 0; row < jobs_.size(); ++row) {
        const NetDownloadJob& job = jobs_.at(row);
        auto* check = new QTableWidgetItem;
        check->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        check->setCheckState(job.selected ? Qt::Checked : Qt::Unchecked);
        table_->setItem(row, 0, check);
        table_->setItem(row, 1, new QTableWidgetItem(job.chart.title));
        table_->setItem(row, 2, new QTableWidgetItem(job.chart.artist));
        table_->setItem(row, 3, new QTableWidgetItem(job.chart.designer));
        table_->setItem(row, 4, new QTableWidgetItem(formatLevels(job.chart.levels)));
        table_->setItem(row, 5, new QTableWidgetItem(displayTimestamp(job.chart.timestampUtc)));
        table_->setItem(row, 6, new QTableWidgetItem(job.status));
        auto* previewCell = new QWidget(table_);
        auto* previewLayout = new QHBoxLayout(previewCell);
        previewLayout->setContentsMargins(2, 2, 2, 2);
        previewLayout->setAlignment(Qt::AlignCenter);
        auto* previewButton = new QPushButton(previewCell);
        previewButton->setIcon(makePlayIcon(UiTheme::colors().textPrimary));
        previewButton->setIconSize(QSize(16, 16));
        previewButton->setToolTip(UiText::text(QStringLiteral("net.online_preview")));
        previewButton->setAccessibleName(UiText::text(QStringLiteral("net.online_preview")));
        previewButton->setStyleSheet(QStringLiteral(
            "QPushButton { min-width: 26px; max-width: 26px; min-height: 26px; max-height: 26px; padding: 0; }"));
        previewButton->setAutoDefault(false);
        previewButton->setDefault(false);
        previewButton->setFocusPolicy(Qt::NoFocus);
        previewLayout->addWidget(previewButton);
        connect(previewButton, &QPushButton::clicked, this, [this, chartId = job.chart.id]() {
            onlinePreview(chartId);
        });
        table_->setCellWidget(row, kPreviewColumn, previewCell);
    }
    table_->resizeRowsToContents();
    static_cast<NetDownloadTable*>(table_)->scheduleColumnProportions();
}

void NetBatchDownloadDialog::applyCurrentSort()
{
    if (sortCombo_ == nullptr || jobs_.isEmpty()) {
        return;
    }
    const auto order = static_cast<NetDownloadSortOrder>(sortCombo_->currentData().toInt());
    sortNetDownloadJobs(&jobs_, order);
}

void NetBatchDownloadDialog::syncSelectionsFromTable()
{
    if (table_->rowCount() != jobs_.size()) {
        return;
    }
    for (int row = 0; row < jobs_.size(); ++row) {
        if (const QTableWidgetItem* item = table_->item(row, 0); item != nullptr) {
            jobs_[row].selected = item->checkState() == Qt::Checked;
        }
    }
}

void NetBatchDownloadDialog::selectAllRows(bool selected)
{
    for (int row = 0; row < table_->rowCount(); ++row) {
        if (QTableWidgetItem* item = table_->item(row, 0); item != nullptr) {
            item->setCheckState(selected ? Qt::Checked : Qt::Unchecked);
        }
    }
}

void NetBatchDownloadDialog::setChartStatus(const QString& chartId, const QString& status)
{
    for (int row = 0; row < jobs_.size(); ++row) {
        if (jobs_[row].chart.id != chartId) {
            continue;
        }
        jobs_[row].status = status;
        if (table_->item(row, 6) != nullptr) {
            table_->item(row, 6)->setText(status);
        }
        break;
    }
    qApp->processEvents(QEventLoop::AllEvents, 50);
}

void NetBatchDownloadDialog::onlinePreview(const QString& chartId)
{
    if (busy_ || onlinePreviewHandler_ == nullptr) {
        return;
    }
    syncSelectionsFromTable();
    const auto it = std::find_if(jobs_.cbegin(), jobs_.cend(), [&](const NetDownloadJob& job) {
        return job.chart.id == chartId;
    });
    if (it == jobs_.cend()) {
        return;
    }

    const QString cacheRoot = onlinePreviewSessionCacheRoot();
    if (cacheRoot.isEmpty()) {
        QMessageBox::critical(this, windowTitle(), UiText::text(QStringLiteral("net.online_preview_cache_failed")));
        return;
    }
    const QString cacheIdentity = it->chart.hash.trimmed().isEmpty()
        ? it->chart.id
        : QStringLiteral("%1-%2").arg(it->chart.id, it->chart.hash.left(16));
    const QString chartDirectory = chartDirectoryPathForTitle(
        cacheRoot, it->chart.title, cacheIdentity);
    const QString chartPath = QDir(chartDirectory).filePath(QStringLiteral("maidata.txt"));
    const bool downloadVideo = downloadPvCheck_->isChecked();
    if (onlinePreviewCacheIsComplete(chartDirectory, downloadVideo)) {
        if (onlinePreviewHandler_(chartPath)) {
            summaryLabel_->setText(UiText::text(QStringLiteral("net.online_preview_opened")));
        }
        return;
    }

    NetDownloadJob previewJob = *it;
    previewJob.selected = true;
    previewJob.outputDirectoryPath = chartDirectory;
    NetBatchDownloadRequest request;
    request.jobs = {previewJob};
    request.outputDirectory = cacheRoot;
    request.createZip = false;
    request.downloadVideo = downloadVideo;
    request.onlinePreview = true;
    setChartStatus(chartId, UiText::text(QStringLiteral("net.online_preview_loading")));
    appendLog(UiText::text(QStringLiteral("net.online_preview_loading_1")).arg(it->chart.title));
    startDownloadRequest(std::move(request), 1, chartPath);
}

void NetBatchDownloadDialog::downloadSelected()
{
    const QString outputDir = QDir::fromNativeSeparators(outputDirEdit_->text().trimmed());
    if (outputDir.isEmpty() || !QDir().mkpath(outputDir)) {
        QMessageBox::warning(this, windowTitle(), UiText::text(QStringLiteral("net.please_choose_a_valid_output")));
        return;
    }
    saveStoredOutputDirectory(outputDir);

    int selectedCount = 0;
    for (int row = 0; row < jobs_.size(); ++row) {
        jobs_[row].selected = table_->item(row, 0) != nullptr && table_->item(row, 0)->checkState() == Qt::Checked;
        if (jobs_[row].selected) {
            ++selectedCount;
        }
    }
    if (selectedCount <= 0) {
        QMessageBox::information(this, windowTitle(), UiText::text(QStringLiteral("net.no_charts_are_selected")));
        return;
    }

    NetBatchDownloadRequest request;
    request.jobs = jobs_;
    request.outputDirectory = outputDir;
    request.createZip = zipAfterDownloadCheck_->isChecked();
    request.downloadVideo = downloadPvCheck_->isChecked();
    appendLog(UiText::text(QStringLiteral("net.start_download_queue_selected_1"))
                  .arg(selectedCount)
                  .arg(outputDir)
                  .arg(request.createZip ? QStringLiteral("yes") : QStringLiteral("no"))
                  .arg(request.downloadVideo ? QStringLiteral("yes") : QStringLiteral("no")));

    startDownloadRequest(std::move(request), selectedCount);
}

void NetBatchDownloadDialog::startDownloadRequest(
    NetBatchDownloadRequest request,
    int workCount,
    const QString& previewChartPath)
{
    setBusy(true);
    cancelRequested_ = false;
    progressBar_->setRange(0, workCount);
    progressBar_->setValue(0);
    QStringList requestChartIds;
    requestChartIds.reserve(request.jobs.size());
    for (const NetDownloadJob& job : std::as_const(request.jobs)) {
        requestChartIds.append(job.chart.id);
    }

    auto* worker = new NetBatchDownloadWorker(std::move(request), &cancelRequested_);
    downloadThread_ = new QThread(this);
    worker->moveToThread(downloadThread_);

    connect(downloadThread_, &QThread::started, worker, &NetBatchDownloadWorker::run);
    connect(worker, &NetBatchDownloadWorker::rowStatus, this, [this, requestChartIds](int row, const QString& status) {
        if (row >= 0 && row < requestChartIds.size()) {
            setChartStatus(requestChartIds.at(row), status);
        }
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
    connect(worker, &NetBatchDownloadWorker::finished, this, [this, previewChartPath](int succeeded, int failed, bool paused, bool canceled) {
        setBusy(false);
        downloadThread_ = nullptr;
        applyCurrentSort();
        rebuildTable();
        if (paused) {
            summaryLabel_->setText(UiText::text(QStringLiteral("net.queue_paused_net_cloudflare_blocked")));
            QMessageBox::warning(this, windowTitle(), summaryLabel_->text());
            return;
        }
        if (canceled) {
            summaryLabel_->setText(UiText::text(QStringLiteral("net.download_canceled")));
            return;
        }
        if (!previewChartPath.isEmpty()) {
            if (succeeded == 1 && onlinePreviewCacheIsComplete(QFileInfo(previewChartPath).absolutePath())) {
                if (onlinePreviewHandler_ != nullptr && onlinePreviewHandler_(previewChartPath)) {
                    summaryLabel_->setText(UiText::text(QStringLiteral("net.online_preview_opened")));
                }
            } else {
                summaryLabel_->setText(UiText::text(QStringLiteral("net.online_preview_failed")));
                QMessageBox::warning(this, windowTitle(), summaryLabel_->text());
            }
            return;
        }
        summaryLabel_->setText(
            UiText::text(QStringLiteral("net.download_complete_1_succeeded_2"))
                .arg(succeeded)
                .arg(failed));
        if (failed > 0) {
            QMessageBox::warning(
                this,
                windowTitle(),
                UiText::text(QStringLiteral("net.download_complete_with_errors_1"))
                    .arg(failed));
        }
    });
    connect(worker, &NetBatchDownloadWorker::finished, downloadThread_, &QThread::quit);
    connect(worker, &NetBatchDownloadWorker::finished, worker, &QObject::deleteLater);
    connect(downloadThread_, &QThread::finished, downloadThread_, &QObject::deleteLater);
    downloadThread_->start();
}

}  // namespace miacode::net
