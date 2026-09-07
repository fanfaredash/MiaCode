#pragma once

#include <QObject>
#include <QPointF>
#include <QPointer>
#include <QQuickTextDocument>
#include <QVariantMap>
#include <QtQmlIntegration/qqmlintegration.h>

namespace miacode::qml_ui {

// 处理 TextArea 的输入法事件，并提供鼠标选择使用的文字命中坐标和单词边界。
// 预编辑事件交给 Qt；提交文字由 QML 事务适配器处理一次。
class QmlEditorInputBridge : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QObject* target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(bool imeInputDisabled READ imeInputDisabled WRITE setImeInputDisabled NOTIFY imeInputDisabledChanged)
    Q_PROPERTY(QQuickTextDocument* textDocument READ textDocument WRITE setTextDocument NOTIFY textDocumentChanged)

public:
    explicit QmlEditorInputBridge(QObject* parent = nullptr);
    QObject* target() const;
    void setTarget(QObject* target);
    bool imeInputDisabled() const;
    void setImeInputDisabled(bool disabled);
    QQuickTextDocument* textDocument() const;
    void setTextDocument(QQuickTextDocument* document);
    Q_INVOKABLE QPointF textHitPoint(qreal x, qreal y) const;
    Q_INVOKABLE QVariantMap wordRange(int position) const;

signals:
    void targetChanged();
    void imeInputDisabledChanged();
    void textDocumentChanged();
    void imeCommitted(const QString& text);
    void imeComposingChanged(bool composing);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void applyInputMethodState();

    QPointer<QObject> target_;
    QPointer<QQuickTextDocument> textDocument_;
    bool imeInputDisabled_ = true;
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
