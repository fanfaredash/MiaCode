#include "PlainCodeEditor.h"

#include <QContextMenuEvent>
#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>

namespace {
constexpr int kLineNumberLeftPadding = 6;
constexpr int kLineNumberRightPadding = 10;
constexpr int kLineNumberMinWidth = 40;
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
    updateLineNumberAreaWidth(0);
    setLineWrapMode(QTextEdit::NoWrap);
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

void PlainCodeEditor::refreshLineNumberAreaLayout()
{
    lineNumberArea_->setFont(font());
    updateLineNumberAreaWidth(0);
    const QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    lineNumberArea_->update();
    viewport()->update();
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
    setViewportMargins(lineNumberAreaWidth(), 0, 0, 0);
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
    QMenu* menu = createStandardContextMenu();
    if (menu == nullptr) {
        return;
    }
    menu->setWindowFlag(Qt::FramelessWindowHint, true);
    menu->setWindowFlag(Qt::NoDropShadowWindowHint, true);
    menu->setAttribute(Qt::WA_TranslucentBackground, true);
    menu->setStyleSheet(
        "QMenu {"
        " background: rgba(255, 255, 255, 245);"
        " border: 1px solid #D7E0EB;"
        " border-radius: 8px;"
        " padding: 7px;"
        "}"
        "QMenu::item {"
        " padding: 6px 20px 6px 12px;"
        " margin: 1px 0;"
        " border-radius: 6px;"
        " color: #203040;"
        "}"
        "QMenu::item:selected {"
        " background: #EEF5FF;"
        " color: #203040;"
        "}"
        "QMenu::item:disabled {"
        " color: #9AA5B4;"
        " background: transparent;"
        "}"
    );
    menu->exec(event->globalPos());
    delete menu;
}

void PlainCodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(lineNumberArea_);
    painter.fillRect(event->rect(), QColor("#E6E6E6"));
    painter.setPen(QColor("#666666"));
    painter.setFont(lineNumberArea_->font());

    QTextCursor startCursor = cursorForPosition(QPoint(0, 0));
    QTextBlock block = startCursor.block();
    if (!block.isValid() && document() != nullptr) {
        block = document()->firstBlock();
    }
    int blockNumber = block.isValid() ? block.blockNumber() : 0;
    const int visibleTop = event->rect().top();
    const int visibleBottom = event->rect().bottom();
    const int lineHeight = QFontMetrics(lineNumberArea_->font()).height();

    while (block.isValid()) {
        QTextCursor blockCursor(block);
        const QRect blockRect = cursorRect(blockCursor);
        if (blockRect.top() > visibleBottom) {
            break;
        }
        if (blockRect.bottom() >= visibleTop) {
            const QString number = QString::number(blockNumber + 1);
            painter.drawText(
                kLineNumberLeftPadding,
                blockRect.top(),
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
