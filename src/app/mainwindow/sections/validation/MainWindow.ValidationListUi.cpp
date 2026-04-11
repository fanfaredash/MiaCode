#include "MainWindow.ValidationSection.h"
#include "../../MainWindowShared.h"

#include "UiTheme.h"

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

using namespace miacode::mainwindow::shared;

namespace {

constexpr int kIssueLineRole = Qt::UserRole;
constexpr int kIssueColRole = Qt::UserRole + 1;
constexpr int kIssueAuxRole = Qt::UserRole + 2;

int wrappedRichTextHeight(const QString& html, const QFont& font, int width)
{
    QTextDocument document;
    document.setDocumentMargin(0.0);
    document.setDefaultFont(font);
    document.setHtml(html);
    document.setTextWidth(qMax(1, width));
    return qMax(1, qCeil(document.size().height()));
}

}  // namespace

QListWidgetItem* MainWindow::ValidationSection::addWrappedListEntry(
    QListWidget* list,
    const QString& html,
    const QString& plainText,
    int line,
    int col,
    double second,
    bool enabled)
{
    if (list == nullptr) {
        return nullptr;
    }

    auto* item = new QListWidgetItem(list);
    item->setToolTip(plainText);
    item->setData(kIssueLineRole, line);
    item->setData(kIssueColRole, col);
    item->setData(kIssueAuxRole, second);
    if (!enabled) {
        item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
    item->setText(QString());

    auto* label = new QLabel(list);
    label->setObjectName(QStringLiteral("WrappedListEntryLabel"));
    label->setTextFormat(Qt::RichText);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::NoTextInteraction);
    label->setStyleSheet(UiTheme::validationMessageLabelStyleSheet());
    label->setText(html);

    auto* rowWidget = new QWidget(list);
    auto* rowLayout = new QVBoxLayout(rowWidget);
    const bool roomyLayout = (list == ui_.muriList_ || list == ui_.errorList_);
    rowLayout->setContentsMargins(roomyLayout ? 4 : 0, roomyLayout ? 3 : 0, roomyLayout ? 4 : 0, roomyLayout ? 4 : 0);
    rowLayout->setSpacing(0);
    rowLayout->addWidget(label);

    list->setItemWidget(item, rowWidget);
    relayoutWrappedListRows(list);
    scheduleWrappedListRelayout(list);
    return item;
}

void MainWindow::ValidationSection::relayoutWrappedListRows(QListWidget* list)
{
    if (list == nullptr) {
        return;
    }

    const int rowWidth = qMax(220, list->viewport()->width());
    const bool roomyLayout = (list == ui_.muriList_ || list == ui_.errorList_);
    const int horizontalMargin = roomyLayout ? 8 : 0;
    const int verticalMargin = roomyLayout ? 7 : 2;
    const int labelWidth = qMax(1, rowWidth - horizontalMargin);
    for (int index = 0; index < list->count(); ++index) {
        QListWidgetItem* item = list->item(index);
        if (item == nullptr) {
            continue;
        }
        QWidget* rowWidget = list->itemWidget(item);
        if (rowWidget == nullptr) {
            continue;
        }
        QLabel* label = rowWidget->findChild<QLabel*>(QStringLiteral("WrappedListEntryLabel"));
        if (label == nullptr) {
            continue;
        }
        label->setFixedWidth(labelWidth);
        const int labelHeight = wrappedRichTextHeight(label->text(), label->font(), labelWidth);
        label->setFixedHeight(labelHeight);
        const int rowHeight = qMax(roomyLayout ? 40 : 28, labelHeight + verticalMargin);
        rowWidget->setMinimumHeight(rowHeight);
        rowWidget->setMaximumHeight(rowHeight);
        item->setSizeHint(QSize(rowWidth, rowHeight));
    }
}

void MainWindow::ValidationSection::scheduleWrappedListRelayout(QListWidget* list)
{
    if (list == nullptr) {
        return;
    }
    if (list->property("wrappedRelayoutPending").toBool()) {
        return;
    }
    list->setProperty("wrappedRelayoutPending", true);
    QTimer::singleShot(0, list, [this, list]() {
        if (list == nullptr) {
            return;
        }
        list->setProperty("wrappedRelayoutPending", false);
        relayoutWrappedListRows(list);
    });
}

QString MainWindow::ValidationSection::currentValidationIgnoreScopeKey() const
{
    return state_.currentFilePath_.isEmpty() ? QStringLiteral("<unsaved>") : state_.currentFilePath_;
}

bool MainWindow::ValidationSection::isIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey) const
{
    if (issueTypeKey.isEmpty()) {
        return false;
    }
    const auto it = state_.ignoredHeaderIssueTypesByFile_.constFind(currentValidationIgnoreScopeKey());
    return it != state_.ignoredHeaderIssueTypesByFile_.constEnd() && it->contains(issueTypeKey);
}

void MainWindow::ValidationSection::setIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey, bool ignored)
{
    if (issueTypeKey.isEmpty()) {
        return;
    }
    const QString scopeKey = currentValidationIgnoreScopeKey();
    QSet<QString>& ignoredTypes = state_.ignoredHeaderIssueTypesByFile_[scopeKey];
    if (ignored) {
        ignoredTypes.insert(issueTypeKey);
    } else {
        ignoredTypes.remove(issueTypeKey);
        if (ignoredTypes.isEmpty()) {
            state_.ignoredHeaderIssueTypesByFile_.remove(scopeKey);
        }
    }
}

QListWidgetItem* MainWindow::addWrappedListEntry(
    QListWidget* list,
    const QString& html,
    const QString& plainText,
    int line,
    int col,
    double second,
    bool enabled)
{
    return validationSection_->addWrappedListEntry(list, html, plainText, line, col, second, enabled);
}

void MainWindow::relayoutWrappedListRows(QListWidget* list)
{
    validationSection_->relayoutWrappedListRows(list);
}

void MainWindow::scheduleWrappedListRelayout(QListWidget* list)
{
    validationSection_->scheduleWrappedListRelayout(list);
}

QString MainWindow::currentValidationIgnoreScopeKey() const
{
    return validationSection_->currentValidationIgnoreScopeKey();
}

bool MainWindow::isIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey) const
{
    return validationSection_->isIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey);
}

void MainWindow::setIssueTypeIgnoredInHeaderForCurrentFile(const QString& issueTypeKey, bool ignored)
{
    validationSection_->setIssueTypeIgnoredInHeaderForCurrentFile(issueTypeKey, ignored);
}
