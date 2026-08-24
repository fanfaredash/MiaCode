#include "PlainCodeEditor.h"
#include "BracketCompletionPopup.h"
#include "SimaiCompletionCatalog.h"
#include "common/AdoptedSurfaceDragAutoScroll.h"
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
#include <QScopedValueRollback>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFrame>

#include "PlainCodeEditor.Internal.h"

using namespace miacode::editor::pce_detail;

namespace {
constexpr int kLineNumberLeftPadding = 6;

constexpr int kLineNumberRightPadding = 10;

constexpr int kLineNumberMinWidth = 40;

constexpr qreal kEditorDocumentLeftInset = 14.0;

class TopOverlayArea final : public QWidget {
public:
    explicit TopOverlayArea(QWidget* parent)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(this);
        const QRect dirtyRect = event != nullptr ? event->rect() : rect();
        painter.setClipRect(dirtyRect);
        if (miacode::ui::paintAppBackgroundForWidget(this, painter)) {
            const UiTheme::Colors& c = UiTheme::colors();
            QColor editorSurface = c.inputBg;
            editorSurface.setAlpha(
                UiTheme::appBackgroundOverlayAlpha(UiTheme::AppBackgroundOverlayRole::CodeEditor, c.dark));
            painter.fillRect(dirtyRect, editorSurface);
        } else {
            painter.fillRect(dirtyRect, UiTheme::colors().inputBg);
        }
    }
};
}  // namespace

PlainCodeEditor::PlainCodeEditor(QWidget* parent)
    : QTextEdit(parent)
    , lineNumberArea_(new LineNumberArea(this))
    , topOverlayArea_(new TopOverlayArea(this))
{
    topOverlayArea_->hide();
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
    if (QScrollBar* vbar = verticalScrollBar(); vbar != nullptr) {
        vbar->setContextMenuPolicy(Qt::NoContextMenu);
        verticalScrollBaseMaximum_ = vbar->maximum();
        connect(vbar, &QScrollBar::rangeChanged, this, [this](int minimum, int maximum) {
            Q_UNUSED(minimum);
            if (updatingScrollBeyondLastLineRange_) {
                return;
            }
            verticalScrollBaseMaximum_ = maximum;
            updateScrollBeyondLastLineRange();
        });
    }
    if (QScrollBar* hbar = horizontalScrollBar(); hbar != nullptr) {
        hbar->setContextMenuPolicy(Qt::NoContextMenu);
    }
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
    updateScrollBeyondLastLineRange();
    setCursorWidth(kEditorCursorVisibleWidth);
    updateCursorVisibility();
    lastCurrentLineHighlightRect_ = currentLineHighlightRect();
    // The line-number gutter is a viewport margin, so a leftward drag-select
    // leaves the viewport and would hand the gesture to Qt's QCursor-driven
    // autoscroll — which mis-targets on the adopted macOS surface. No-op
    // elsewhere; see common/AdoptedSurfaceDragAutoScroll.h.
    miacode::ui::installAdoptedSurfaceDragAutoScroll(this);
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
    updateScrollBeyondLastLineRange();
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
    refreshLineNumberAreaLayout();
}

void PlainCodeEditor::refreshLineNumberAreaLayout()
{
    lineNumberArea_->setFont(font());
    updateLineNumberAreaWidth(0);
    const QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    lineNumberArea_->update();
    viewport()->update();
    updateScrollBeyondLastLineRange();
}

void PlainCodeEditor::updateScrollBeyondLastLineRange()
{
    QScrollBar* vbar = verticalScrollBar();
    if (vbar == nullptr || viewport() == nullptr) {
        return;
    }
    const int lastLineHeight = qMax(1, fontMetrics().height() + blockSpacingPixels_);
    const int virtualSpace = scrollBeyondLastLineEnabled_
        ? qMax(0, viewport()->height() - lastLineHeight)
        : 0;
    const int desiredMaximum = verticalScrollBaseMaximum_ + virtualSpace;
    if (vbar->maximum() == desiredMaximum) {
        return;
    }
    const QScopedValueRollback<bool> guard(updatingScrollBeyondLastLineRange_, true);
    vbar->setMaximum(desiredMaximum);
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
    updateScrollBeyondLastLineRange();

    const QRect cr = contentsRect();
    lineNumberArea_->setGeometry(QRect(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height()));
    if (topOverlayArea_ != nullptr) {
        const QRect viewportGeometry = viewport()->geometry();
        topOverlayArea_->setGeometry(
            viewportGeometry.left(),
            cr.top(),
            viewportGeometry.width(),
            qMax(0, topOverlayInsetPixels_));
        topOverlayArea_->setVisible(topOverlayInsetPixels_ > 0);
        topOverlayArea_->update();
    }
}

void PlainCodeEditor::lineNumberAreaPaintEvent(QPaintEvent* event)
{
    QPainter painter(lineNumberArea_);
    const UiTheme::Colors& c = UiTheme::colors();
    const QRect dirtyRect = event != nullptr ? event->rect() : lineNumberArea_->rect();
    if (miacode::ui::paintAppBackgroundForWidget(lineNumberArea_, painter)) {
        QColor sidebarSurface = c.timelineSidebar;
        sidebarSurface.setAlpha(
            UiTheme::appBackgroundOverlayAlpha(UiTheme::AppBackgroundOverlayRole::CodeEditor, c.dark));
        painter.fillRect(dirtyRect, sidebarSurface);
    } else {
        painter.fillRect(dirtyRect, c.timelineSidebar);
    }
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
            if (isBookmarkLine) {
                QColor markerColor = c.accent;
                markerColor.setAlpha(34);
                const QRect rowRect(0, drawTop, lineNumberArea_->width(), lineHeight);
                painter.fillRect(rowRect, markerColor);
            }
            painter.setPen(isBookmarkLine ? c.accent : c.textSecondary);
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
