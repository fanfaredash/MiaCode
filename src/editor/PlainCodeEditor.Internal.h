#pragma once

// Internal helpers shared by the PlainCodeEditor translation units. The
// PlainCodeEditor.cpp god-file was split into several .cpp units; this header
// holds the file-local constants + the LineNumberArea inner widget that more
// than one of those units needs. Anonymous-namespace symbols have internal
// linkage, so a constant referenced from two TUs would otherwise be a link
// error — co-locating it here in a named namespace with `inline constexpr`
// keeps every TU pointing at the same definition. LineNumberArea (a plain,
// non-Q_OBJECT QWidget) is defined here so the Layout TU (which creates it) and
// the Bookmarks TU (which drives its drag/drop + repaint) share the complete
// type. Bodies are byte-identical to the former in-.cpp definitions.

#include "PlainCodeEditor.h"

#include <QApplication>
#include <QByteArray>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QSize>
#include <QWidget>

namespace miacode::editor::pce_detail {

// Caret width for insert mode (1 px). Used by the Layout TU (ctor pins the
// initial cursor width) and the HighlightAndCaret TU (overwrite-mode toggle and
// the preview-follow caret painter).
inline constexpr int kEditorCursorVisibleWidth = 1;

}  // namespace miacode::editor::pce_detail

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
            if (editor_->bookmarkedLines_.contains(line)) {
                emit editor_->lineNumberBookmarkActivated(line);
            } else {
                emit editor_->lineNumberBookmarkCreateRequested(line);
            }
            event->accept();
            return;
        }
        QWidget::mouseDoubleClickEvent(event);
    }

    // Gutter right-click: bookmark actions for the clicked row. The menu
    // itself is assembled by MainWindow (the editor layer stays UI-policy-free).
    void contextMenuEvent(QContextMenuEvent* event) override
    {
        const int line = editor_->lineNumberAtAreaPosition(event->pos());
        if (line > 0) {
            emit editor_->lineNumberBookmarkContextMenuRequested(line, event->globalPos());
            event->accept();
            return;
        }
        QWidget::contextMenuEvent(event);
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
