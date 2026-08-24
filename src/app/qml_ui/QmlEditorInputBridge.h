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
    void imeComposingChanged(bool composing);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QPointer<QObject> target_;
    bool imeComposing_ = false;
    // The desktop acceptance run reported a committed IME character being
    // inserted many times over, which no synthetic QInputMethodEvent sequence
    // reproduces. These record the exact platform sequence — how many commits
    // arrive, what each carries, and whether one arrives while the previous
    // QML transaction is still running — so the next reproduction identifies
    // the source instead of guessing at it.
    quint64 imeEventSequence_ = 0;
    int commitDepth_ = 0;
};

} // namespace miacode::qml_ui
