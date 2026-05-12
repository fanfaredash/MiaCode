#pragma once

#include <QList>
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
    bool applyPreviewFollowCursor(const QTextCursor& cursor, bool centerView, bool suppressSignals = true);
    QPointF normalizedViewportHitPosition(const QPointF& position) const;
    void setHalfWidthInputEnabled(bool enabled);
    bool halfWidthInputEnabled() const { return halfWidthInputEnabled_; }
    void setEditorOverwriteMode(bool enabled);

signals:
    void undoShortcutRequested();
    void redoShortcutRequested();
    void editorOverwriteModeChanged(bool enabled);

protected:
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
    QRect overwriteCharacterHighlightRect() const;
    QRect previewFollowVisualCaretRect() const;
    void syncCursorVisualState();
    void updateCursorVisibility();
    void updateCurrentLineHighlightRegion(const QRect& previousRect, const QRect& currentRect);

    int blockSpacingPixels_ = 0;
    int topOverlayInsetPixels_ = 0;
    bool halfWidthInputEnabled_ = true;
    QList<QAction*> batchTransformActions_;
    QList<QAction*> moreBatchTransformActions_;
    QRect lastCurrentLineHighlightRect_;
    bool previewFollowVisualCaretActive_ = false;
    int previewFollowVisualCaretLine_ = 1;
    int previewFollowVisualCaretCol_ = 1;
    LineNumberArea* lineNumberArea_;
};
