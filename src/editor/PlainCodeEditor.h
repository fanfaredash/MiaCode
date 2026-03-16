#pragma once

#include <QList>
#include <QTextEdit>

class LineNumberArea;
class QAction;
class QContextMenuEvent;
class QMimeData;

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

protected:
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
    void insertFromMimeData(const QMimeData* source) override;
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private slots:
    void updateLineNumberAreaWidth(int newBlockCount);
    void updateLineNumberArea();

private:
    int blockSpacingPixels_ = 0;
    int topOverlayInsetPixels_ = 0;
    QList<QAction*> batchTransformActions_;
    QList<QAction*> moreBatchTransformActions_;
    LineNumberArea* lineNumberArea_;
};
