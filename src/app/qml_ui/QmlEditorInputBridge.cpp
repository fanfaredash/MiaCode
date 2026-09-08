#include "QmlEditorInputBridge.h"

#include "common/DebugLog.h"

#include <QAbstractTextDocumentLayout>
#include <QGuiApplication>
#include <QInputMethod>
#include <QInputMethodEvent>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QStringList>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextLayout>

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
    emit textDocumentChanged();
}

void QmlEditorInputBridge::applyInputMethodState()
{
    auto* item = qobject_cast<QQuickItem*>(target_.data());
    if (item != nullptr) {
        item->setFlag(QQuickItem::ItemAcceptsInputMethod, !imeInputDisabled_);
    }
    if (QInputMethod* inputMethod = QGuiApplication::inputMethod(); inputMethod != nullptr) {
        if (imeInputDisabled_ && item != nullptr && item->hasActiveFocus()) {
            inputMethod->reset();
        }
        inputMethod->update(Qt::ImEnabled);
    }
    if (imeInputDisabled_ && imeComposing_) {
        imeComposing_ = false;
        emit imeComposingChanged(false);
    }
}

QPointF QmlEditorInputBridge::textHitPoint(qreal x, qreal y) const
{
    const QPointF point(x, y);
    QTextDocument* document = textDocument_ ? textDocument_->textDocument() : nullptr;
    if (!target_ || !document) return point;
    const qreal topPadding = target_->property("topPadding").toReal();
    const qreal documentY = y - topPadding;
    // 按实际段落几何定位，行距空隙也有明确归属。
    int firstBlock = 0;
    int lastBlock = document->blockCount() - 1;
    while (firstBlock < lastBlock) {
        const int middle = (firstBlock + lastBlock + 1) / 2;
        const QTextBlock candidate = document->findBlockByNumber(middle);
        if (document->documentLayout()->blockBoundingRect(candidate).top() <= documentY)
            firstBlock = middle;
        else
            lastBlock = middle - 1;
    }
    const QTextBlock block = document->findBlockByNumber(firstBlock);
    const QTextLayout* layout = block.layout();
    if (!layout || layout->lineCount() == 0) return point;

    // 行距空隙仍归属于所在显示行；把纵坐标移到文字中部，横坐标保持原值。
    // 自动换行使用实际 QTextLine，不能用字体高度推算行号。
    const qreal localY = documentY - layout->position().y();
    int low = 0;
    int high = layout->lineCount() - 1;
    while (low < high) {
        const int middle = (low + high + 1) / 2;
        if (layout->lineAt(middle).y() <= localY)
            low = middle;
        else
            high = middle - 1;
    }
    const QTextLine line = layout->lineAt(low);
    return QPointF(x, topPadding + layout->position().y() + line.y() + line.height() / 2);
}

QVariantMap QmlEditorInputBridge::wordRange(int position) const
{
    QTextCursor cursor(textDocument_->textDocument());
    cursor.setPosition(position);
    cursor.select(QTextCursor::WordUnderCursor);
    return {{QStringLiteral("start"), cursor.selectionStart()},
            {QStringLiteral("end"), cursor.selectionEnd()}};
}

bool QmlEditorInputBridge::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != target_ || event->type() != QEvent::InputMethod)
        return QObject::eventFilter(watched, event);
    if (imeInputDisabled_) {
        event->accept();
        return true;
    }
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
