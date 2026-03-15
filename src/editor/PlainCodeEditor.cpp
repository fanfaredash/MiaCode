#include "PlainCodeEditor.h"
#include "UiText.h"
#include "UiTheme.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QMenu>
#include <QMimeData>
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
constexpr int kEditorCursorVisibleWidth = 1;
constexpr int kEditorCursorHiddenWidth = 0;
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
    const auto applyCursorVisibility = [this]() {
        const QTextCursor cursor = textCursor();
        const bool hideAtLineStart = !cursor.hasSelection() && cursor.positionInBlock() == 0;
        setCursorWidth(hideAtLineStart ? kEditorCursorHiddenWidth : kEditorCursorVisibleWidth);
    };

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
    connect(this, &QTextEdit::cursorPositionChanged, this, [this, applyCursorVisibility]() {
        applyCursorVisibility();
        viewport()->update();
    });
    updateLineNumberAreaWidth(0);
    setLineWrapMode(QTextEdit::NoWrap);
    setAcceptRichText(false);
    if (QTextFrame* frame = document()->rootFrame(); frame != nullptr) {
        QTextFrameFormat format = frame->frameFormat();
        format.setLeftMargin(kEditorDocumentLeftInset);
        frame->setFrameFormat(format);
    }
    setCursorWidth(kEditorCursorVisibleWidth);
    applyCursorVisibility();
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

void PlainCodeEditor::setBatchTransformActions(const QList<QAction*>& actions)
{
    batchTransformActions_ = actions;
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

    if (!batchTransformActions_.isEmpty()) {
        menu->addSeparator();
        auto* transformMenu = menu->addMenu(QStringLiteral("批量操作"));
        UiTheme::styleRoundedMenu(*transformMenu);
        transformMenu->setStyleSheet(
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
        for (QAction* action : batchTransformActions_) {
            if (action == nullptr) {
                continue;
            }
            transformMenu->addAction(action);
        }
    }

    menu->exec(event->globalPos());
    delete menu;
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

void PlainCodeEditor::paintEvent(QPaintEvent* event)
{
    QTextEdit::paintEvent(event);

    QTextCursor cursor = textCursor();
    const QTextBlock block = cursor.block();
    if (!block.isValid()) {
        return;
    }

    QTextCursor blockStartCursor(block);
    const QRect blockRect = cursorRect(blockStartCursor);
    const int blockTop = blockRect.top();
    // Keep current-line highlight tied to glyph line height, not paragraph bottom margin.
    const int blockHeight = qMax(1, blockRect.height());

    const int verticalExpand = qMax(0, qRound(static_cast<qreal>(blockSpacingPixels_) / 8.0));
    QRect highlightRect(
        kCurrentLineHighlightLeftInset,
        blockTop - verticalExpand,
        viewport()->width() - kCurrentLineHighlightLeftInset - kCurrentLineHighlightRightInset,
        blockHeight + (verticalExpand * 2)
    );
    highlightRect = highlightRect.intersected(viewport()->rect());
    highlightRect.adjust(0, 0, -1, -1);
    if (!highlightRect.isValid()) {
        return;
    }

    QPainter painter(viewport());
    const UiTheme::Colors& c = UiTheme::colors();
    QColor currentLineFill = c.menuHoverBg;
    currentLineFill.setAlpha(c.dark ? 60 : 36);
    painter.setPen(QPen(c.borderSoft, 1));
    painter.setBrush(currentLineFill);
    painter.drawRoundedRect(highlightRect, 4.0, 4.0);
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
