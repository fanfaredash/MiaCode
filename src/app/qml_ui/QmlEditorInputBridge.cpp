#include "QmlEditorInputBridge.h"

#include "common/DebugLog.h"

#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QStringList>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>

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
bool QmlEditorInputBridge::imeInputDisabled() const { return imeInputDisabled_; }
QQuickTextDocument* QmlEditorInputBridge::textDocument() const { return textDocument_; }
int QmlEditorInputBridge::blockSpacing() const { return blockSpacing_; }

void QmlEditorInputBridge::setTarget(QObject* target)
{
    if (target_ == target) return;
    if (target_ != nullptr) target_->removeEventFilter(this);
    target_ = target;
    if (target_ != nullptr) target_->installEventFilter(this);
    applyInputMethodState();
    emit targetChanged();
}

void QmlEditorInputBridge::setImeInputDisabled(bool disabled)
{
    if (imeInputDisabled_ == disabled) return;
    imeInputDisabled_ = disabled;
    applyInputMethodState();
    emit imeInputDisabledChanged();
}

void QmlEditorInputBridge::setTextDocument(QQuickTextDocument* document)
{
    if (textDocument_ == document) return;
    textDocument_ = document;
    applyBlockSpacing();
    emit textDocumentChanged();
}

void QmlEditorInputBridge::setBlockSpacing(int spacing)
{
    const int normalized = qMax(0, spacing);
    if (blockSpacing_ == normalized) return;
    blockSpacing_ = normalized;
    applyBlockSpacing();
    emit blockSpacingChanged();
}

void QmlEditorInputBridge::applyBlockSpacing()
{
    QTextDocument* document = textDocument_ != nullptr ? textDocument_->textDocument() : nullptr;
    if (document == nullptr || document->firstBlock().blockFormat().bottomMargin() == blockSpacing_) return;
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    cursor.select(QTextCursor::Document);
    QTextBlockFormat format;
    format.setBottomMargin(blockSpacing_);
    cursor.mergeBlockFormat(format);
    cursor.endEditBlock();
}

void QmlEditorInputBridge::applyInputMethodState()
{
    if (auto* item = qobject_cast<QQuickItem*>(target_.data()); item != nullptr) {
        item->setFlag(QQuickItem::ItemAcceptsInputMethod, !imeInputDisabled_);
    }
    if (QInputMethod* inputMethod = QGuiApplication::inputMethod(); inputMethod != nullptr) {
        inputMethod->update(Qt::ImEnabled);
    }
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
    // One platform commit is one document transaction. Applying a commit runs
    // the QML transaction, which mutates the document while the editor still
    // holds this event's preedit; QQuickTextControl then commits that preedit
    // through the platform input context, which re-delivers the SAME commit
    // into this filter. Applying it again recurses without bound — the desktop
    // capture reached depth 630 and inserted 631 copies of one character. A
    // re-entrant commit has already been applied by the frame below, so strip
    // it and drop it rather than replaying it.
    if (commitDepth_ > 0) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("editor/ime_commit"),
            QStringLiteral("seq=%1 applied_len=0 reentrant=1 action=dropped depth=%2")
                .arg(sequence)
                .arg(commitDepth_));
        input->setCommitString(QString());
        return false;
    }
    // The depth is recorded on both sides of the emit so a capture still shows
    // whether the platform re-entered, even though it can no longer duplicate.
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
