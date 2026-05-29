#pragma once

#include <QList>
#include <QSet>
#include <QPointF>
#include <QTextEdit>

class LineNumberArea;
class QAction;
class QContextMenuEvent;
class QFocusEvent;
class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
class QMimeData;

namespace miacode::editor {
QChar normalizedHalfWidthChar(QChar ch);
QString normalizedHalfWidthText(QString text);
QString normalizedHalfWidthKeyText(const QKeyEvent* event, const QString& text);
}

class PlainCodeEditor : public QTextEdit
{
    Q_OBJECT
    friend class LineNumberArea;

public:
    explicit PlainCodeEditor(QWidget* parent = nullptr);
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    void setBlockSpacingPixels(int px);
    void setTopOverlayInsetPixels(int px);
    void refreshLineNumberAreaLayout();
    void setBatchTransformActions(const QList<QAction*>& actions);
    void setMoreBatchTransformActions(const QList<QAction*>& actions);
    void setPreviewFollowVisualCaret(bool active, int line = 1, int col = 1);
    void setBookmarkedLines(const QSet<int>& lines);
    int lineNumberAtGlobalPosition(const QPoint& globalPos) const;
    void setBookmarkDropPreviewLine(int line);
    bool applyPreviewFollowCursor(const QTextCursor& cursor, bool centerView, bool suppressSignals = true);
    QPointF normalizedViewportHitPosition(const QPointF& position) const;
    void setHalfWidthInputEnabled(bool enabled);
    bool halfWidthInputEnabled() const { return halfWidthInputEnabled_; }
    // beta51+ — hard-disable the platform IME on this editor when the user
    // turns on the "禁止中文輸入法輸入" preference. Sets Qt::WA_InputMethodEnabled
    // = !disabled so the OS routes raw key events directly to the editor
    // (Chinese / Japanese / Korean / Vietnamese IMEs all get bypassed,
    // regardless of whether they honour Qt::ImhLatinOnly). halfWidth's
    // ImhLatinOnly hint is a *suggestion* most CJK IMEs ignore on Windows;
    // this attribute is the OS-level kill-switch.
    void setImeInputDisabled(bool disabled);
    bool imeInputDisabled() const { return imeInputDisabled_; }
    void setEditorOverwriteMode(bool enabled);
    // Auto-close brackets — when the user types {, [, or (, the matching
    // closing bracket is inserted right after and the caret stays
    // between the pair. Default on; controlled via the editor section
    // of the Preferences dialog.
    void setAutoCloseBracketsEnabled(bool enabled);
    bool autoCloseBracketsEnabled() const { return autoCloseBracketsEnabled_; }
    // simai hold shortcut — typing 'h' auto-inserts '[]' after it and
    // parks the caret between the square brackets. Independent toggle
    // from the generic bracket auto-close above.
    void setAutoInsertSquareAfterHEnabled(bool enabled);
    bool autoInsertSquareAfterHEnabled() const { return autoInsertSquareAfterHEnabled_; }

signals:
    void undoShortcutRequested();
    void redoShortcutRequested();
    void editorOverwriteModeChanged(bool enabled);
    void lineNumberBookmarkMoveRequested(int fromLine, int toLine);
    void lineNumberBookmarkActivated(int line);

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    bool event(QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void focusInEvent(QFocusEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;
    void inputMethodEvent(QInputMethodEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void keyPressEvent(QKeyEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea();

private:
    QRect currentLineHighlightRect() const;
    QRect previewFollowVisualCaretRect() const;
    int lineNumberAtAreaPosition(const QPoint& pos) const;
    void syncCursorVisualState();
    void updateCursorVisibility();
    void updateCurrentLineHighlightRegion(const QRect& previousRect, const QRect& currentRect);
    // Bracket auto-pairing helpers. Each returns true when it consumed the input
    // and performed the insertion. tryAutoCloseBracket is shared by both the
    // keyPressEvent and the IME commit (inputMethodEvent) paths; tryAutoExpandH
    // is keyPress-only (see its definition for why).
    bool tryAutoCloseBracket(const QString& text);
    bool tryAutoExpandH(const QString& text);

    int blockSpacingPixels_ = 0;
    int topOverlayInsetPixels_ = 0;
    bool halfWidthInputEnabled_ = true;
    bool imeInputDisabled_ = false;
    bool autoCloseBracketsEnabled_ = true;
    bool autoInsertSquareAfterHEnabled_ = true;
    QList<QAction*> batchTransformActions_;
    QList<QAction*> moreBatchTransformActions_;
    QRect lastCurrentLineHighlightRect_;
    bool previewFollowVisualCaretActive_ = false;
    int previewFollowVisualCaretLine_ = 1;
    int previewFollowVisualCaretCol_ = 1;
    QSet<int> bookmarkedLines_;
    int hoveredBookmarkDropLine_ = -1;
    int pressedBookmarkLine_ = -1;
    QPoint lineNumberPressPos_;
    LineNumberArea* lineNumberArea_;
};
