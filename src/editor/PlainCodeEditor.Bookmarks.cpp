#include "PlainCodeEditor.h"
#include "PlainCodeEditor.Internal.h"
#include "BracketCompletionPopup.h"
#include "SimaiCompletionCatalog.h"
#include "common/DebugLog.h"
#include "ShortcutRegistry.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QInputMethod>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFocusEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

namespace {
constexpr int kLineNumberDropHitSlopRight = 56;
}  // namespace

void PlainCodeEditor::setBookmarkedLines(const QSet<int>& lines)
{
    bookmarkedLines_ = lines;
    if (lineNumberArea_ != nullptr) {
        lineNumberArea_->update();
    }
}

int PlainCodeEditor::lineNumberAtGlobalPosition(const QPoint& globalPos) const
{
    if (lineNumberArea_ == nullptr) {
        return -1;
    }
    const QPoint areaPos = miacode::ui::mapGlobalPointToWidget(lineNumberArea_, globalPos);
    QRect hitRect = lineNumberArea_->rect();
    hitRect.adjust(0, 0, kLineNumberDropHitSlopRight, 0);
    if (!hitRect.contains(areaPos)) {
        return -1;
    }
    return lineNumberAtAreaPosition(areaPos);
}

void PlainCodeEditor::setBookmarkDropPreviewLine(int line)
{
    const int normalizedLine = line > 0 ? line : -1;
    if (hoveredBookmarkDropLine_ == normalizedLine) {
        return;
    }
    hoveredBookmarkDropLine_ = normalizedLine;
    if (lineNumberArea_ != nullptr) {
        lineNumberArea_->update();
    }
}

int PlainCodeEditor::lineNumberAtAreaPosition(const QPoint& pos) const
{
    QTextCursor startCursor = cursorForPosition(QPoint(0, 0));
    QTextBlock block = startCursor.block();
    if (!block.isValid() && document() != nullptr) {
        block = document()->firstBlock();
    }
    int blockNumber = block.isValid() ? block.blockNumber() : 0;
    const int yOffset = qMax(0, topOverlayInsetPixels_);
    const int targetY = pos.y() - yOffset;

    while (block.isValid()) {
        QTextCursor blockCursor(block);
        const QRect blockRect = cursorRect(blockCursor);
        if (targetY >= blockRect.top() && targetY <= blockRect.bottom()) {
            return blockNumber + 1;
        }
        if (blockRect.top() > targetY) {
            break;
        }
        block = block.next();
        ++blockNumber;
    }
    return -1;
}

void PlainCodeEditor::dragEnterEvent(QDragEnterEvent* event)
{
    if (event != nullptr && event->mimeData() != nullptr
        && event->mimeData()->hasFormat(QStringLiteral("application/x-miacode-bookmark-move"))) {
        event->acceptProposedAction();
        return;
    }
    QTextEdit::dragEnterEvent(event);
}

void PlainCodeEditor::dragMoveEvent(QDragMoveEvent* event)
{
    if (event != nullptr && event->mimeData() != nullptr
        && event->mimeData()->hasFormat(QStringLiteral("application/x-miacode-bookmark-move"))) {
        const QPoint areaPos = event->position().toPoint();
        const int line = lineNumberAtAreaPosition(areaPos);
        if (hoveredBookmarkDropLine_ != line) {
            hoveredBookmarkDropLine_ = line;
            lineNumberArea_->update();
        }
        event->acceptProposedAction();
        return;
    }
    QTextEdit::dragMoveEvent(event);
}

void PlainCodeEditor::dragLeaveEvent(QDragLeaveEvent* event)
{
    hoveredBookmarkDropLine_ = -1;
    lineNumberArea_->update();
    QTextEdit::dragLeaveEvent(event);
}

void PlainCodeEditor::dropEvent(QDropEvent* event)
{
    if (event != nullptr && event->mimeData() != nullptr) {
        // Clear any bookmark-drop-row hover indicator regardless of which
        // drop format we receive — dragMoveEvent may have set it for a
        // bookmark drag earlier in the gesture.
        hoveredBookmarkDropLine_ = -1;
        lineNumberArea_->update();
        if (event->mimeData()->hasFormat(QStringLiteral("application/x-miacode-bookmark-move"))) {
            const int line = lineNumberAtAreaPosition(event->position().toPoint());
            if (line > 0) {
                bool ok = false;
                const int fromLine = event->mimeData()->data(
                    QStringLiteral("application/x-miacode-bookmark-move")).toInt(&ok);
                if (ok && fromLine > 0 && fromLine != line) {
                    emit lineNumberBookmarkMoveRequested(fromLine, line);
                }
            }
            event->acceptProposedAction();
            return;
        }
    }
    // Non-bookmark drop (e.g. dragging a selected block of text inside
    // the editor): delegate to QTextEdit so the default text-move drop
    // logic actually moves the text. Previously every drop was
    // short-circuited with acceptProposedAction(), which left the
    // caret at the drop position but never moved any text.
    QTextEdit::dropEvent(event);
}

void PlainCodeEditor::mouseReleaseEvent(QMouseEvent* event)
{
    pressedBookmarkLine_ = -1;
    QTextEdit::mouseReleaseEvent(event);
}
