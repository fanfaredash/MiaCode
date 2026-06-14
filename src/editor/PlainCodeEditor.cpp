#include "PlainCodeEditor.h"
#include "BracketCompletionPopup.h"
#include "SimaiCompletionCatalog.h"
#include "common/DebugLog.h"
#include "ShortcutRegistry.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAction>
#include <QApplication>
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

namespace miacode::editor {
QChar normalizedHalfWidthChar(QChar ch)
{
    const ushort code = ch.unicode();
    if (code == 0x3000) {
        return QLatin1Char(' ');
    }
    if (code >= 0xFF01 && code <= 0xFF5E) {
        return QChar(code - 0xFEE0);
    }
    switch (code) {
    case 0x3001:
        return QLatin1Char('/');
    case 0x3002:
        return QLatin1Char('.');
    case 0x00B7:
        return QLatin1Char('`');
    case 0x300A:
        return QLatin1Char('<');
    case 0x300B:
        return QLatin1Char('>');
    case 0x3010:
        return QLatin1Char('[');
    case 0x3011:
        return QLatin1Char(']');
    case 0xFFE5:
        return QLatin1Char('$');
    default:
        return ch;
    }
}

QString normalizedHalfWidthText(QString text)
{
    if (text == QStringLiteral("……") || text == QStringLiteral("…")) {
        return QStringLiteral("^");
    }
    for (int i = 0; i < text.size(); ++i) {
        text[i] = normalizedHalfWidthChar(text.at(i));
    }
    return text;
}

QString normalizedHalfWidthKeyText(const QKeyEvent* event, const QString& text)
{
    if (event != nullptr
        && (event->modifiers() & Qt::ShiftModifier)
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        if (event->key() == Qt::Key_6) {
            return QStringLiteral("^");
        }
        if (event->key() == Qt::Key_4) {
            return QStringLiteral("$");
        }
    }
    return normalizedHalfWidthText(text);
}

QString clearCompleteElementsInSelection(
    const QString& text,
    int selectionStart,
    int selectionEnd,
    int* changedCount)
{
    if (changedCount != nullptr) {
        *changedCount = 0;
    }
    if (text.isEmpty()) {
        return text;
    }

    const int boundedStart = qBound(0, selectionStart, text.size());
    const int boundedEnd = qBound(boundedStart, selectionEnd, text.size());
    if (boundedStart >= boundedEnd) {
        return text;
    }

    QString output;
    output.reserve(text.size());
    output.append(text.left(boundedStart));

    int segmentStart = boundedStart;
    const auto commentStartInLine = [&text](int start, int endExclusive) {
        const int index = text.indexOf(QStringLiteral("||"), start);
        return (index >= 0 && index < endExclusive) ? index : -1;
    };
    const int newlineBeforeSelection =
        boundedStart > 0 ? text.lastIndexOf(QLatin1Char('\n'), boundedStart - 1) : -1;
    int lineStart = newlineBeforeSelection + 1;
    int nextLineStart = text.indexOf(QLatin1Char('\n'), lineStart);
    if (nextLineStart < 0) {
        nextLineStart = text.size();
    } else {
        ++nextLineStart;
    }
    int commentStart = commentStartInLine(lineStart, nextLineStart);
    int squareDepth = 0;
    int braceDepth = 0;
    int parenDepth = 0;
    int changed = 0;

    const auto isTopLevel = [&]() {
        return squareDepth == 0 && braceDepth == 0 && parenDepth == 0;
    };
    const auto leadingPrefixEnd = [&](int start, int end) {
        int pos = start;
        while (pos < end) {
            const QChar opening = text.at(pos);
            QChar closing;
            if (opening == QLatin1Char('{')) {
                closing = QLatin1Char('}');
            } else if (opening == QLatin1Char('(')) {
                closing = QLatin1Char(')');
            } else {
                break;
            }
            const int closeIndex = text.indexOf(closing, pos + 1);
            if (closeIndex < 0 || closeIndex >= end) {
                break;
            }
            pos = closeIndex + 1;
        }
        return pos;
    };
    const auto isBoundaryBefore = [&](int pos) {
        if (pos <= 0) {
            return true;
        }
        const QChar previous = text.at(pos - 1);
        return previous == QLatin1Char(',')
            || previous == QLatin1Char('\n')
            || previous == QChar::ParagraphSeparator
            || previous == QChar::LineSeparator;
    };
    const auto partialLeadingPrefixEnd = [&](int start, int end) {
        const int openBrace = text.lastIndexOf(QLatin1Char('{'), start);
        const int openParen = text.lastIndexOf(QLatin1Char('('), start);
        const int openIndex = qMax(openBrace, openParen);
        if (openIndex < 0 || openIndex >= start || !isBoundaryBefore(openIndex)) {
            return start;
        }
        const QChar closing = text.at(openIndex) == QLatin1Char('{')
            ? QLatin1Char('}')
            : QLatin1Char(')');
        const int closeIndex = text.indexOf(closing, openIndex + 1);
        if (closeIndex < start || closeIndex >= end) {
            return start;
        }
        return closeIndex + 1;
    };
    const auto appendRawThrough = [&](int endExclusive) {
        output.append(text.mid(segmentStart, endExclusive - segmentStart));
        segmentStart = endExclusive;
    };
    const auto isElementBoundary = [&](int pos) {
        if (pos <= 0) {
            return true;
        }
        const QChar previous = text.at(pos - 1);
        if (previous == QLatin1Char(',')
            || previous == QLatin1Char('\n')
            || previous == QChar::ParagraphSeparator
            || previous == QChar::LineSeparator) {
            return true;
        }
        if (previous != QLatin1Char('}') && previous != QLatin1Char(')')) {
            return false;
        }
        const QChar opening = previous == QLatin1Char('}')
            ? QLatin1Char('{')
            : QLatin1Char('(');
        const int openIndex = text.lastIndexOf(opening, pos - 2);
        if (openIndex < 0) {
            return false;
        }
        if (openIndex == 0) {
            return true;
        }
        const QChar beforePrefix = text.at(openIndex - 1);
        return beforePrefix == QLatin1Char(',')
            || beforePrefix == QLatin1Char('\n')
            || beforePrefix == QChar::ParagraphSeparator
            || beforePrefix == QChar::LineSeparator;
    };
    const auto appendClearedSegment = [&](int commaIndex) {
        const int fullPrefixEnd = leadingPrefixEnd(segmentStart, commaIndex);
        const int partialPrefixEnd = partialLeadingPrefixEnd(segmentStart, commaIndex);
        const int prefixEnd = qMax(fullPrefixEnd, partialPrefixEnd);
        const bool canClear = commaIndex > prefixEnd
            && (isElementBoundary(segmentStart) || partialPrefixEnd > segmentStart);
        output.append(text.mid(segmentStart, prefixEnd - segmentStart));
        if (canClear) {
            ++changed;
            output.append(QLatin1Char(','));
        } else {
            output.append(text.mid(prefixEnd, commaIndex - prefixEnd + 1));
        }
        segmentStart = commaIndex + 1;
    };

    for (int i = boundedStart; i < boundedEnd; ++i) {
        while (i >= nextLineStart) {
            lineStart = nextLineStart;
            nextLineStart = text.indexOf(QLatin1Char('\n'), lineStart);
            if (nextLineStart < 0) {
                nextLineStart = text.size();
            } else {
                ++nextLineStart;
            }
            commentStart = commentStartInLine(lineStart, nextLineStart);
            squareDepth = 0;
            braceDepth = 0;
            parenDepth = 0;
        }
        if (commentStart >= lineStart && i >= commentStart) {
            // TODO: Ctrl+Q intentionally skips comment text for now. If comment
            // editing later needs this shortcut, add a separate comment-aware
            // grammar instead of reusing chart-token clearing.
            appendRawThrough(qMin(nextLineStart, boundedEnd));
            i = segmentStart - 1;
            continue;
        }
        const QChar ch = text.at(i);
        switch (ch.unicode()) {
        case '[':
            ++squareDepth;
            break;
        case ']':
            squareDepth = qMax(0, squareDepth - 1);
            break;
        case '{':
            ++braceDepth;
            break;
        case '}':
            braceDepth = qMax(0, braceDepth - 1);
            break;
        case '(':
            ++parenDepth;
            break;
        case ')':
            parenDepth = qMax(0, parenDepth - 1);
            break;
        case ',':
            if (isTopLevel()) {
                appendClearedSegment(i);
            }
            break;
        default:
            break;
        }
    }

    output.append(text.mid(segmentStart, boundedEnd - segmentStart));
    output.append(text.mid(boundedEnd));

    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}
}

namespace {
constexpr int kLineNumberLeftPadding = 6;
constexpr int kLineNumberRightPadding = 10;
constexpr int kLineNumberMinWidth = 40;
constexpr int kLineNumberDropHitSlopRight = 56;
constexpr qreal kEditorDocumentLeftInset = 14.0;
constexpr int kCurrentLineHighlightLeftInset = 3;
constexpr int kCurrentLineHighlightRightInset = 0;
constexpr int kCurrentLineHighlightDirtyMargin = 2;
constexpr int kEditorCursorVisibleWidth = 1;

void logSelectionRestoreEditorShortcut(const QString& scope, const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("selection_restore/%1").arg(scope),
        payload,
        true
    );
}

QKeySequence keySequenceForEvent(const QKeyEvent* event)
{
    if (event == nullptr) {
        return QKeySequence();
    }
    return QKeySequence(
        (event->modifiers() & (Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        | event->key());
}

bool matchesShortcutId(const QKeyEvent* event, const QString& id, const QList<QKeySequence>& fallback)
{
    if (event == nullptr) {
        return false;
    }
    const QKeySequence pressed = keySequenceForEvent(event);
    const QList<QKeySequence> sequences = ShortcutRegistry::instance().sequences(id, fallback);
    for (const QKeySequence& sequence : sequences) {
        if (!sequence.isEmpty() && pressed == sequence) {
            return true;
        }
        const QString portable = sequence.toString(QKeySequence::PortableText);
        if (portable == QStringLiteral("Ctrl+Shift+=")
            && (pressed == QKeySequence(QStringLiteral("Ctrl++"))
                || pressed == QKeySequence(QStringLiteral("Ctrl+Shift++")))) {
            return true;
        }
        if (portable == QStringLiteral("Ctrl+Shift+-")
            && (pressed == QKeySequence(QStringLiteral("Ctrl+_"))
                || pressed == QKeySequence(QStringLiteral("Ctrl+Shift+_")))) {
            return true;
        }
    }
    return false;
}

void insertLineBreakAtCursor(QTextEdit* editor)
{
    if (editor == nullptr || editor->isReadOnly()) {
        return;
    }
    QTextCursor cursor = editor->textCursor();
    cursor.beginEditBlock();
    cursor.insertBlock(cursor.blockFormat(), cursor.charFormat());
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
}

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

class LineNumberArea : public QWidget
{
public:
    explicit LineNumberArea(PlainCodeEditor* editor)
        : QWidget(editor), editor_(editor)
    {
        setAcceptDrops(true);
    }

    QSize sizeHint() const override
    {
        return QSize(editor_->lineNumberAreaWidth(), 0);
    }

protected:
    void dragEnterEvent(QDragEnterEvent* event) override
    {
        editor_->dragEnterEvent(event);
    }

    void dragMoveEvent(QDragMoveEvent* event) override
    {
        editor_->dragMoveEvent(event);
    }

    void dragLeaveEvent(QDragLeaveEvent* event) override
    {
        editor_->dragLeaveEvent(event);
    }

    void dropEvent(QDropEvent* event) override
    {
        editor_->dropEvent(event);
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        const int line = editor_->lineNumberAtAreaPosition(event->pos());
        if (line > 0) {
            emit editor_->lineNumberBookmarkActivated(line);
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        const int line = editor_->lineNumberAtAreaPosition(event->pos());
        editor_->pressedBookmarkLine_ = editor_->bookmarkedLines_.contains(line) ? line : -1;
        editor_->lineNumberPressPos_ = event->pos();
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (event != nullptr
            && (event->buttons() & Qt::LeftButton)
            && editor_->pressedBookmarkLine_ > 0
            && (event->pos() - editor_->lineNumberPressPos_).manhattanLength() >= QApplication::startDragDistance()) {
            auto* drag = new QDrag(this);
            auto* mime = new QMimeData;
            mime->setData(QStringLiteral("application/x-miacode-bookmark-move"), QByteArray::number(editor_->pressedBookmarkLine_));
            drag->setMimeData(mime);
            drag->exec(Qt::MoveAction);
            editor_->pressedBookmarkLine_ = -1;
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void paintEvent(QPaintEvent* event) override
    {
        editor_->lineNumberAreaPaintEvent(event);
    }

private:
    PlainCodeEditor* editor_;
};

PlainCodeEditor::PlainCodeEditor(QWidget* parent)
    : QTextEdit(parent), lineNumberArea_(new LineNumberArea(this))
{
    lineNumberArea_->setFont(font());
    connect(document(), &QTextDocument::blockCountChanged, this, &PlainCodeEditor::updateLineNumberAreaWidth);
    connect(this, &QTextEdit::textChanged, this, [this]() {
        updateLineNumberAreaWidth(0);
        updateLineNumberArea();
    });
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
        Q_UNUSED(value);
        updateLineNumberArea();
    });
    connect(this, &QTextEdit::cursorPositionChanged, this, [this]() {
        syncCursorVisualState();
        // While the bracket-completion popup is open, a caret move means the
        // user typed/deleted a filter char (or clicked away) — re-filter or
        // dismiss. Programmatic edits (open/accept) raise suppressCompletionFilter_.
        if (!suppressCompletionFilter_ && bracketCompletionActive()) {
            updateBracketCompletionFilter();
        }
    });
    updateLineNumberAreaWidth(0);
    setLineWrapMode(QTextEdit::NoWrap);
    setAcceptRichText(false);
    setAcceptDrops(true);
    setHalfWidthInputEnabled(halfWidthInputEnabled_);
    if (QTextFrame* frame = document()->rootFrame(); frame != nullptr) {
        QTextFrameFormat format = frame->frameFormat();
        format.setLeftMargin(kEditorDocumentLeftInset);
        frame->setFrameFormat(format);
    }
    setCursorWidth(kEditorCursorVisibleWidth);
    updateCursorVisibility();
    lastCurrentLineHighlightRect_ = currentLineHighlightRect();
}

void PlainCodeEditor::setBlockSpacingPixels(int px)
{
    blockSpacingPixels_ = qMax(0, px);
    QTextCursor cursor(document());
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    QTextBlockFormat fmt;
    fmt.setBottomMargin(static_cast<qreal>(blockSpacingPixels_));
    cursor.mergeBlockFormat(fmt);
    cursor.endEditBlock();
}

void PlainCodeEditor::setTopOverlayInsetPixels(int px)
{
    const int normalized = qMax(0, px);
    if (topOverlayInsetPixels_ == normalized) {
        return;
    }
    topOverlayInsetPixels_ = normalized;
    updateLineNumberAreaWidth(0);
    updateLineNumberArea();
}

void PlainCodeEditor::refreshLineNumberAreaLayout()
{
    lineNumberArea_->setFont(font());
    updateLineNumberAreaWidth(0);
    const QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    lineNumberArea_->update();
    viewport()->update();
}

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
    const QPoint areaPos = lineNumberArea_->mapFromGlobal(globalPos);
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

void PlainCodeEditor::setBatchTransformActions(const QList<QAction*>& actions)
{
    batchTransformActions_ = actions;
}

void PlainCodeEditor::setMoreBatchTransformActions(const QList<QAction*>& actions)
{
    moreBatchTransformActions_ = actions;
}

void PlainCodeEditor::setHalfWidthInputEnabled(bool enabled)
{
    halfWidthInputEnabled_ = enabled;
    Qt::InputMethodHints hints = inputMethodHints();
    if (enabled) {
        hints |= Qt::ImhLatinOnly;
    } else {
        hints &= ~Qt::ImhLatinOnly;
    }
    setInputMethodHints(hints);
}

void PlainCodeEditor::setImeInputDisabled(bool disabled)
{
    // [BETA51 IME-DISABLE: DISABLED PENDING FIX]
    // Setter is kept on the public API but every caller in the project has
    // been commented out (see MainWindow.EditorDisplay.cpp + the preferences
    // dialog). Qt::WA_InputMethodEnabled does *not* prevent Windows CJK IMEs
    // from opening their composition window on this widget — the user
    // reported the toggle had no effect post-4f9a27e. The eventual fix
    // probably needs to wire ImmAssociateContext / TSF directly on the
    // widget's native window handle; that's outside the Qt attribute surface.
    // Until then this method records the requested state but does not touch
    // the WA attribute, so external callers (if any new ones appear) don't
    // think the feature is live.
    imeInputDisabled_ = disabled;
#if 0
    setAttribute(Qt::WA_InputMethodEnabled, !disabled);
#endif
}

void PlainCodeEditor::setEditorOverwriteMode(bool enabled)
{
    if (overwriteMode() == enabled) {
        return;
    }
    setOverwriteMode(enabled);
    // Refresh the caret width through the shared cursorPositionChanged
    // code path so the mode visual stays consistent whether the toggle
    // came from Insert, the Preferences dialog, or anywhere else.
    updateCursorVisibility();
    viewport()->update();
    emit editorOverwriteModeChanged(enabled);
}

void PlainCodeEditor::setAutoCompletionEnabled(bool enabled)
{
    autoCompletionEnabled_ = enabled;
    if (!enabled) {
        closeBracketCompletion();
    }
}

void PlainCodeEditor::setWholeBpmCandidate(const QString& bpm)
{
    wholeBpmCandidate_ = bpm.trimmed();
}

bool PlainCodeEditor::event(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent == nullptr) {
            return QTextEdit::event(event);
        }
        if (matchesShortcutId(
                keyEvent,
                QStringLiteral("transform.clear_complete_elements"),
                {QKeySequence(Qt::CTRL | Qt::Key_Q)})
            || matchesShortcutId(
                keyEvent,
                QStringLiteral("transform.subdivision_half_up"),
                {QKeySequence(QStringLiteral("Ctrl+Shift+=")), QKeySequence(QStringLiteral("Ctrl++"))})
            || matchesShortcutId(
                keyEvent,
                QStringLiteral("transform.subdivision_half_down"),
                {QKeySequence(QStringLiteral("Ctrl+Shift+-"))})) {
            event->accept();
            return true;
        }
        if (keyEvent->matches(QKeySequence::Undo) || keyEvent->matches(QKeySequence::Redo)) {
            logSelectionRestoreEditorShortcut(
                QStringLiteral("editor_shortcut_override"),
                QStringLiteral("match=%1 key=%2 modifiers=%3 focus=%4")
                    .arg(keyEvent->matches(QKeySequence::Undo) ? QStringLiteral("undo") : QStringLiteral("redo"))
                    .arg(keyEvent->key())
                    .arg(static_cast<int>(keyEvent->modifiers()))
                    .arg(hasFocus() ? 1 : 0)
            );
            event->accept();
            return true;
        }
    }
    return QTextEdit::event(event);
}

int PlainCodeEditor::lineNumberAreaWidth() const
{
    int digits = 1;
    int max = qMax(1, document() != nullptr ? document()->blockCount() : 1);
    while (max >= 10) {
        max /= 10;
        ++digits;
    }

    const QFontMetrics metrics(lineNumberArea_->font());
    const int digitWidth = metrics.horizontalAdvance(QLatin1Char('9'));
    const int space = kLineNumberLeftPadding + (digitWidth * digits) + kLineNumberRightPadding;
    return qMax(kLineNumberMinWidth, space);
}

void PlainCodeEditor::updateLineNumberAreaWidth(int)
{
    setViewportMargins(lineNumberAreaWidth(), topOverlayInsetPixels_, 0, 0);
}

void PlainCodeEditor::updateLineNumberArea()
{
    lineNumberArea_->update();
}

void PlainCodeEditor::resizeEvent(QResizeEvent* event)
{
    QTextEdit::resizeEvent(event);

    const QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
}

void PlainCodeEditor::changeEvent(QEvent* event)
{
    QTextEdit::changeEvent(event);
    if (event != nullptr && event->type() == QEvent::FontChange) {
        refreshLineNumberAreaLayout();
    }
}

void PlainCodeEditor::focusInEvent(QFocusEvent* event)
{
    const QRect previousRect = previewFollowVisualCaretRect();
    QTextEdit::focusInEvent(event);
    if (viewport() != nullptr && previousRect.isValid()) {
        viewport()->update(previousRect.adjusted(-1, -1, 1, 1).intersected(viewport()->rect()));
    }
}

void PlainCodeEditor::focusOutEvent(QFocusEvent* event)
{
    // The popup never takes focus, so losing focus means the user clicked or
    // tabbed away — dismiss any open suggestion list.
    closeBracketCompletion();
    const QRect previousRect = previewFollowVisualCaretRect();
    QTextEdit::focusOutEvent(event);
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
    if (dirtyRect.isValid()) {
        viewport()->update(dirtyRect.adjusted(-1, -1, 1, 1).intersected(viewport()->rect()));
    }
}

void PlainCodeEditor::contextMenuEvent(QContextMenuEvent* event)
{
    if (event == nullptr) {
        return;
    }
    auto* menu = new QMenu(this);
    UiTheme::styleRoundedMenu(*menu);
    const UiTheme::Colors& c = UiTheme::colors();
    const QColor contextMenuBg = c.dark ? QColor(QStringLiteral("#2B3543")) : QColor(QStringLiteral("#FFFFFF"));
    const QColor contextMenuBorder = c.dark ? QColor(QStringLiteral("#667A91")) : c.menuBorder;
    const QColor contextMenuHover = c.dark ? QColor(QStringLiteral("#3D4A5C")) : c.menuHoverBg;
    const QColor contextMenuSeparator = c.dark ? QColor(QStringLiteral("#73859A")) : c.border;
    menu->setStyleSheet(
        QStringLiteral(
            "QMenu { background: %1; border: 1px solid %2; border-radius: 8px; padding: 7px; }"
            "QMenu::item { padding: 4px 12px; margin: 1px 4px; min-height: 20px; border-radius: 6px; color: %3; background: transparent; }"
            "QMenu::item:selected { background: %4; color: %3; }"
            "QMenu::item:disabled { color: %5; background: transparent; }"
            "QMenu::separator { height: 1px; margin: 7px 6px; background: %6; }"
        )
            .arg(contextMenuBg.name(QColor::HexRgb))
            .arg(contextMenuBorder.name(QColor::HexRgb))
            .arg(c.textPrimary.name(QColor::HexRgb))
            .arg(contextMenuHover.name(QColor::HexRgb))
            .arg(c.menuDisabledText.name(QColor::HexRgb))
            .arg(contextMenuSeparator.name(QColor::HexRgb))
    );

    const auto translated = [](const QString& key, const QString& fallback) {
        const QString value = UiText::text(key);
        return value.isEmpty() ? fallback : value;
    };

    auto* cutAction = menu->addAction(translated(QStringLiteral("action.cut"), QStringLiteral("Cut")));
    ShortcutRegistry::instance().applyShortcut(
        cutAction,
        QStringLiteral("edit.cut"),
        QKeySequence::Cut);
    cutAction->setShortcutVisibleInContextMenu(true);
    cutAction->setEnabled(textCursor().hasSelection());
    connect(cutAction, &QAction::triggered, this, &QTextEdit::cut);

    auto* copyAction = menu->addAction(translated(QStringLiteral("action.copy"), QStringLiteral("Copy")));
    ShortcutRegistry::instance().applyShortcut(
        copyAction,
        QStringLiteral("edit.copy"),
        QKeySequence::Copy);
    copyAction->setShortcutVisibleInContextMenu(true);
    copyAction->setEnabled(textCursor().hasSelection());
    connect(copyAction, &QAction::triggered, this, &QTextEdit::copy);

    auto* pasteAction = menu->addAction(translated(QStringLiteral("action.paste"), QStringLiteral("Paste")));
    ShortcutRegistry::instance().applyShortcut(
        pasteAction,
        QStringLiteral("edit.paste"),
        QKeySequence::Paste);
    pasteAction->setShortcutVisibleInContextMenu(true);
    pasteAction->setEnabled(canPaste());
    connect(pasteAction, &QAction::triggered, this, &QTextEdit::paste);

    const auto addStyledSubmenu = [&](const QString& title, const QList<QAction*>& actions) {
        auto* submenu = menu->addMenu(title);
        UiTheme::styleRoundedMenu(*submenu);
        submenu->setStyleSheet(
            QStringLiteral(
                "QMenu { background: %1; border: 1px solid %2; border-radius: 8px; padding: 7px; }"
                "QMenu::item { padding: 4px 12px; margin: 1px 4px; min-height: 20px; border-radius: 6px; color: %3; background: transparent; }"
                "QMenu::item:selected { background: %4; color: %3; }"
                "QMenu::item:disabled { color: %5; background: transparent; }"
                "QMenu::separator { height: 1px; margin: 7px 6px; background: %6; }"
            )
                .arg(contextMenuBg.name(QColor::HexRgb))
                .arg(contextMenuBorder.name(QColor::HexRgb))
                .arg(c.textPrimary.name(QColor::HexRgb))
                .arg(contextMenuHover.name(QColor::HexRgb))
                .arg(c.menuDisabledText.name(QColor::HexRgb))
                .arg(contextMenuSeparator.name(QColor::HexRgb))
        );
        for (QAction* action : actions) {
            if (action == nullptr) {
                continue;
            }
            submenu->addAction(action);
        }
    };

    if (!batchTransformActions_.isEmpty() || !moreBatchTransformActions_.isEmpty()) {
        menu->addSeparator();
        if (!batchTransformActions_.isEmpty()) {
            addStyledSubmenu(
                translated(QStringLiteral("context.batch_transform"), QStringLiteral("Batch Transform")),
                batchTransformActions_
            );
        }
        if (!moreBatchTransformActions_.isEmpty()) {
            addStyledSubmenu(
                translated(QStringLiteral("context.more_transform"), QStringLiteral("More...")),
                moreBatchTransformActions_
            );
        }
    }

    menu->exec(event->globalPos());
    delete menu;
}

// Auto-close brackets — when the user types {, [, or (, insert the matching
// closing glyph immediately after and park the caret in between. Shared by the
// keyPressEvent path and the IME commit path (inputMethodEvent) so a bracket
// delivered either as a raw key or as an IME commit string — including a
// full-width 「（」/「【」/「｛」 normalized to half-width before we get here —
// gets paired the same way. The bracket-scope highlighter sees the new closing
// char through the document-changed signal and re-colors the block on the next
// paint, so the matched-pair color (and its cross-line scope) stays consistent.
bool PlainCodeEditor::tryAutoCloseBracket(const QString& text)
{
    if (!autoCompletionEnabled_ || text.size() != 1) {
        return false;
    }
    QChar opening;
    QChar closing;
    switch (text.at(0).toLatin1()) {
    case '{': opening = QLatin1Char('{'); closing = QLatin1Char('}'); break;
    case '[': opening = QLatin1Char('['); closing = QLatin1Char(']'); break;
    case '(': opening = QLatin1Char('('); closing = QLatin1Char(')'); break;
    default: return false;
    }
    if (isReadOnly() || overwriteMode()) {
        return false;
    }
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    if (cursor.hasSelection()) {
        // Wrap the selection: {<sel>}. The caret ends up at the closing
        // bracket which mirrors the standard editor "surround with pair".
        const QString selected = cursor.selectedText();
        cursor.insertText(QString(opening) + selected + QString(closing));
        cursor.endEditBlock();
    } else {
        cursor.insertText(QString(opening) + QString(closing));
        cursor.movePosition(QTextCursor::PreviousCharacter);
        cursor.endEditBlock();
        setTextCursor(cursor);
    }
    return true;
}

// Bracket input that also offers a completion popup. Wraps tryAutoCloseBracket
// so the suggestion list opens on the same normalized key/IME bracket. Returns
// true when the bracket was consumed (paired and popup opened).
bool PlainCodeEditor::tryBracketInput(const QString& text)
{
    if (text.size() != 1) {
        return false;
    }
    const QChar opening = text.at(0);
    if (!miacode::editor::isBracketOpening(opening)) {
        return false;
    }
    const bool hadSelection = textCursor().hasSelection();
    if (tryAutoCloseBracket(text)) {
        // The auto-close path inserts "[]" (caret parked between) for the no-
        // selection case, or wraps the selection. Only the empty-pair case is a
        // completion slot — wrapping already filled the brackets.
        if (!hadSelection) {
            maybeOpenBracketCompletion(opening, /*closingPresent=*/true);
        }
        return true;
    }
    // Auto-close declined (preference off, read-only, overwrite): leave the
    // bracket to the normal insert path. Completion follows the same single
    // preference as auto-close, so there is no completion-without-close case.
    return false;
}

// simai "hold" shortcut: typing a lowercase `h` inserts a bare `h` and offers
// the full-bracket hold-duration tokens ("[8:1]" …) as a completion popup —
// simai hold notation is `<lane>h[<beats>:<ticks>]`. Crucially it inserts NO
// bracket of its own, so a following `[` produces the normal `h[]` rather than
// the old contradictory `h[[]]`; the suggestion supplies the whole `[...]`.
// We require EXACTLY a single lowercase `h` so Shift+H, Ctrl+H, full-width 「ｈ」
// etc. fall through to a normal insert. Wired to the keyPressEvent path ONLY —
// CJK IMEs routinely commit stray latin letters mid-composition and triggering
// this on those would be disruptive.
bool PlainCodeEditor::tryHoldExpand(const QString& text)
{
    if (!autoCompletionEnabled_ || text != QLatin1String("h")) {
        return false;
    }
    if (isReadOnly() || overwriteMode()) {
        return false;
    }
    // A completion popup is already filtering — let the `h` be a normal filter
    // character instead of spawning a second, nested suggestion slot.
    if (bracketCompletionActive()) {
        return false;
    }
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        // Wrap the selection: h[<sel>]. Mirrors auto-close's surround-with-pair.
        const QString selected = cursor.selectedText();
        cursor.beginEditBlock();
        cursor.insertText(QStringLiteral("h[") + selected + QStringLiteral("]"));
        cursor.endEditBlock();
        setTextCursor(cursor);
        return true;
    }
    cursor.insertText(QStringLiteral("h"));
    setTextCursor(cursor);
    // Don't suggest when the caret already sits right before a '[': the user is
    // hold-filling the existing bracket, so the full-bracket tokens would only
    // duplicate it. The bare 'h' was still the right insert (yields `h[`).
    QTextCursor rightProbe(document());
    rightProbe.setPosition(cursor.position());
    if (rightProbe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor)
        && rightProbe.selectedText() == QLatin1String("[")) {
        return true;
    }
    maybeOpenHoldCompletion();
    return true;
}

// Backspace inside an empty matching pair (`(|)`, `[|]`, `{|}`) deletes BOTH
// glyphs as a single undo step — the inverse of auto-close's pair insertion, so
// it's gated by the same preference. Ctrl/Alt/Meta+Backspace (word delete) and
// any selection fall through to the default handling.
bool PlainCodeEditor::tryDeleteBracketPair(QKeyEvent* event)
{
    if (!autoCompletionEnabled_ || event->key() != Qt::Key_Backspace) {
        return false;
    }
    if (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier)) {
        return false;
    }
    if (isReadOnly() || overwriteMode()) {
        return false;
    }
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        return false;
    }
    const int position = cursor.position();

    QTextCursor leftProbe(document());
    leftProbe.setPosition(position);
    if (!leftProbe.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor)) {
        return false;
    }
    const QString leftText = leftProbe.selectedText();
    if (leftText.size() != 1 || !miacode::editor::isBracketOpening(leftText.at(0))) {
        return false;
    }
    const QChar closing = miacode::editor::closingBracketFor(leftText.at(0));

    QTextCursor rightProbe(document());
    rightProbe.setPosition(position);
    if (!rightProbe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor)
        || rightProbe.selectedText() != QString(closing)) {
        return false;
    }

    // An open completion popup is anchored to this slot — dismiss it first.
    if (bracketCompletionActive()) {
        closeBracketCompletion();
    }
    suppressCompletionFilter_ = true;
    cursor.beginEditBlock();
    cursor.setPosition(position - 1);
    cursor.setPosition(position + 1, QTextCursor::KeepAnchor);
    cursor.removeSelectedText();
    cursor.endEditBlock();
    setTextCursor(cursor);
    suppressCompletionFilter_ = false;
    event->accept();
    return true;
}

// Type-over: when the user types a closing bracket and the identical glyph is
// already sitting immediately to the right of the caret, just hop past it rather
// than inserting a second one. This is the natural complement to auto-close — the
// pair you opened parks the caret at `[|]`, and typing the `]` you see walks the
// caret out instead of leaving `[]|]`. Gated by the same preference as auto-close.
// A selection, overwrite mode, or read-only falls through to a normal insert.
bool PlainCodeEditor::tryOverwriteClosingBracket(const QString& text)
{
    if (!autoCompletionEnabled_ || text.size() != 1) {
        return false;
    }
    const QChar closing = text.at(0);
    if (!miacode::editor::isBracketClosing(closing)) {
        return false;
    }
    if (isReadOnly() || overwriteMode()) {
        return false;
    }
    QTextCursor cursor = textCursor();
    if (cursor.hasSelection()) {
        return false;
    }
    QTextCursor rightProbe(document());
    rightProbe.setPosition(cursor.position());
    if (!rightProbe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor)
        || rightProbe.selectedText() != QString(closing)) {
        return false;
    }
    // Stepping over a glyph the popup might be filtering against would leave a
    // stale list anchored to the old slot — dismiss it first.
    if (bracketCompletionActive()) {
        closeBracketCompletion();
    }
    cursor.movePosition(QTextCursor::NextCharacter);
    setTextCursor(cursor);
    return true;
}

void PlainCodeEditor::ensureCompletionPopup()
{
    if (completionPopup_ != nullptr) {
        return;
    }
    completionPopup_ = new BracketCompletionPopup(this);
    const auto& colors = UiTheme::colors();
    completionPopup_->setStyleSheet(QStringLiteral(
        "QListWidget {"
        "  background-color: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  outline: 0;"
        "  padding: 2px;"
        "}"
        "QListWidget::item {"
        "  padding: 3px 10px;"
        "  border-radius: 3px;"
        "}"
        "QListWidget::item:selected {"
        "  background-color: %4;"
        "  color: %5;"
        "}")
        .arg(colors.panelBg.name(QColor::HexRgb))
        .arg(colors.textPrimary.name(QColor::HexRgb))
        .arg(colors.border.name(QColor::HexRgb))
        .arg(colors.accent.name(QColor::HexRgb))
        .arg(colors.accentText.name(QColor::HexRgb)));
    // A mouse click commits the highlighted row.
    connect(completionPopup_, &BracketCompletionPopup::candidateActivated, this,
            [this](const QString&) { acceptCompletionCandidate(); });
}

// Shared core: anchor the popup under the caret and record the slot state.
// `opening` governs the filter's "left the slot" check (via its matching close);
// `closingPresent` says whether an auto-inserted close sits to the right so the
// accept path can swallow it. No-op when there is nothing to suggest.
void PlainCodeEditor::openCompletionPopup(const QStringList& candidates, QChar opening, bool closingPresent)
{
    if (candidates.isEmpty()) {
        return;
    }
    completionOpening_ = opening;
    completionClosingPresent_ = closingPresent;
    completionStartPos_ = textCursor().position();
    ensureCompletionPopup();
    const QRect caret = cursorRect();
    const QPoint anchor = viewport()->mapToGlobal(caret.bottomLeft() + QPoint(0, 2));
    completionPopup_->showCandidates(candidates, anchor);
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("bracket_completion/open"),
        QStringLiteral("glyph=%1 candidates=%2 closing=%3")
            .arg(opening)
            .arg(candidates.size())
            .arg(closingPresent ? 1 : 0));
}

// Opens the suggestion popup for the bracket the caret now sits inside. closing-
// Present says whether a matching closing glyph was auto-inserted to the right.
void PlainCodeEditor::maybeOpenBracketCompletion(QChar opening, bool closingPresent)
{
    if (!autoCompletionEnabled_ || isReadOnly() || overwriteMode()) {
        return;
    }
    // e.g. '(' before any BPM is known yields an empty list — nothing to show.
    openCompletionPopup(
        miacode::editor::candidatesForOpening(opening, wholeBpmCandidate_, toPlainText()),
        opening,
        closingPresent);
}

// Opens the 'h' hold-duration suggestions ("[8:1]" …) anchored right after the
// just-typed 'h'. No bracket was inserted (closingPresent=false); completion-
// Opening is '[' so the filter's "left the slot" check keys off ']', matching
// the '[' duration popup.
void PlainCodeEditor::maybeOpenHoldCompletion()
{
    if (!autoCompletionEnabled_ || isReadOnly() || overwriteMode()) {
        return;
    }
    openCompletionPopup(
        miacode::editor::holdDurationCandidates(),
        QLatin1Char('['),
        /*closingPresent=*/false);
}

bool PlainCodeEditor::bracketCompletionActive() const
{
    return completionPopup_ != nullptr && completionPopup_->isVisible()
        && !completionOpening_.isNull();
}

// Intercepts the navigation / accept / dismiss keys while the popup is open.
// Printable characters and Backspace deliberately return false so they reach the
// normal insert path and the cursorPositionChanged slot re-filters the list.
bool PlainCodeEditor::handleCompletionPopupKey(QKeyEvent* event)
{
    switch (event->key()) {
    case Qt::Key_Escape:
        closeBracketCompletion();
        event->accept();
        return true;
    case Qt::Key_Up:
        completionPopup_->moveSelection(-1);
        event->accept();
        return true;
    case Qt::Key_Down:
        completionPopup_->moveSelection(1);
        event->accept();
        return true;
    case Qt::Key_Tab:
        acceptCompletionCandidate();
        event->accept();
        return true;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        // Plain Enter commits the highlighted candidate (and swallows the line
        // break). Ctrl/Alt/Meta + Enter fall through to the editor's own
        // line-break handling.
        if (!(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
            acceptCompletionCandidate();
            event->accept();
            return true;
        }
        return false;
    default:
        return false;
    }
}

void PlainCodeEditor::acceptCompletionCandidate()
{
    if (!bracketCompletionActive()) {
        return;
    }
    const QString candidate = completionPopup_->currentCandidate();
    if (candidate.isEmpty()) {
        closeBracketCompletion();
        return;
    }
    const QChar closing = miacode::editor::closingBracketFor(completionOpening_);

    suppressCompletionFilter_ = true;
    QTextCursor cursor = textCursor();
    int selectionEnd = cursor.position();
    // The candidate carries its own closing glyph, so swallow the auto-inserted
    // one immediately to the right (if present) instead of duplicating it.
    if (completionClosingPresent_ && !closing.isNull()) {
        QTextCursor probe(document());
        probe.setPosition(selectionEnd);
        probe.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        if (probe.selectedText() == QString(closing)) {
            selectionEnd += 1;
        }
    }
    cursor.beginEditBlock();
    cursor.setPosition(completionStartPos_);
    cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    cursor.insertText(candidate);
    cursor.endEditBlock();
    setTextCursor(cursor);
    suppressCompletionFilter_ = false;
    closeBracketCompletion();
}

void PlainCodeEditor::updateBracketCompletionFilter()
{
    const int position = textCursor().position();
    if (position < completionStartPos_) {
        closeBracketCompletion();
        return;
    }
    QTextCursor span(document());
    span.setPosition(completionStartPos_);
    span.setPosition(position, QTextCursor::KeepAnchor);
    const QString typed = span.selectedText();
    // A newline or the closing glyph means the caret left the completion slot.
    const QChar closing = miacode::editor::closingBracketFor(completionOpening_);
    if (typed.contains(QChar::ParagraphSeparator) || typed.contains(QChar::LineSeparator)
        || (!closing.isNull() && typed.contains(closing))) {
        closeBracketCompletion();
        return;
    }
    if (!completionPopup_->applyFilter(typed)) {
        closeBracketCompletion();
        return;
    }
    const QRect caret = cursorRect();
    const QPoint anchor = viewport()->mapToGlobal(caret.bottomLeft() + QPoint(0, 2));
    completionPopup_->moveToAnchor(anchor);
}

void PlainCodeEditor::closeBracketCompletion()
{
    if (completionPopup_ != nullptr) {
        completionPopup_->hide();
    }
    completionOpening_ = QChar();
    completionClosingPresent_ = false;
    completionStartPos_ = -1;
}

void PlainCodeEditor::inputMethodEvent(QInputMethodEvent* event)
{
    if (event == nullptr || !halfWidthInputEnabled_) {
        QTextEdit::inputMethodEvent(event);
        return;
    }

    const QString commitString = event->commitString();
    const QString normalizedCommitString = miacode::editor::normalizedHalfWidthText(commitString);

    // Route a finalized single-bracket commit through the same auto-close
    // pairing as keyPressEvent. Without this, a bracket delivered via the IME
    // commit string — e.g. a full-width 「【」 normalized to a half-width "["
    // just above — is inserted unclosed, leaving a dangling scope that the
    // bracket-scope highlighter then carries onto the following line. Guard to a
    // finalized composition (empty preedit) with no replacement span so the
    // helper's caret math stays valid.
    if (event->preeditString().isEmpty()
        && event->replacementLength() == 0
        && normalizedCommitString.size() == 1
        && miacode::editor::isBracketOpening(normalizedCommitString.at(0))) {
        const QChar opening = normalizedCommitString.at(0);
        const bool hadSelection = textCursor().hasSelection();
        if (tryAutoCloseBracket(normalizedCommitString)) {
            if (!hadSelection) {
                maybeOpenBracketCompletion(opening, /*closingPresent=*/true);
            }
            event->accept();
            return;
        }
    }

    // A finalized closing-bracket commit (e.g. a full-width 「）」 normalized to a
    // half-width ")") gets the same type-over treatment as the keyPress path.
    if (event->preeditString().isEmpty()
        && event->replacementLength() == 0
        && normalizedCommitString.size() == 1
        && miacode::editor::isBracketClosing(normalizedCommitString.at(0))) {
        if (tryOverwriteClosingBracket(normalizedCommitString)) {
            event->accept();
            return;
        }
    }

    if (commitString == normalizedCommitString) {
        QTextEdit::inputMethodEvent(event);
        return;
    }

    QInputMethodEvent normalizedEvent(event->preeditString(), event->attributes());
    normalizedEvent.setCommitString(
        normalizedCommitString,
        event->replacementStart(),
        event->replacementLength()
    );
    QTextEdit::inputMethodEvent(&normalizedEvent);
}

void PlainCodeEditor::insertFromMimeData(const QMimeData* source)
{
    if (source == nullptr) {
        return;
    }

    const QString text = source->text();
    if (text.isEmpty()) {
        QTextEdit::insertFromMimeData(source);
        return;
    }

    QTextCursor cursor = textCursor();
    const int selectionStart = cursor.selectionStart();
    cursor.beginEditBlock();
    cursor.insertText(text);

    QTextCursor blockCursor(document());
    blockCursor.setPosition(selectionStart);
    blockCursor.setPosition(cursor.position(), QTextCursor::KeepAnchor);
    QTextBlockFormat fmt;
    fmt.setBottomMargin(static_cast<qreal>(blockSpacingPixels_));
    blockCursor.mergeBlockFormat(fmt);
    cursor.endEditBlock();
    setTextCursor(cursor);
}

void PlainCodeEditor::keyPressEvent(QKeyEvent* event)
{
    if (event == nullptr) {
        QTextEdit::keyPressEvent(event);
        return;
    }

    // While the completion popup is open it owns the navigation / accept /
    // dismiss keys (↑ ↓ Tab Enter Esc). Printable characters and Backspace
    // fall through to normal editing; the cursorPositionChanged slot then
    // re-filters (or dismisses) the list. This must run before the line-break
    // handling below so a popup-Enter commits instead of inserting a newline.
    if (bracketCompletionActive() && handleCompletionPopupKey(event)) {
        return;
    }

    // Backspace between an empty matching pair removes both glyphs at once.
    if (tryDeleteBracketPair(event)) {
        return;
    }

    // The default plain Insert binding deliberately avoids Shift+Insert
    // paste and Ctrl+Insert copy. Custom bindings are matched through
    // the shortcut registry. The Preferences UI no longer exposes a
    // checkbox for this (beta59) — Insert key + persistence stay so the
    // user can still toggle overwrite mode on demand.
    const QKeySequence overwriteModeSequence =
        ShortcutRegistry::instance().sequence(
            QStringLiteral("editor.overwrite_mode"),
            QKeySequence(Qt::Key_Insert));
    const QKeySequence pressedSequence = keySequenceForEvent(event);
    if (matchesShortcutId(
            event,
            QStringLiteral("transform.clear_complete_elements"),
            {QKeySequence(Qt::CTRL | Qt::Key_Q)})) {
        emit clearCompleteElementsShortcutRequested();
        event->accept();
        return;
    }
    if (matchesShortcutId(
            event,
            QStringLiteral("transform.subdivision_half_up"),
            {QKeySequence(QStringLiteral("Ctrl+Shift+=")), QKeySequence(QStringLiteral("Ctrl++"))})) {
        emit raiseSubdivisionHalfStepShortcutRequested();
        event->accept();
        return;
    }
    if (matchesShortcutId(
            event,
            QStringLiteral("transform.subdivision_half_down"),
            {QKeySequence(QStringLiteral("Ctrl+Shift+-")), QKeySequence(QStringLiteral("Ctrl+_"))})) {
        emit lowerSubdivisionHalfStepShortcutRequested();
        event->accept();
        return;
    }
    const bool overwriteModeKey =
        !overwriteModeSequence.isEmpty()
        && pressedSequence == overwriteModeSequence;
    if (overwriteModeKey) {
        if (!event->isAutoRepeat()) {
            setEditorOverwriteMode(!overwriteMode());
        }
        event->accept();
        return;
    }

    // Undo/Redo are handled on auto-repeat too, so holding Ctrl+Z / Ctrl+Y keeps
    // stepping through the history like every other text editor. (Earlier this was
    // gated behind !isAutoRepeat(), which is correct for the one-shot overwrite-mode
    // toggle above but wrongly suppressed repeated undo/redo.) The emit routes through
    // MainWindow's selection-restore-aware undo/redo actions.
    if (event->matches(QKeySequence::Undo)) {
        logSelectionRestoreEditorShortcut(
            QStringLiteral("editor_shortcut_press"),
            QStringLiteral("match=undo key=%1 modifiers=%2 repeat=%3")
                .arg(event->key())
                .arg(static_cast<int>(event->modifiers()))
                .arg(event->isAutoRepeat() ? 1 : 0)
        );
        emit undoShortcutRequested();
        event->accept();
        return;
    }
    if (event->matches(QKeySequence::Redo)) {
        logSelectionRestoreEditorShortcut(
            QStringLiteral("editor_shortcut_press"),
            QStringLiteral("match=redo key=%1 modifiers=%2 repeat=%3")
                .arg(event->key())
                .arg(static_cast<int>(event->modifiers()))
                .arg(event->isAutoRepeat() ? 1 : 0)
        );
        emit redoShortcutRequested();
        event->accept();
        return;
    }

    const bool plainEnterKey =
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    const bool ctrlEnterKey =
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && (event->modifiers() & Qt::ControlModifier)
        && !(event->modifiers() & (Qt::AltModifier | Qt::MetaModifier));
    if (plainEnterKey || ctrlEnterKey) {
        insertLineBreakAtCursor(this);
        return;
    }

    // Bracket auto-pairing is handled by tryAutoCloseBracket() / tryBracketInput()
    // (members, so the IME commit path in inputMethodEvent can reuse them); the
    // 'h' hold shortcut by tryHoldExpand(). The call sites below pass the half-
    // width-normalized text so a full-width 「（」/「【」/「｛」 pairs the same way a
    // raw "(" / "[" / "{" does.
    if (!halfWidthInputEnabled_
        || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        if (tryOverwriteClosingBracket(event->text())
            || tryBracketInput(event->text()) || tryHoldExpand(event->text())) {
            event->accept();
            return;
        }
        QTextEdit::keyPressEvent(event);
        return;
    }

    const QString inputText = event->text();
    if (inputText.isEmpty()) {
        QTextEdit::keyPressEvent(event);
        return;
    }

    const QString normalizedText = miacode::editor::normalizedHalfWidthKeyText(event, inputText);
    if (tryOverwriteClosingBracket(normalizedText)
        || tryBracketInput(normalizedText) || tryHoldExpand(normalizedText)) {
        event->accept();
        return;
    }
    if (normalizedText == inputText) {
        QTextEdit::keyPressEvent(event);
        return;
    }

    QKeyEvent normalizedEvent(
        event->type(),
        event->key(),
        event->modifiers(),
        normalizedText,
        event->isAutoRepeat(),
        event->count()
    );
    QTextEdit::keyPressEvent(&normalizedEvent);
}

void PlainCodeEditor::mousePressEvent(QMouseEvent* event)
{
    if (event == nullptr) {
        QTextEdit::mousePressEvent(event);
        return;
    }

    const QPointF adjustedPosition = normalizedViewportHitPosition(event->position());
    if (adjustedPosition == event->position()) {
        QTextEdit::mousePressEvent(event);
        return;
    }

    const QPointF delta = adjustedPosition - event->position();
    QMouseEvent adjustedEvent(
        event->type(),
        adjustedPosition,
        event->scenePosition() + delta,
        event->globalPosition() + delta,
        event->button(),
        event->buttons(),
        event->modifiers(),
        event->pointingDevice()
    );
    adjustedEvent.setAccepted(false);
    QTextEdit::mousePressEvent(&adjustedEvent);
    event->setAccepted(adjustedEvent.isAccepted());
}

void PlainCodeEditor::paintEvent(QPaintEvent* event)
{
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

void PlainCodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(lineNumberArea_);
    const UiTheme::Colors& c = UiTheme::colors();
    painter.fillRect(event->rect(), c.timelineSidebar);
    painter.setPen(c.textSecondary);
    painter.setFont(lineNumberArea_->font());

    QTextCursor startCursor = cursorForPosition(QPoint(0, 0));
    QTextBlock block = startCursor.block();
    if (!block.isValid() && document() != nullptr) {
        block = document()->firstBlock();
    }
    int blockNumber = block.isValid() ? block.blockNumber() : 0;
    const int yOffset = qMax(0, topOverlayInsetPixels_);
    const int visibleTop = event->rect().top() - yOffset;
    const int visibleBottom = event->rect().bottom() - yOffset;
    const int lineHeight = QFontMetrics(lineNumberArea_->font()).height();

    while (block.isValid()) {
        QTextCursor blockCursor(block);
        const QRect blockRect = cursorRect(blockCursor);
        if (blockRect.top() > visibleBottom) {
            break;
        }
        if (blockRect.bottom() >= visibleTop) {
            const QString number = QString::number(blockNumber + 1);
            const int drawTop = blockRect.top() + yOffset;
            const int line = blockNumber + 1;
            const bool isBookmarkLine = bookmarkedLines_.contains(line);
            const bool isDropLine = hoveredBookmarkDropLine_ == line;
            if (isBookmarkLine || isDropLine) {
                QColor markerColor = c.accent;
                markerColor.setAlpha(isDropLine ? 80 : 34);
                const QRect rowRect(0, drawTop, lineNumberArea_->width(), lineHeight);
                painter.fillRect(rowRect, markerColor);
                painter.setPen(QPen(c.accent, isDropLine ? 2 : 1));
                const int underlineY = qMin(rowRect.bottom() - 2, drawTop + lineHeight - 3);
                painter.drawLine(
                    kLineNumberLeftPadding,
                    underlineY,
                    lineNumberArea_->width() - kLineNumberRightPadding,
                    underlineY
                );
            }
            painter.setPen(isBookmarkLine || isDropLine ? c.accent : c.textSecondary);
            painter.drawText(
                kLineNumberLeftPadding,
                drawTop,
                lineNumberArea_->width() - kLineNumberLeftPadding - kLineNumberRightPadding,
                lineHeight,
                Qt::AlignRight,
                number
            );
        }

        block = block.next();
        ++blockNumber;
    }
}
