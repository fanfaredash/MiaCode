#pragma once

#include <QObject>
#include <QPointer>
#include <QtQmlIntegration/qqmlintegration.h>

namespace miacode::qml_ui {

// Owns the QInputMethodEvent boundary for the QML TextArea. Preedit events are
// deliberately forwarded to Qt; commit strings are consumed and handed to the
// QML transaction adapter exactly once.
class QmlEditorInputBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* target READ target WRITE setTarget NOTIFY targetChanged)

public:
    explicit QmlEditorInputBridge(QObject* parent = nullptr);
    QObject* target() const;
    void setTarget(QObject* target);

signals:
    void targetChanged();
    void imeCommitted(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QObject> target_;
};

} // namespace miacode::qml_ui
