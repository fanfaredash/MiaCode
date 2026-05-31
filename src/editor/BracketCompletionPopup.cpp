#include "BracketCompletionPopup.h"

#include <QApplication>
#include <QPoint>
#include <QRect>
#include <QScreen>

#include <utility>

BracketCompletionPopup::BracketCompletionPopup(QWidget* parent)
    : QListWidget(parent)
{
    // ToolTip + ShowWithoutActivating keeps the editor focused so it still owns
    // the keyboard. NoFocus stops the list from ever grabbing the caret.
    setWindowFlags(Qt::ToolTip | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint);
    setAttribute(Qt::WA_ShowWithoutActivating, true);
    setFocusPolicy(Qt::NoFocus);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    setUniformItemSizes(true);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setSelectionBehavior(QAbstractItemView::SelectRows);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setFrameShape(QFrame::Box);

    connect(this, &QListWidget::itemClicked, this, [this](QListWidgetItem* item) {
        if (item != nullptr) {
            emit candidateActivated(item->text());
        }
    });
}

void BracketCompletionPopup::showCandidates(const QStringList& items, const QPoint& globalTopLeft)
{
    allItems_ = items;
    clear();
    addItems(items);
    if (count() > 0) {
        setCurrentRow(0);
    }
    relayout(globalTopLeft);
    show();
}

void BracketCompletionPopup::moveToAnchor(const QPoint& globalTopLeft)
{
    relayout(globalTopLeft);
}

void BracketCompletionPopup::moveSelection(int delta)
{
    if (count() == 0) {
        return;
    }
    int row = currentRow() + delta;
    if (row < 0) {
        row = 0;
    }
    if (row > count() - 1) {
        row = count() - 1;
    }
    setCurrentRow(row);
}

QString BracketCompletionPopup::currentCandidate() const
{
    const QListWidgetItem* item = currentItem();
    return item != nullptr ? item->text() : QString();
}

bool BracketCompletionPopup::applyFilter(const QString& prefix)
{
    QStringList matched;
    for (const QString& candidate : std::as_const(allItems_)) {
        if (prefix.isEmpty() || candidate.startsWith(prefix)) {
            matched.append(candidate);
        }
    }
    if (matched.isEmpty()) {
        return false;
    }
    const QString previous = currentCandidate();
    clear();
    addItems(matched);
    const int previousRow = matched.indexOf(previous);
    setCurrentRow(previousRow >= 0 ? previousRow : 0);
    return true;
}

void BracketCompletionPopup::relayout(const QPoint& globalTopLeft)
{
    const int rows = count();
    int rowHeight = rows > 0 ? sizeHintForRow(0) : 0;
    if (rowHeight <= 0) {
        rowHeight = fontMetrics().height() + 6;
    }
    const int visibleRows = qBound(1, rows, 8);
    int width = (rows > 0 ? sizeHintForColumn(0) : 0) + 28;
    width = qBound(96, width, 320);
    const int height = visibleRows * rowHeight + 2 * frameWidth() + 4;
    resize(width, height);

    QPoint pos = globalTopLeft;
    if (const QScreen* screen = QApplication::screenAt(globalTopLeft)) {
        const QRect available = screen->availableGeometry();
        if (pos.x() + width > available.right()) {
            pos.setX(available.right() - width);
        }
        if (pos.x() < available.left()) {
            pos.setX(available.left());
        }
        // Flip above the caret line when there isn't room below.
        if (pos.y() + height > available.bottom()) {
            pos.setY(globalTopLeft.y() - height - 6);
        }
    }
    move(pos);
}
