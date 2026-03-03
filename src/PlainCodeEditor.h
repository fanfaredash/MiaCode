#pragma once

#include <QPlainTextEdit>

class LineNumberArea;

class PlainCodeEditor : public QPlainTextEdit
{
    Q_OBJECT

public:
    explicit PlainCodeEditor(QWidget* parent = nullptr);
    int lineNumberAreaWidth() const;
    void lineNumberAreaPaintEvent(QPaintEvent* event);

protected:
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea(const QRect& rect, int dy);

private:
    LineNumberArea* lineNumberArea_;
};

