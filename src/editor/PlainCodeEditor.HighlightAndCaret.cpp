#include "PlainCodeEditor.h"
#include "BracketCompletionPopup.h"
#include "SimaiCompletionCatalog.h"
#include "common/DebugLog.h"
#include "ShortcutRegistry.h"
#include "UiText.h"
#include "UiTheme.h"
#include "app/ui/AppBackgroundPainter.h"

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

#include "PlainCodeEditor.Internal.h"

using namespace miacode::editor::pce_detail;

namespace {
constexpr int kCurrentLineHighlightLeftInset = 3;

constexpr int kCurrentLineHighlightRightInset = 0;

constexpr int kCurrentLineHighlightDirtyMargin = 2;

struct BlockVisualSpan
{
    QRect firstRowRect;
    QRect lastRowRect;

    bool isValid() const
    {
        return firstRowRect.isValid() && lastRowRect.isValid();
    }

    int top() const
    {
        return qMin(firstRowRect.top(), lastRowRect.top());
    }

    int bottom() const
    {
        return qMax(firstRowRect.bottom(), lastRowRect.bottom());
    }

    int height() const
    {
        return qMax(1, bottom() - top() + 1);
    }
};

BlockVisualSpan visibleBlockSpan(const PlainCodeEditor* editor, const QTextBlock& block)
{
    BlockVisualSpan span;
    if (editor == nullptr || !block.isValid()) {
        return span;
    }

    QTextCursor blockStartCursor(block);
    span.firstRowRect = editor->cursorRect(blockStartCursor);
    if (!span.firstRowRect.isValid()) {
        return span;
    }

    // The end-of-block caret sits on the last soft-wrapped visual row.
    QTextCursor blockEndCursor(block);
    blockEndCursor.movePosition(QTextCursor::EndOfBlock);
    span.lastRowRect = editor->cursorRect(blockEndCursor);
    if (!span.lastRowRect.isValid()) {
        span.lastRowRect = span.firstRowRect;
    }
    return span;
}

QPointF normalizedViewportHitPositionForBlockSpacing(const PlainCodeEditor* editor, const QPointF& position)
{
    if (editor == nullptr || editor->document() == nullptr || editor->viewport() == nullptr) {
        return position;
    }

    QTextCursor visibleStartCursor = editor->cursorForPosition(QPoint(0, 0));
    QTextBlock block = visibleStartCursor.block();
    if (!block.isValid()) {
        block = editor->document()->firstBlock();
    }
    if (!block.isValid()) {
        return position;
    }

    BlockVisualSpan previousSpan;
    while (block.isValid()) {
        const BlockVisualSpan currentSpan = visibleBlockSpan(editor, block);
        if (!currentSpan.isValid()) {
            block = block.next();
            continue;
        }

        if (position.y() < currentSpan.top()) {
            if (previousSpan.isValid() && position.y() > previousSpan.bottom()) {
                return QPointF(position.x(), static_cast<qreal>(previousSpan.lastRowRect.center().y()));
            }
            return position;
        }
        if (position.y() <= currentSpan.bottom()) {
            return position;
        }

        previousSpan = currentSpan;
        block = block.next();
    }

    if (!previousSpan.isValid() || position.y() <= previousSpan.bottom()) {
        return position;
    }
    return QPointF(position.x(), static_cast<qreal>(previousSpan.lastRowRect.center().y()));
}

QRect currentLineHighlightRectForCursor(
    const PlainCodeEditor* editor,
    const QTextCursor& cursor,
    int blockSpacingPixels)
{
    if (editor == nullptr || editor->viewport() == nullptr) {
        return QRect();
    }

    const QTextBlock block = cursor.block();
    if (!block.isValid()) {
        return QRect();
    }

    const BlockVisualSpan blockSpan = visibleBlockSpan(editor, block);
    int blockTop = 0;
    int blockHeight = 0;
    if (blockSpan.isValid()) {
        blockTop = blockSpan.top();
        blockHeight = blockSpan.height();
    } else {
        const QRect caretRect = editor->cursorRect(cursor);
        if (!caretRect.isValid()) {
            return QRect();
        }
        blockTop = caretRect.top();
        blockHeight = qMax(1, caretRect.height());
    }
    const int verticalExpand = qMax(0, qRound(static_cast<qreal>(blockSpacingPixels) / 8.0));
    QRect highlightRect(
        kCurrentLineHighlightLeftInset,
        blockTop - verticalExpand,
        editor->viewport()->width() - kCurrentLineHighlightLeftInset - kCurrentLineHighlightRightInset,
        blockHeight + (verticalExpand * 2)
    );
    highlightRect = highlightRect.intersected(editor->viewport()->rect());
    highlightRect.adjust(0, 0, -1, -1);
    return highlightRect.isValid() ? highlightRect : QRect();
}
}  // namespace

QRect PlainCodeEditor::currentLineHighlightRect() const
{
    return currentLineHighlightRectForCursor(this, textCursor(), blockSpacingPixels_);
}

QRect PlainCodeEditor::previewFollowVisualCaretRect() const
{
    if (!previewFollowVisualCaretActive_ || document() == nullptr || viewport() == nullptr) {
        return QRect();
    }

    const int normalizedLine = qMax(1, previewFollowVisualCaretLine_);
    QTextBlock block = document()->findBlockByNumber(normalizedLine - 1);
    if (!block.isValid()) {
        return QRect();
    }

    QTextCursor cursor(document());
    cursor.setPosition(block.position() + qBound(0, previewFollowVisualCaretCol_ - 1, block.text().size()));
    QRect caretRect = cursorRect(cursor);
    if (!caretRect.isValid()) {
        return QRect();
    }
    caretRect = caretRect.intersected(viewport()->rect());
    return caretRect.isValid() ? caretRect : QRect();
}

QPointF PlainCodeEditor::normalizedViewportHitPosition(const QPointF& position) const
{
    return normalizedViewportHitPositionForBlockSpacing(this, position);
}

void PlainCodeEditor::updateCursorVisibility()
{
    // Cursor width tracks the typing mode:
    //   - insert: pinned at kEditorCursorVisibleWidth (1 px caret)
    //   - overwrite: widened to a space-advance block (floored at 6 px
    //     so micro-fonts still render a visible block)
    // This function runs both on every cursorPositionChanged (via
    // syncCursorVisualState) and on the Insert toggle, so the mode's
    // visual stays consistent across arrow-key moves, click-to-position,
    // and re-focus.
    if (overwriteMode()) {
        const QFontMetrics fm(font());
        const int spaceAdvance = fm.horizontalAdvance(QLatin1Char(' '));
        setCursorWidth(qMax(6, spaceAdvance));
    } else {
        setCursorWidth(kEditorCursorVisibleWidth);
    }
}

void PlainCodeEditor::syncCursorVisualState()
{
    const QRect previousHighlightRect = lastCurrentLineHighlightRect_;
    updateCursorVisibility();
    const QRect currentHighlightRect = currentLineHighlightRect();
    lastCurrentLineHighlightRect_ = currentHighlightRect;
    updateCurrentLineHighlightRegion(previousHighlightRect, currentHighlightRect);
}

void PlainCodeEditor::updateCurrentLineHighlightRegion(const QRect& previousRect, const QRect& currentRect)
{
    if (viewport() == nullptr) {
        return;
    }

    const QRect viewportRect = viewport()->rect();
    if (previousRect.isValid() && previousRect != currentRect) {
        const QRect expandedPreviousRect =
            previousRect.adjusted(
                -kCurrentLineHighlightDirtyMargin,
                -kCurrentLineHighlightDirtyMargin,
                kCurrentLineHighlightDirtyMargin,
                kCurrentLineHighlightDirtyMargin)
                .intersected(viewportRect);
        if (expandedPreviousRect.isValid()) {
            viewport()->update(expandedPreviousRect);
        }
    }

    QRect dirtyRect;
    if (previousRect.isValid()) {
        dirtyRect = previousRect;
    }
    if (currentRect.isValid()) {
        dirtyRect = dirtyRect.isNull() ? currentRect : dirtyRect.united(currentRect);
    }

    if (!dirtyRect.isValid()) {
        viewport()->update();
        return;
    }
    const QRect expandedDirtyRect =
        dirtyRect.adjusted(
            -kCurrentLineHighlightDirtyMargin,
            -kCurrentLineHighlightDirtyMargin,
            kCurrentLineHighlightDirtyMargin,
            kCurrentLineHighlightDirtyMargin)
            .intersected(viewportRect);
    viewport()->update(expandedDirtyRect);
}

void PlainCodeEditor::setPreviewFollowVisualCaret(bool active, int line, int col)
{
    const bool normalizedActive = active;
    const int normalizedLine = qMax(1, line);
    const int normalizedCol = qMax(1, col);
    if (previewFollowVisualCaretActive_ == normalizedActive
        && previewFollowVisualCaretLine_ == normalizedLine
        && previewFollowVisualCaretCol_ == normalizedCol) {
        return;
    }

    const QRect previousRect = previewFollowVisualCaretRect();
    previewFollowVisualCaretActive_ = normalizedActive;
    previewFollowVisualCaretLine_ = normalizedLine;
    previewFollowVisualCaretCol_ = normalizedCol;
    const QRect currentRect = previewFollowVisualCaretRect();

    if (viewport() == nullptr) {
        return;
    }
    QRect dirtyRect;
    if (previousRect.isValid()) {
        dirtyRect = previousRect;
    }
    if (currentRect.isValid()) {
        dirtyRect = dirtyRect.isNull() ? currentRect : dirtyRect.united(currentRect);
    }
    if (!dirtyRect.isValid()) {
        return;
    }
    viewport()->update(dirtyRect.adjusted(-1, -1, 1, 1).intersected(viewport()->rect()));
}

bool PlainCodeEditor::applyPreviewFollowCursor(const QTextCursor& cursor, bool centerView, bool suppressSignals)
{
    if (document() == nullptr) {
        return false;
    }

    const int oldVScroll = !centerView && verticalScrollBar() != nullptr ? verticalScrollBar()->value() : 0;
    const int oldHScroll = !centerView && horizontalScrollBar() != nullptr ? horizontalScrollBar()->value() : 0;

    if (suppressSignals) {
        QSignalBlocker blocker(this);
        setTextCursor(cursor);
    } else {
        setTextCursor(cursor);
    }

    syncCursorVisualState();

    if (centerView) {
        if (QScrollBar* vbar = verticalScrollBar()) {
            const QRect caretRect = cursorRect();
            const int centeredValue = vbar->value() + caretRect.center().y() - (viewport()->height() / 2);
            vbar->setValue(qBound(vbar->minimum(), centeredValue, vbar->maximum()));
        }
    } else {
        if (QScrollBar* vbar = verticalScrollBar()) {
            vbar->setValue(qBound(vbar->minimum(), oldVScroll, vbar->maximum()));
        }
        if (QScrollBar* hbar = horizontalScrollBar()) {
            hbar->setValue(qBound(hbar->minimum(), oldHScroll, hbar->maximum()));
        }
    }
    return true;
}

void PlainCodeEditor::paintEvent(QPaintEvent* event)
{
    {
        QPainter backgroundPainter(viewport());
        if (backgroundPainter.isActive()) {
            const QRect dirtyRect = event != nullptr ? event->rect() : viewport()->rect();
            backgroundPainter.setClipRect(dirtyRect);
            if (miacode::ui::paintAppBackgroundForWidget(viewport(), backgroundPainter)) {
                const UiTheme::Colors& c = UiTheme::colors();
                QColor editorSurface = c.inputBg;
                editorSurface.setAlpha(c.dark ? 196 : 204);
                backgroundPainter.fillRect(dirtyRect, editorSurface);
            }
        }
    }
    QTextEdit::paintEvent(event);
    QPainter painter(viewport());
    const QRect highlightRect = currentLineHighlightRect();
    lastCurrentLineHighlightRect_ = highlightRect;
    if (highlightRect.isValid()) {
        const UiTheme::Colors& c = UiTheme::colors();
        painter.setPen(QPen(c.borderSoft, 1));
        painter.setBrush(Qt::NoBrush);
        painter.drawRoundedRect(highlightRect, 4.0, 4.0);
    }

    if (!hasFocus() && previewFollowVisualCaretActive_) {
        const QRect caretRect = previewFollowVisualCaretRect();
        if (caretRect.isValid()) {
            const UiTheme::Colors& c = UiTheme::colors();
            QColor caretColor = c.accent;
            caretColor.setAlpha(c.dark ? 232 : 212);
            painter.fillRect(QRect(caretRect.left(), caretRect.top(), qMax(1, kEditorCursorVisibleWidth), caretRect.height()), caretColor);
        }
    }
}
