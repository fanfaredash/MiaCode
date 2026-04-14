#pragma once

#include <QList>
#include <QTextEdit>

class LineNumberArea;
class QAction;
class QContextMenuEvent;
class QInputMethodEvent;
class QKeyEvent;
class QMouseEvent;
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

signals:
    void previewPlayPauseRequested();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;
    void contextMenuEvent(QContextMenuEvent* event) override;
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
    bool previewShortcutPending_ = false;
    int blockSpacingPixels_ = 0;
    int topOverlayInsetPixels_ = 0;
    bool halfWidthInputEnabled_ = true;
    QList<QAction*> batchTransformActions_;
    QList<QAction*> moreBatchTransformActions_;
    LineNumberArea* lineNumberArea_;
};
