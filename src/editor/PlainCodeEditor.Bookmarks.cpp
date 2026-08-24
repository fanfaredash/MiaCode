#include "PlainCodeEditor.h"
#include "PlainCodeEditor.Internal.h"
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

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
    if (!lineNumberArea_->rect().contains(areaPos)) {
        return -1;
    }
    return lineNumberAtAreaPosition(areaPos);
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
