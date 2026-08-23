#include "QmlEditorInputBridge.h"

#include <QInputMethodEvent>

namespace miacode::qml_ui {

QmlEditorInputBridge::QmlEditorInputBridge(QObject* parent) : QObject(parent) {}

QObject* QmlEditorInputBridge::target() const { return target_; }

void QmlEditorInputBridge::setTarget(QObject* target)
{
    if (target_ == target) return;
    if (target_ != nullptr) target_->removeEventFilter(this);
    target_ = target;
    if (target_ != nullptr) target_->installEventFilter(this);
    emit targetChanged();
}

bool QmlEditorInputBridge::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != target_ || event->type() != QEvent::InputMethod) return QObject::eventFilter(watched, event);
    auto* input = static_cast<QInputMethodEvent*>(event);
    const bool composing = !input->preeditString().isEmpty();
    if (imeComposing_ != composing) {
        imeComposing_ = composing;
        emit imeComposingChanged(imeComposing_);
    }
    if (input->commitString().isEmpty()) return false;
    emit imeCommitted(input->commitString());
    // Preserve the new composition state carried alongside this commit. The
    // target receives the original event with only its committed replacement
    // stripped, so it can render preedit text and formatting attributes while
    // the QML transaction remains the sole document mutation.
    input->setCommitString(QString());
    return false;
}

} // namespace miacode::qml_ui
