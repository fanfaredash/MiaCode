#include "QmlEditorController.h"

#include "editor/SimaiCompletionCatalog.h"

#include <QtGlobal>
#include <QGuiApplication>
#include <QClipboard>

namespace miacode::qml_ui {
namespace {

miacode::editor::SimaiTextEditResult untouched(const QString& text, int anchor, int position)
{
    miacode::editor::SimaiTextEditResult result;
    result.transaction.text = text;
    result.transaction.anchor = qBound(0, anchor, text.size());
    result.transaction.position = qBound(0, position, text.size());
    return result;
}

bool commandModifier(Qt::KeyboardModifiers modifiers)
{
    return modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
}

} // namespace

QmlEditorController::QmlEditorController(QObject* parent) : QObject(parent) {}
bool QmlEditorController::halfWidthInputEnabled() const { return halfWidthInputEnabled_; }
bool QmlEditorController::overwriteMode() const { return overwriteMode_; }
bool QmlEditorController::autoCompletionEnabled() const { return autoCompletionEnabled_; }
QString QmlEditorController::wholeBpm() const { return wholeBpm_; }
bool QmlEditorController::completionActive() const { return completion_.active; }
QStringList QmlEditorController::completionCandidates() const { return visibleCandidates_; }
int QmlEditorController::completionIndex() const { return completionIndex_; }
bool QmlEditorController::canUndo() const { return canUndo_; }
bool QmlEditorController::canRedo() const { return canRedo_; }

void QmlEditorController::setHalfWidthInputEnabled(bool enabled)
{
    if (halfWidthInputEnabled_ == enabled) return;
    halfWidthInputEnabled_ = enabled;
    emit settingsChanged();
}
void QmlEditorController::setOverwriteMode(bool enabled)
{
    if (overwriteMode_ == enabled) return;
    overwriteMode_ = enabled;
    emit settingsChanged();
}
void QmlEditorController::setAutoCompletionEnabled(bool enabled)
{
    if (autoCompletionEnabled_ == enabled) return;
    autoCompletionEnabled_ = enabled;
    if (!enabled) closeCompletion();
    emit settingsChanged();
}
void QmlEditorController::setWholeBpm(const QString& bpm)
{
    if (wholeBpm_ == bpm) return;
    wholeBpm_ = bpm;
    emit settingsChanged();
}

miacode::editor::SimaiTextEditResult QmlEditorController::processKey(
    const QString& text, int anchor, int position, const QString& input, int key, int modifiers)
{
    const auto keyboardModifiers = static_cast<Qt::KeyboardModifiers>(modifiers);
    if (completion_.active) {
        if (key == Qt::Key_Up) {
            moveCompletionSelection(-1);
            auto result = untouched(text, anchor, position);
            result.consumed = true;
            return result;
        }
        if (key == Qt::Key_Down) {
            moveCompletionSelection(1);
            auto result = untouched(text, anchor, position);
            result.consumed = true;
            return result;
        }
        if (key == Qt::Key_Escape) {
            closeCompletion();
            auto result = untouched(text, anchor, position);
            result.consumed = true;
            return result;
        }
        if ((key == Qt::Key_Return || key == Qt::Key_Enter) && !commandModifier(keyboardModifiers))
            return acceptCompletion(text, anchor, position);
    }
    miacode::editor::SimaiTextEditRequest request;
    request.text = text;
    request.anchor = anchor;
    request.position = position;
    request.input = input;
    request.key = key;
    request.modifiers = keyboardModifiers;
    return process(request);
}

miacode::editor::SimaiTextEditResult QmlEditorController::processImeCommit(
    const QString& text, int anchor, int position, const QString& committedText)
{
    if (committedText.isEmpty()) return untouched(text, anchor, position);
    miacode::editor::SimaiTextEditRequest request;
    request.text = text; request.anchor = anchor; request.position = position;
    request.input = committedText; request.isImeCommit = true;
    return process(request);
}

miacode::editor::SimaiTextEditResult QmlEditorController::processPaste(
    const QString& text, int anchor, int position, const QString& pastedText)
{
    miacode::editor::SimaiTextEditRequest request;
    request.text = text; request.anchor = anchor; request.position = position;
    request.input = pastedText; request.autoCompletionEnabled = false;
    return process(request);
}

miacode::editor::SimaiTextEditResult QmlEditorController::process(const miacode::editor::SimaiTextEditRequest& input)
{
    auto request = input;
    request.halfWidthInputEnabled = halfWidthInputEnabled_;
    request.overwriteMode = overwriteMode_;
    request.autoCompletionEnabled = autoCompletionEnabled_ && request.autoCompletionEnabled;
    request.wholeBpm = wholeBpm_;
    request.completionActive = completion_.active;
    auto result = miacode::editor::applySimaiTextEditPolicy(request);
    if (result.completion.active) setCompletion(result.completion);
    else if (completion_.active && result.transaction.hasEdit) filterCompletion(result.transaction.text, result.transaction.position);
    return result;
}

miacode::editor::SimaiTextEditResult QmlEditorController::acceptCompletion(const QString& text, int anchor, int position)
{
    auto result = untouched(text, anchor, position);
    if (!completion_.active || completionIndex_ < 0 || completionIndex_ >= visibleCandidates_.size()) return result;
    const int start = qBound(0, completion_.startPosition, text.size());
    int end = qBound(start, position, text.size());
    const QString candidate = visibleCandidates_.at(completionIndex_);
    // Catalog candidates include their closing glyph. When policy inserted a
    // matching pair, consume that existing right glyph in the same QML edit.
    const QChar closing = miacode::editor::closingBracketFor(completion_.opening);
    if (completion_.closingPresent && !closing.isNull() && end < text.size()
        && text.at(end) == closing) {
        ++end;
    }
    result.transaction.replacementStart = start;
    result.transaction.replacementEnd = end;
    result.transaction.replacementText = candidate;
    result.transaction.text.replace(start, end - start, candidate);
    result.transaction.anchor = result.transaction.position = start + candidate.size();
    result.transaction.hasEdit = result.transaction.undoGroup = true;
    result.consumed = true;
    closeCompletion();
    return result;
}

void QmlEditorController::setCompletion(const miacode::editor::SimaiCompletionSession& completion)
{
    completion_ = completion;
    visibleCandidates_ = completion.candidates;
    completionIndex_ = visibleCandidates_.isEmpty() ? -1 : 0;
    emit completionChanged();
}

void QmlEditorController::filterCompletion(const QString& text, int position)
{
    const int start = completion_.startPosition;
    if (start < 0 || position < start || position > text.size()) { closeCompletion(); return; }
    const QString prefix = text.mid(start, position - start);
    const QChar closing = miacode::editor::closingBracketFor(completion_.opening);
    if (prefix.contains(QLatin1Char('\n')) || (!closing.isNull() && prefix.contains(closing))) { closeCompletion(); return; }
    QStringList candidates;
    for (const QString& candidate : completion_.candidates) {
        if (candidate.startsWith(prefix, Qt::CaseInsensitive)) candidates.append(candidate);
    }
    visibleCandidates_ = candidates;
    completionIndex_ = candidates.isEmpty() ? -1 : qBound(0, completionIndex_, candidates.size() - 1);
    if (candidates.isEmpty()) completion_.active = false;
    emit completionChanged();
}

void QmlEditorController::moveCompletionSelection(int delta)
{
    if (!completion_.active || visibleCandidates_.isEmpty()) return;
    completionIndex_ = (completionIndex_ + delta) % visibleCandidates_.size();
    if (completionIndex_ < 0) completionIndex_ += visibleCandidates_.size();
    emit completionChanged();
}

void QmlEditorController::selectCompletionIndex(int index)
{
    if (!completion_.active || index < 0 || index >= visibleCandidates_.size()
        || completionIndex_ == index) {
        return;
    }
    completionIndex_ = index;
    emit completionChanged();
}

void QmlEditorController::closeCompletion()
{
    if (!completion_.active && visibleCandidates_.isEmpty() && completionIndex_ == -1) return;
    completion_ = {};
    visibleCandidates_.clear();
    completionIndex_ = -1;
    emit completionChanged();
}

void QmlEditorController::setUndoAvailability(bool canUndo, bool canRedo)
{
    if (canUndo_ == canUndo && canRedo_ == canRedo) return;
    canUndo_ = canUndo;
    canRedo_ = canRedo;
    emit undoAvailabilityChanged();
}

QString QmlEditorController::clipboardText() const
{
    const QClipboard* clipboard = QGuiApplication::clipboard();
    return clipboard != nullptr ? clipboard->text() : QString();
}

QVariantMap QmlEditorController::toQmlTransaction(const miacode::editor::SimaiTextEditResult& result) const
{
    const auto& tx = result.transaction;
    return {{QStringLiteral("consumed"), result.consumed}, {QStringLiteral("hasEdit"), tx.hasEdit},
            {QStringLiteral("undoGroup"), tx.undoGroup}, {QStringLiteral("replacementStart"), tx.replacementStart},
            {QStringLiteral("replacementEnd"), tx.replacementEnd}, {QStringLiteral("replacementText"), tx.replacementText},
            {QStringLiteral("anchor"), tx.anchor}, {QStringLiteral("position"), tx.position}};
}
QVariantMap QmlEditorController::processKeyForQml(const QString& text, int anchor, int position, const QString& input, int key, int modifiers) { return toQmlTransaction(processKey(text, anchor, position, input, key, modifiers)); }
QVariantMap QmlEditorController::processImeCommitForQml(const QString& text, int anchor, int position, const QString& textInput) { return toQmlTransaction(processImeCommit(text, anchor, position, textInput)); }
QVariantMap QmlEditorController::processPasteForQml(const QString& text, int anchor, int position, const QString& pastedText) { return toQmlTransaction(processPaste(text, anchor, position, pastedText)); }
QVariantMap QmlEditorController::acceptCompletionForQml(const QString& text, int anchor, int position) { return toQmlTransaction(acceptCompletion(text, anchor, position)); }

} // namespace miacode::qml_ui
