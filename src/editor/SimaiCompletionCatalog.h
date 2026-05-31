#pragma once

#include <QChar>
#include <QString>
#include <QStringList>

// Candidate catalog for the bracket-completion dropdown (the "tab 补全" feature).
//
// simai's three brackets each carry a distinct role, so the suggestions are
// grouped per opening glyph:
//   '['  -> note / hold / slide duration  -> "8:1]" style tokens
//   '{'  -> beat subdivision (grid)        -> "16}" style tokens
//   '('  -> BPM change                     -> only BPMs already seen in the
//                                             chart plus the &wholebpm value
//
// Everything here is pure (QString in, QString out) so it can be unit-tested
// without a widget — see src/tools/editor/SimaiCompletionCatalogSpec.cpp.
namespace miacode::editor {

// True for the three opening glyphs we offer completion for.
bool isBracketOpening(QChar ch);

// The matching closing glyph (']' '}' ')'), or a null QChar for anything else.
QChar closingBracketFor(QChar opening);

// Static duration suggestions for '['. Order is intentional (most common first).
QStringList squareDurationCandidates();

// Static subdivision suggestions for '{'. Order is intentional.
QStringList braceDivisionCandidates();

// Trim a raw BPM token to a tidy form: "120.000" -> "120", "184.5" stays.
// Returns the trimmed input unchanged when it doesn't parse as a positive number.
QString normalizeBpmToken(const QString& raw);

// Distinct BPM values that already appear as "(<number>)" markers in the chart
// body, in first-seen order, normalized via normalizeBpmToken().
QStringList appearedBpmValues(const QString& chartText);

// BPM suggestions for '(': the &wholebpm value first (when non-empty), then the
// distinct values already used in the chart, deduplicated. Each token carries
// its own trailing ')'. Empty when nothing is known yet (so no popup is shown).
QStringList parenBpmCandidates(const QString& wholeBpm, const QString& chartText);

// Dispatch to the right candidate list for the given opening glyph.
QStringList candidatesForOpening(QChar opening, const QString& wholeBpm, const QString& chartText);

}  // namespace miacode::editor
