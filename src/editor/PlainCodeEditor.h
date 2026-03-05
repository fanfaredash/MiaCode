#pragma once

#include <QPlainTextEdit>

class LineNumberArea;
class QContextMenuEvent;

class PlainCodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit PlainCodeEditor(QWidget* parent = nullptr);
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    void setBlockSpacingPixels(int px);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);

private:
    int blockSpacingPixels_ = 0;
    LineNumberArea* lineNumberArea_;
};
