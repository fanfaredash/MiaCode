#pragma once

#include "editor/PlainCodeEditor.h"

#include <QTextBlock>
#include <QTextCursor>

namespace miacode::mainwindow::editor_selection {

inline int clampedSelectionStartOffset(const QString& blockText, int col)
{
    if (blockText.isEmpty()) {
        return 0;
    }
    return qBound(0, col - 1, blockText.size() - 1);
}

inline int clampedCaretOffset(const QString& blockText, int col)
{
    return qBound(0, col - 1, blockText.size());
}

inline int clampedExclusiveSelectionEndOffset(const QString& blockText, int col)
{
    if (blockText.isEmpty()) {
        return 0;
    }
    return qBound(1, col, blockText.size());
}

inline bool buildCaretCursor(PlainCodeEditor* editor, int line, int col, QTextCursor* cursorOut)
{
    if (editor == nullptr || editor->document() == nullptr || cursorOut == nullptr) {
        return false;
    }

    const int normalizedLine = qMax(1, line);
    QTextBlock block = editor->document()->findBlockByNumber(normalizedLine - 1);
    if (!block.isValid()) {
        return false;
    }

    QTextCursor cursor(editor->document());
    cursor.setPosition(block.position() + clampedCaretOffset(block.text(), qMax(1, col)));
    *cursorOut = cursor;
    return true;
}

inline bool buildSelectionCursor(
    PlainCodeEditor* editor,
    int startLine,
    int startCol,
    int endLine,
    int endCol,
    QTextCursor* cursorOut)
{
    if (editor == nullptr || editor->document() == nullptr || cursorOut == nullptr) {
        return false;
    }

    int normalizedStartLine = qMax(1, startLine);
    int normalizedStartCol = qMax(1, startCol);
    int normalizedEndLine = qMax(1, endLine);
    int normalizedEndCol = qMax(1, endCol);
    if (normalizedEndLine < normalizedStartLine
        || (normalizedEndLine == normalizedStartLine && normalizedEndCol < normalizedStartCol)) {
        normalizedEndLine = normalizedStartLine;
        normalizedEndCol = normalizedStartCol;
    }

    QTextBlock startBlock = editor->document()->findBlockByNumber(normalizedStartLine - 1);
    QTextBlock endBlock = editor->document()->findBlockByNumber(normalizedEndLine - 1);
    if (!startBlock.isValid() || !endBlock.isValid()) {
        return false;
    }

    const QString startText = startBlock.text();
    const QString endText = endBlock.text();
    const int startPosition = startBlock.position() + clampedSelectionStartOffset(startText, normalizedStartCol);
    int endPositionExclusive = endBlock.position() + clampedExclusiveSelectionEndOffset(endText, normalizedEndCol);

    if (endBlock == startBlock && endPositionExclusive <= startPosition && !startText.isEmpty()) {
        endPositionExclusive = qMin(startBlock.position() + startText.size(), startPosition + 1);
    }

    QTextCursor cursor(editor->document());
    cursor.setPosition(startPosition);
    if (endPositionExclusive > startPosition) {
        cursor.setPosition(endPositionExclusive, QTextCursor::KeepAnchor);
    }
    *cursorOut = cursor;
    return true;
}

}  // namespace miacode::mainwindow::editor_selection
