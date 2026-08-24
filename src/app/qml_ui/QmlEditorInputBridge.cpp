#include "QmlEditorInputBridge.h"

#include "common/DebugLog.h"

#include <QInputMethodEvent>
#include <QStringList>

namespace miacode::qml_ui {
namespace {

QString describeAttributes(const QInputMethodEvent& event)
{
    QStringList parts;
    for (const QInputMethodEvent::Attribute& attribute : event.attributes()) {
        switch (attribute.type) {
        case QInputMethodEvent::Selection:
            parts.append(QStringLiteral("selection@%1+%2").arg(attribute.start).arg(attribute.length));
            break;
        case QInputMethodEvent::Cursor:
            parts.append(QStringLiteral("cursor@%1+%2").arg(attribute.start).arg(attribute.length));
            break;
        case QInputMethodEvent::TextFormat:
            parts.append(QStringLiteral("format@%1+%2").arg(attribute.start).arg(attribute.length));
            break;
        case QInputMethodEvent::Language:
        case QInputMethodEvent::Ruby:
            parts.append(QStringLiteral("other@%1+%2").arg(attribute.start).arg(attribute.length));
            break;
        }
    }
    return parts.isEmpty() ? QStringLiteral("none") : parts.join(QLatin1Char(','));
}

} // namespace

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
    const quint64 sequence = ++imeEventSequence_;
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("editor/ime_event"),
        QStringLiteral("seq=%1 commit_len=%2 preedit_len=%3 replace_start=%4 replace_len=%5 "
                       "attributes=%6 composing=%7 depth=%8")
            .arg(sequence)
            .arg(input->commitString().size())
            .arg(input->preeditString().size())
            .arg(input->replacementStart())
            .arg(input->replacementLength())
            .arg(describeAttributes(*input))
            .arg(imeComposing_ ? 1 : 0)
            .arg(commitDepth_));
    const bool composing = !input->preeditString().isEmpty();
    if (imeComposing_ != composing) {
        imeComposing_ = composing;
        emit imeComposingChanged(imeComposing_);
    }
    if (input->commitString().isEmpty()) return false;
    // A commit arriving while the previous one is still being applied would
    // mean the QML transaction re-enters the platform input context; the depth
    // is recorded on both sides of the emit so a reproduction shows it.
    ++commitDepth_;
    emit imeCommitted(input->commitString());
    --commitDepth_;
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("editor/ime_commit"),
        QStringLiteral("seq=%1 applied_len=%2 reentrant=%3")
            .arg(sequence)
            .arg(input->commitString().size())
            .arg(commitDepth_ > 0 ? 1 : 0));
    // Preserve the new composition state carried alongside this commit. The
    // target receives the original event with only its committed replacement
    // stripped, so it can render preedit text and formatting attributes while
    // the QML transaction remains the sole document mutation.
    input->setCommitString(QString());
    return false;
}

} // namespace miacode::qml_ui
