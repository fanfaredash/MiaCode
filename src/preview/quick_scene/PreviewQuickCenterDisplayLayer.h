#pragma once

#include <QPointer>
#include <QQuickPaintedItem>

class QPainter;
class PreviewRuntime;
namespace miacode::preview::scene {
struct PreviewFrameState;
}

class PreviewQuickCenterDisplayLayer : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QObject* runtime READ runtimeObject WRITE setRuntimeObject NOTIFY runtimeChanged)

public:
    explicit PreviewQuickCenterDisplayLayer(QQuickItem* parent = nullptr);

    void setRuntime(PreviewRuntime* runtime);
    QObject* runtimeObject() const;
    void setRuntimeObject(QObject* runtimeObject);
    void paint(QPainter* painter) override;

signals:
    void runtimeChanged();

private:
    QPointer<PreviewRuntime> runtime_;
    QMetaObject::Connection runtimeUpdateConnection_;
};

