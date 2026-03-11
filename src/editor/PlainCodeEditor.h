#pragma once

#include <QTextEdit>

class LineNumberArea;
class QContextMenuEvent;

class PlainCodeEditor : public QTextEdit
{
    Q_OBJECT

public:
    explicit PlainCodeEditor(QWidget* parent = nullptr);
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);
    void setBlockSpacingPixels(int px);
    void refreshLineNumberAreaLayout();

protected:
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea();

private:
    int blockSpacingPixels_ = 0;
    LineNumberArea* lineNumberArea_;
};
