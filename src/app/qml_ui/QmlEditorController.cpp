#include "QmlEditorController.h"

#include "editor/SimaiCompletionCatalog.h"
#include "editor/BookmarkCommentSyntax.h"
#include "editor/TouchPadAuthoringEdit.h"

#include <QtGlobal>
#include <QGuiApplication>
#include <QClipboard>
#include <QRegularExpression>

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

// The differing region between two whole-document snapshots, found by trimming
// the shared prefix and suffix. Undo/redo replay a snapshot pair, but replaying
// the whole document leaves the caret parked at a stale offset with nothing
// selected — the user cannot see what the step did.
struct TextDelta {
    int start = 0;
    int fromEnd = 0;
    int toEnd = 0;
};

TextDelta computeTextDelta(const QString& from, const QString& to)
{
    TextDelta delta;
    const int shorter = qMin(from.size(), to.size());
    int prefix = 0;
    while (prefix < shorter && from.at(prefix) == to.at(prefix)) ++prefix;
    int suffix = 0;
    while (suffix < shorter - prefix
           && from.at(from.size() - 1 - suffix) == to.at(to.size() - 1 - suffix)) {
        ++suffix;
    }
    delta.start = prefix;
    delta.fromEnd = from.size() - suffix;
    delta.toEnd = to.size() - suffix;
    return delta;
}

QRegularExpression findExpression(const QString& needle, bool caseSensitive, bool wholeWord)
{
    const QString escaped = QRegularExpression::escape(needle);
    const QString pattern = wholeWord ? QStringLiteral("\\b%1\\b").arg(escaped) : escaped;
    return QRegularExpression(pattern, caseSensitive
        ? QRegularExpression::NoPatternOption
        : QRegularExpression::CaseInsensitiveOption);
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
        if ((key == Qt::Key_Return || key == Qt::Key_Enter || key == Qt::Key_Tab)
            && !commandModifier(keyboardModifiers))
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

QmlEditorController::FindResult QmlEditorController::find(
    const QString& text, int anchor, int position, const QString& needle, bool caseSensitive,
    bool wholeWord, bool backwards) const
{
    FindResult result;
    if (needle.isEmpty()) return result;
    const auto expression = findExpression(needle, caseSensitive, wholeWord);
    const int cursor = qBound(0, backwards ? qMin(anchor, position) : qMax(anchor, position), text.size());
    QRegularExpressionMatch match;
    if (backwards) {
        auto iterator = expression.globalMatch(text.left(cursor));
        while (iterator.hasNext()) match = iterator.next();
        if (!match.hasMatch()) {
            iterator = expression.globalMatch(text);
            while (iterator.hasNext()) match = iterator.next();
        }
    } else {
        match = expression.match(text, cursor);
        if (!match.hasMatch()) match = expression.match(text, 0);
    }
    if (!match.hasMatch()) return result;
    result.found = true;
    result.start = match.capturedStart();
    result.end = match.capturedEnd();
    return result;
}

miacode::editor::SimaiTextEditResult QmlEditorController::replaceSelection(
    const QString& text, int anchor, int position, const QString& needle, const QString& replacement,
    bool caseSensitive, bool wholeWord) const
{
    auto result = untouched(text, anchor, position);
    const int start = qBound(0, qMin(anchor, position), text.size());
    const int end = qBound(start, qMax(anchor, position), text.size());
    const auto expression = findExpression(needle, caseSensitive, wholeWord);
    const auto match = expression.match(text.mid(start, end - start));
    if (needle.isEmpty() || !match.hasMatch() || match.capturedStart() != 0 || match.capturedEnd() != end - start)
        return result;
    result.consumed = true;
    result.transaction.hasEdit = result.transaction.undoGroup = true;
    result.transaction.replacementStart = start;
    result.transaction.replacementEnd = end;
    result.transaction.replacementText = replacement;
    result.transaction.text.replace(start, end - start, replacement);
    result.transaction.anchor = result.transaction.position = start + replacement.size();
    return result;
}

miacode::editor::SimaiTextEditResult QmlEditorController::replaceAll(
    const QString& text, const QString& needle, const QString& replacement, bool caseSensitive,
    bool wholeWord) const
{
    auto result = untouched(text, 0, 0);
    if (needle.isEmpty()) return result;
    int count = 0;
    auto matches = findExpression(needle, caseSensitive, wholeWord).globalMatch(text);
    while (matches.hasNext()) {
        matches.next();
        ++count;
    }
    if (count == 0) return result;
    QString replaced = text;
    replaced.replace(findExpression(needle, caseSensitive, wholeWord), replacement);
    result.consumed = true;
    result.transaction.hasEdit = result.transaction.undoGroup = true;
    result.transaction.replacementStart = 0;
    result.transaction.replacementEnd = text.size();
    result.transaction.replacementText = replaced;
    result.transaction.text = replaced;
    result.transaction.anchor = result.transaction.position = 0;
    return result;
}

void QmlEditorController::setDocumentContext(int difficultyId, quint64 revision)
{
    activeDifficultyId_ = difficultyId;
    documentRevision_ = revision;
}

bool QmlEditorController::acceptsCaret(int difficultyId, quint64 revision, bool imeComposing) const
{
    return !imeComposing && difficultyId == activeDifficultyId_ && revision == documentRevision_;
}

bool QmlEditorController::acceptsTouchAuthoring(
    int difficultyId, quint64 revision, bool imeComposing, bool editorHasFocus) const
{
    return editorHasFocus && acceptsCaret(difficultyId, revision, imeComposing);
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
    return {{QStringLiteral("consumed"), result.consumed},
            {QStringLiteral("suppressFallbackInsert"), result.suppressFallbackInsert},
            {QStringLiteral("hasEdit"), tx.hasEdit},
            {QStringLiteral("undoGroup"), tx.undoGroup}, {QStringLiteral("replacementStart"), tx.replacementStart},
            {QStringLiteral("replacementEnd"), tx.replacementEnd}, {QStringLiteral("replacementText"), tx.replacementText},
            {QStringLiteral("anchor"), tx.anchor}, {QStringLiteral("position"), tx.position}};
}
QVariantMap QmlEditorController::processKeyForQml(const QString& text, int anchor, int position, const QString& input, int key, int modifiers) { return toQmlTransaction(processKey(text, anchor, position, input, key, modifiers)); }
QVariantMap QmlEditorController::processImeCommitForQml(const QString& text, int anchor, int position, const QString& textInput) { return toQmlTransaction(processImeCommit(text, anchor, position, textInput)); }
QVariantMap QmlEditorController::processPasteForQml(const QString& text, int anchor, int position, const QString& pastedText) { return toQmlTransaction(processPaste(text, anchor, position, pastedText)); }
QVariantMap QmlEditorController::acceptCompletionForQml(const QString& text, int anchor, int position) { return toQmlTransaction(acceptCompletion(text, anchor, position)); }
QVariantMap QmlEditorController::findForQml(const QString& text, int anchor, int position, const QString& needle, bool caseSensitive, bool wholeWord, bool backwards) const
{
    const FindResult result = find(text, anchor, position, needle, caseSensitive, wholeWord, backwards);
    return {{QStringLiteral("found"), result.found}, {QStringLiteral("start"), result.start}, {QStringLiteral("end"), result.end}};
}
QVariantMap QmlEditorController::replaceSelectionForQml(const QString& text, int anchor, int position, const QString& needle, const QString& replacement, bool caseSensitive, bool wholeWord) const { return toQmlTransaction(replaceSelection(text, anchor, position, needle, replacement, caseSensitive, wholeWord)); }
QVariantMap QmlEditorController::replaceAllForQml(const QString& text, const QString& needle, const QString& replacement, bool caseSensitive, bool wholeWord) const { return toQmlTransaction(replaceAll(text, needle, replacement, caseSensitive, wholeWord)); }
void QmlEditorController::setDocumentContextForQml(int difficultyId, qulonglong revision) { setDocumentContext(difficultyId, revision); }
bool QmlEditorController::publishCaretForQml(int difficultyId, qulonglong revision, int anchor, int position, bool imeComposing)
{
    Q_UNUSED(anchor);
    Q_UNUSED(position);
    return acceptsCaret(difficultyId, revision, imeComposing);
}
bool QmlEditorController::acceptsTouchAuthoringForQml(int difficultyId, qulonglong revision, bool imeComposing, bool editorHasFocus) const { return acceptsTouchAuthoring(difficultyId, revision, imeComposing, editorHasFocus); }
QVariantList QmlEditorController::bookmarksForQml(const QString& text) const
{
    QVariantList result;
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (int i = 0; i < lines.size(); ++i) {
        const auto bookmark = miacode::editor::parseBookmarkComment(lines.at(i));
        if (!bookmark.has_value()) continue;
        result.append(QVariantMap{{QStringLiteral("line"), i + 1}, {QStringLiteral("title"), bookmark->title}});
    }
    return result;
}
QVariantMap QmlEditorController::createBookmarkForQml(const QString& text, int line, const QString& title) const
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int index = qBound(0, line - 1, qMax(0, lines.size() - 1));
    int offset = 0;
    for (int i = 0; i < index; ++i) offset += lines.at(i).size() + 1;
    const QString replacement = miacode::editor::appendBookmarkComment(lines.at(index), title);
    if (replacement == lines.at(index)) return toQmlTransaction(untouched(text, offset, offset));
    auto result = untouched(text, offset, offset);
    result.consumed = true;
    result.transaction.hasEdit = result.transaction.undoGroup = true;
    result.transaction.replacementStart = offset;
    result.transaction.replacementEnd = offset + lines.at(index).size();
    result.transaction.replacementText = replacement;
    result.transaction.text.replace(offset, lines.at(index).size(), replacement);
    result.transaction.anchor = result.transaction.position = offset + replacement.size();
    return toQmlTransaction(result);
}
QVariantMap QmlEditorController::renameBookmarkForQml(const QString& text, int line, const QString& title) const
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int index = line - 1;
    if (index < 0 || index >= lines.size()) return toQmlTransaction(untouched(text, 0, 0));
    if (!miacode::editor::parseBookmarkComment(lines.at(index)).has_value()) return toQmlTransaction(untouched(text, 0, 0));
    int offset = 0; for (int i = 0; i < index; ++i) offset += lines.at(i).size() + 1;
    auto result = untouched(text, offset, offset);
    result.consumed = true; result.transaction.hasEdit = result.transaction.undoGroup = true;
    result.transaction.replacementStart = offset;
    result.transaction.replacementEnd = offset + lines.at(index).size();
    result.transaction.replacementText = miacode::editor::renameBookmarkComment(lines.at(index), title);
    result.transaction.text.replace(result.transaction.replacementStart, result.transaction.replacementEnd - result.transaction.replacementStart, result.transaction.replacementText);
    result.transaction.anchor = result.transaction.position = result.transaction.replacementStart + result.transaction.replacementText.size();
    return toQmlTransaction(result);
}
QVariantMap QmlEditorController::deleteBookmarkForQml(const QString& text, int line) const
{
    const QStringList lines = text.split(QLatin1Char('\n'));
    const int index = line - 1;
    if (index < 0 || index >= lines.size()) return toQmlTransaction(untouched(text, 0, 0));
    if (!miacode::editor::parseBookmarkComment(lines.at(index)).has_value()) return toQmlTransaction(untouched(text, 0, 0));
    int offset = 0; for (int i = 0; i < index; ++i) offset += lines.at(i).size() + 1;
    auto result = untouched(text, offset, offset);
    result.consumed = true; result.transaction.hasEdit = result.transaction.undoGroup = true;
    result.transaction.replacementStart = offset;
    result.transaction.replacementEnd = offset + lines.at(index).size();
    result.transaction.replacementText = miacode::editor::removeBookmarkComment(lines.at(index));
    result.transaction.text.replace(result.transaction.replacementStart,
                                    result.transaction.replacementEnd - result.transaction.replacementStart,
                                    result.transaction.replacementText);
    result.transaction.anchor = result.transaction.position = result.transaction.replacementStart;
    return toQmlTransaction(result);
}

QVariantMap QmlEditorController::touchPadAuthoringForQml(
    const QString& text, int anchor, int position, const QString& pad,
    bool useBacktickSeparator) const
{
    const miacode::editor::TouchPadAuthoringEditPlan plan =
        miacode::editor::planTouchPadAuthoringEdit(text, position, pad, useBacktickSeparator);
    auto result = untouched(text, anchor, position);
    if (!plan.valid) {
        return toQmlTransaction(result);
    }
    const int start = qBound(0, plan.insertionPosition, text.size());
    const int end = qBound(start, start + plan.removalLength, text.size());
    result.consumed = true;
    result.transaction.hasEdit = result.transaction.undoGroup = true;
    result.transaction.replacementStart = start;
    result.transaction.replacementEnd = end;
    result.transaction.replacementText = plan.insertionText;
    result.transaction.text.replace(start, end - start, plan.insertionText);
    result.transaction.anchor = result.transaction.position = start + plan.insertionText.size();
    QVariantMap transaction = toQmlTransaction(result);
    transaction.insert(QStringLiteral("touchTokenStart"), plan.tokenStart);
    return transaction;
}
void QmlEditorController::resetQmlHistory(const QString& text, int anchor, int position)
{
    Q_UNUSED(text);
    Q_UNUSED(anchor);
    Q_UNUSED(position);
    qmlUndo_.clear();
    qmlRedo_.clear();
    setUndoAvailability(false, false);
}

void QmlEditorController::recordQmlTransaction(const QString& before, const QString& after,
                                               int beforeAnchor, int beforePosition,
                                               int afterAnchor, int afterPosition)
{
    if (before == after) return;
    qmlUndo_.append({before, after, beforeAnchor, beforePosition, afterAnchor, afterPosition});
    qmlRedo_.clear();
    setUndoAvailability(true, false);
}
QVariantMap QmlEditorController::restoreTransaction(const QString& current, const QString& restored) const
{
    const TextDelta delta = computeTextDelta(current, restored);
    const QString replacement = restored.mid(delta.start, delta.toEnd - delta.start);
    // The caret lands on the restored text and selects it, so the step is
    // visible. Undoing an insertion restores nothing, which collapses the
    // selection at the point the inserted text used to begin.
    return {{QStringLiteral("consumed"), true},
            {QStringLiteral("hasEdit"), true},
            {QStringLiteral("replacementStart"), delta.start},
            {QStringLiteral("replacementEnd"), delta.fromEnd},
            {QStringLiteral("replacementText"), replacement},
            {QStringLiteral("anchor"), delta.start},
            {QStringLiteral("position"), delta.start + replacement.size()}};
}
QVariantMap QmlEditorController::undoQmlTransaction()
{
    if (qmlUndo_.isEmpty()) return {};
    const auto entry = qmlUndo_.takeLast(); qmlRedo_.append(entry);
    setUndoAvailability(!qmlUndo_.isEmpty(), true);
    return restoreTransaction(entry.after, entry.before);
}
QVariantMap QmlEditorController::redoQmlTransaction()
{
    if (qmlRedo_.isEmpty()) return {};
    const auto entry = qmlRedo_.takeLast(); qmlUndo_.append(entry);
    setUndoAvailability(true, !qmlRedo_.isEmpty());
    return restoreTransaction(entry.before, entry.after);
}

} // namespace miacode::qml_ui
