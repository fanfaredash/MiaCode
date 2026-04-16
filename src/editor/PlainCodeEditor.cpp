#include "PlainCodeEditor.h"
#include "common/DebugLog.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAction>
#include <QContextMenuEvent>
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
constexpr int kLineNumberLeftPadding = 6;
constexpr int kLineNumberRightPadding = 10;
constexpr int kLineNumberMinWidth = 40;
constexpr qreal kEditorDocumentLeftInset = 14.0;
constexpr int kCurrentLineHighlightLeftInset = 3;
constexpr int kCurrentLineHighlightRightInset = 0;
constexpr int kCurrentLineHighlightDirtyMargin = 2;
constexpr int kEditorCursorVisibleWidth = 1;
constexpr int kEditorCursorHiddenWidth = 0;

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
        return QLatin1Char(',');
    case 0x3002:
        return QLatin1Char('.');
    case 0x3010:
        return QLatin1Char('[');
    case 0x3011:
        return QLatin1Char(']');
    default:
        return ch;
    }
}

QString normalizedHalfWidthText(QString text)
{
    for (int i = 0; i < text.size(); ++i) {
        text[i] = normalizedHalfWidthChar(text.at(i));
    }
    return text;
}

void logSelectionRestoreEditorShortcut(const QString& scope, const QString& payload)
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("selection_restore/%1").arg(scope),
        payload,
        true
    );
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

QPointF adjustedTrailingBlankClickPosition(const PlainCodeEditor* editor, const QPointF& position)
{
    if (editor == nullptr || editor->document() == nullptr) {
        return position;
    }
    const QTextBlock lastBlock = editor->document()->lastBlock();
    if (!lastBlock.isValid()) {
        return position;
    }

    const BlockVisualSpan lastBlockSpan = visibleBlockSpan(editor, lastBlock);
    if (!lastBlockSpan.isValid() || position.y() <= lastBlockSpan.bottom()) {
        return position;
    }

    return QPointF(position.x(), static_cast<qreal>(lastBlockSpan.lastRowRect.center().y()));
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
    }

    QSize sizeHint() const override
    {
        return QSize(editor_->lineNumberAreaWidth(), 0);
    }

protected:
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
    connect(this, &QTextEdit::cursorPositionChanged, this, [this]() { syncCursorVisualState(); });
    updateLineNumberAreaWidth(0);
    setLineWrapMode(QTextEdit::NoWrap);
    setAcceptRichText(false);
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

void PlainCodeEditor::updateCursorVisibility()
{
    const QTextCursor cursor = textCursor();
    const bool hideAtLineStart = !cursor.hasSelection()
        && cursor.positionInBlock() == 0
        && !cursor.block().text().isEmpty();
    setCursorWidth(hideAtLineStart ? kEditorCursorHiddenWidth : kEditorCursorVisibleWidth);
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
    const QRect previousRect = previewFollowVisualCaretRect();
    previewFollowVisualCaretActive_ = active;
    previewFollowVisualCaretLine_ = qMax(1, line);
    previewFollowVisualCaretCol_ = qMax(1, col);
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

bool PlainCodeEditor::event(QEvent* event)
{
    if (event != nullptr && event->type() == QEvent::ShortcutOverride) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent != nullptr
            && (keyEvent->matches(QKeySequence::Undo) || keyEvent->matches(QKeySequence::Redo))) {
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
    cutAction->setShortcut(QKeySequence::Cut);
    cutAction->setShortcutVisibleInContextMenu(true);
    cutAction->setEnabled(textCursor().hasSelection());
    connect(cutAction, &QAction::triggered, this, &QTextEdit::cut);

    auto* copyAction = menu->addAction(translated(QStringLiteral("action.copy"), QStringLiteral("Copy")));
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setShortcutVisibleInContextMenu(true);
    copyAction->setEnabled(textCursor().hasSelection());
    connect(copyAction, &QAction::triggered, this, &QTextEdit::copy);

    auto* pasteAction = menu->addAction(translated(QStringLiteral("action.paste"), QStringLiteral("Paste")));
    pasteAction->setShortcut(QKeySequence::Paste);
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

void PlainCodeEditor::inputMethodEvent(QInputMethodEvent* event)
{
    if (event == nullptr || !halfWidthInputEnabled_) {
        QTextEdit::inputMethodEvent(event);
        return;
    }

    const QString commitString = event->commitString();
    const QString normalizedCommitString = normalizedHalfWidthText(commitString);
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

    const QString text = halfWidthInputEnabled_
        ? normalizedHalfWidthText(source->text())
        : source->text();
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

    if (!event->isAutoRepeat()) {
        if (event->matches(QKeySequence::Undo)) {
            logSelectionRestoreEditorShortcut(
                QStringLiteral("editor_shortcut_press"),
                QStringLiteral("match=undo key=%1 modifiers=%2").arg(event->key()).arg(static_cast<int>(event->modifiers()))
            );
            emit undoShortcutRequested();
            event->accept();
            return;
        }
        if (event->matches(QKeySequence::Redo)) {
            logSelectionRestoreEditorShortcut(
                QStringLiteral("editor_shortcut_press"),
                QStringLiteral("match=redo key=%1 modifiers=%2").arg(event->key()).arg(static_cast<int>(event->modifiers()))
            );
            emit redoShortcutRequested();
            event->accept();
            return;
        }
    }

    const bool plainEnterKey =
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
        && !(event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier));
    if (plainEnterKey) {
        QTextCursor cursor = textCursor();
        cursor.beginEditBlock();
        cursor.insertBlock(cursor.blockFormat(), cursor.charFormat());
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
    }

    if (!halfWidthInputEnabled_
        || (event->modifiers() & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))) {
        QTextEdit::keyPressEvent(event);
        return;
    }

    const QString inputText = event->text();
    if (inputText.isEmpty()) {
        QTextEdit::keyPressEvent(event);
        return;
    }

    const QString normalizedText = normalizedHalfWidthText(inputText);
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

    const QPointF adjustedPosition = adjustedTrailingBlankClickPosition(this, event->position());
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
        QColor currentLineFill = c.menuHoverBg;
        currentLineFill.setAlpha(c.dark ? 60 : 36);
        painter.setPen(QPen(c.borderSoft, 1));
        painter.setBrush(currentLineFill);
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
