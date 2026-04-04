#pragma once

#include <QQuickPaintedItem>

class PreviewRuntime;

class PreviewQuickHudLayer : public QQuickPaintedItem
{
    Q_OBJECT

public:
    explicit PreviewQuickHudLayer(QQuickItem* parent = nullptr);

    void setRuntime(PreviewRuntime* runtime);
    void paint(QPainter* painter) override;

private:
    PreviewRuntime* runtime_ = nullptr;
};
