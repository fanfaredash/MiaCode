#include "SimaiTextEditPolicy.h"

#include "SimaiCompletionCatalog.h"

#include <QtGlobal>

namespace miacode::editor {
namespace {

QChar normalizedHalfWidthChar(QChar ch)
{
    const ushort code = ch.unicode();
    if (code == 0x3000) return QLatin1Char(' ');
    if (code >= 0xFF01 && code <= 0xFF5E) return QChar(code - 0xFEE0);
    switch (code) {
    case 0x3001: return QLatin1Char('/');
    case 0x3002: return QLatin1Char('.');
    case 0x00B7: return QLatin1Char('`');
    case 0x300A: return QLatin1Char('<');
    case 0x300B: return QLatin1Char('>');
    case 0x3010: return QLatin1Char('[');
    case 0x3011: return QLatin1Char(']');
    case 0xFFE5: return QLatin1Char('$');
    default: return ch;
    }
}

QString normalizedInput(const SimaiTextEditRequest& request)
{
    if (!request.halfWidthInputEnabled) return request.input;
    if (!request.isImeCommit && request.modifiers == Qt::ShiftModifier) {
        if (request.key == Qt::Key_6) return QStringLiteral("^");
        if (request.key == Qt::Key_4) return QStringLiteral("$");
    }
    if (request.input == QStringLiteral("……") || request.input == QStringLiteral("…")) return QStringLiteral("^");
    QString result = request.input;
    for (QChar& ch : result) ch = normalizedHalfWidthChar(ch);
    return result;
}

bool hasCommandModifier(Qt::KeyboardModifiers modifiers)
{
    return modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
}

// What Qt's own text controls would accept as typed text — the same rule as
// QInputControl::isAcceptableInput. The policy's fallback inserts `input`
// under the claim that Qt would have inserted it anyway, and that claim is only
// true for text this returns true for.
//
// Escape is why this exists. Its key text is U+001B: non-empty, unmodified, and
// not something Qt would ever put in a document — but the fallback's only gate
// was "is there text", so pressing Escape typed a control character into the
// chart. Every non-printable key with a non-empty text() had the same hole.
bool isInsertableText(const QString& input)
{
    if (input.isEmpty()) return false;
    const QChar first = input.at(0);
    // Zero-width joiners and friends carry no glyph but are real content.
    if (first.category() == QChar::Other_Format) return true;
    if (first == QLatin1Char('\t')) return true;
    return first.isPrint();
}

// Alt/Option is a legitimate composition modifier (Option+o types "ø" on
// macOS), and Ctrl-modified characters are already dropped by Qt's text
// controls. Only a Meta/Super modifier reaches their raw insertion fallback.
bool wouldInsertLiteralCommandText(const SimaiTextEditRequest& request)
{
    if (!(request.modifiers & Qt::MetaModifier)) return false;
    if (request.modifiers & Qt::ControlModifier) return false;
    return isInsertableText(request.input);
}

SimaiTextEditResult untouched(const SimaiTextEditRequest& request)
{
    SimaiTextEditResult result;
    result.transaction.text = request.text;
    result.transaction.anchor = qBound(0, request.anchor, request.text.size());
    result.transaction.position = qBound(0, request.position, request.text.size());
    return result;
}

void replaceSelection(SimaiTextEditResult* result, const QString& value)
{
    const int start = qMin(result->transaction.anchor, result->transaction.position);
    const int end = qMax(result->transaction.anchor, result->transaction.position);
    result->transaction.replacementStart = start;
    result->transaction.replacementEnd = end;
    result->transaction.replacementText = value;
    result->transaction.text.replace(start, end - start, value);
    const int caret = start + value.size();
    result->transaction.anchor = caret;
    result->transaction.position = caret;
    result->transaction.hasEdit = true;
    result->transaction.undoGroup = true;
}

void openCompletion(SimaiTextEditResult* result, QChar opening, bool closingPresent,
                    const QStringList& candidates)
{
    if (candidates.isEmpty()) return;
    result->completion.active = true;
    result->completion.opening = opening;
    result->completion.closingPresent = closingPresent;
    result->completion.startPosition = result->transaction.position;
    result->completion.candidates = candidates;
}

} // namespace

SimaiTextEditResult applySimaiTextEditPolicy(const SimaiTextEditRequest& request)
{
    SimaiTextEditResult result = untouched(request);
    // Computed before every early return: whichever branch declines the key,
    // the adapter still has to stop Qt from typing its literal character.
    result.suppressFallbackInsert = wouldInsertLiteralCommandText(request);
    const QString input = normalizedInput(request);
    const bool selected = result.transaction.anchor != result.transaction.position;
    const bool smartEnabled = request.autoCompletionEnabled && !request.overwriteMode;

    const bool enter = request.key == Qt::Key_Return || request.key == Qt::Key_Enter;
    const bool ctrlOnly = (request.modifiers & Qt::ControlModifier)
        && !(request.modifiers & (Qt::AltModifier | Qt::MetaModifier));
    if (enter && (!hasCommandModifier(request.modifiers) || ctrlOnly)) {
        replaceSelection(&result, QStringLiteral("\n"));
        result.transaction.insertsBlock = true;
        result.consumed = true;
        return result;
    }

    if (smartEnabled && request.key == Qt::Key_Backspace && !selected && !hasCommandModifier(request.modifiers)) {
        const int pos = result.transaction.position;
        if (pos > 0 && pos < result.transaction.text.size()) {
            const QChar opening = result.transaction.text.at(pos - 1);
            if (isBracketOpening(opening) && result.transaction.text.at(pos) == closingBracketFor(opening)) {
                result.transaction.text.remove(pos - 1, 2);
                result.transaction.replacementStart = pos - 1;
                result.transaction.replacementEnd = pos + 1;
                result.transaction.replacementText.clear();
                result.transaction.anchor = pos - 1;
                result.transaction.position = pos - 1;
                result.transaction.hasEdit = true;
                result.transaction.undoGroup = true;
                result.consumed = true;
                return result;
            }
        }
    }

    // QKeyEvent::text() carries the control characters '\b' and '\x7f' for
    // Backspace and Delete. They are commands, not insertable text: after the
    // empty-pair special case above declines the event, the owning text
    // control must perform its native selection/character/word deletion.
    if (request.key == Qt::Key_Backspace || request.key == Qt::Key_Delete) return result;

    // In overwrite mode QTextEdit owns character replacement. Smart typing
    // must not consume the event or synthesize a regular insertion here.
    if (request.overwriteMode) return result;

    // Declining leaves the key to Qt, which applies the same rule and declines
    // it too — which is the point: a key neither of them will type is a key
    // that types nothing.
    const bool canHandleInput = isInsertableText(input) && !hasCommandModifier(request.modifiers);
    if (!canHandleInput) return result;

    if (smartEnabled && input.size() == 1) {
        const QChar glyph = input.at(0);
        const int pos = result.transaction.position;
        if (!selected && glyph == QLatin1Char('[') && pos < result.transaction.text.size()
            && result.transaction.text.at(pos) == QLatin1Char('[')) {
            ++result.transaction.anchor;
            ++result.transaction.position;
            result.consumed = true;
            openCompletion(&result, glyph, false,
                           candidatesForOpening(glyph, request.wholeBpm, result.transaction.text));
            return result;
        }
        if (!selected && isBracketClosing(glyph) && pos < result.transaction.text.size()
            && result.transaction.text.at(pos) == glyph) {
            ++result.transaction.anchor;
            ++result.transaction.position;
            result.consumed = true;
            return result;
        }
        if (isBracketOpening(glyph)) {
            replaceSelection(&result, QString(glyph) + closingBracketFor(glyph));
            --result.transaction.anchor;
            --result.transaction.position;
            result.consumed = true;
            openCompletion(&result, glyph, true,
                           candidatesForOpening(glyph, request.wholeBpm, result.transaction.text));
            return result;
        }
        if (!request.completionActive && !request.isImeCommit && input == QLatin1String("h")) {
            replaceSelection(&result, input);
            result.consumed = true;
            const bool beforeDuration = result.transaction.position < result.transaction.text.size()
                && result.transaction.text.at(result.transaction.position) == QLatin1Char('[');
            if (!beforeDuration) openCompletion(&result, QLatin1Char('['), false, holdDurationCandidates());
            return result;
        }
    }

    // A policy edit only covers text that Qt would otherwise insert directly.
    replaceSelection(&result, input);
    result.consumed = true;
    return result;
}

} // namespace miacode::editor
