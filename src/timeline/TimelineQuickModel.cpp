#include "timeline/TimelineQuickModel.h"

#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <limits>

namespace {

constexpr double kDefaultBpm = 120.0;
constexpr int kDefaultBeats = 4;
constexpr int kDefaultMeterNumerator = 4;
constexpr int kDefaultMeterDenominator = 4;
constexpr double kTimelineEpsilon = 1e-6;

bool isDigitLane(QChar ch)
{
    return ch >= QChar('1') && ch <= QChar('8');
}

bool isTouchPrefix(const QString& token)
{
    if (!token.isEmpty() && token.at(0) == QChar('C')) {
        return true;
    }
    return token.size() >= 2
        && QStringLiteral("ABDE").contains(token.at(0))
        && isDigitLane(token.at(1));
}

int touchPrefixLength(const QString& token)
{
    if (token.isEmpty()) {
        return 0;
    }
    if (token.at(0) == QChar('C')) {
        return 1;
    }
    return (token.size() >= 2 && QStringLiteral("ABDE").contains(token.at(0)) && isDigitLane(token.at(1))) ? 2 : 0;
}

double noteStepSeconds(double bpm, int beats)
{
    const double clampedBpm = bpm > 0.0 ? bpm : kDefaultBpm;
    const int clampedBeats = std::max(1, beats);
    return 240.0 / (clampedBpm * static_cast<double>(clampedBeats));
}

double measureDurationSeconds(double bpm, int meterNumerator, int meterDenominator)
{
    const int clampedNumerator = std::max(1, meterNumerator);
    return noteStepSeconds(bpm, meterDenominator) * static_cast<double>(clampedNumerator);
}

void applyInitialTimingMetadata(
    int* meterNumerator,
    int* meterDenominator,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    if (meterNumerator == nullptr || meterDenominator == nullptr) {
        return;
    }
    *meterNumerator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureNumerator
        : kDefaultMeterNumerator;
    *meterDenominator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureDenominator
        : kDefaultMeterDenominator;
}

void appendDistinctSecond(QVector<double>* seconds, double second)
{
    if (seconds == nullptr) {
        return;
    }
    if (!seconds->isEmpty() && qAbs(seconds->constLast() - second) <= kTimelineEpsilon) {
        return;
    }
    seconds->append(second);
}

enum class SlideHeadlessMode {
    None,
    Gradual,
    Immediate,
};

struct TapModifierState {
    bool hasBreak = false;
    bool hasEx = false;
    bool hasHold = false;
    bool tapUsesStarMaterial = false;
    bool tapStarDouble = false;
};

struct SlideHeadModifierState {
    QString rawModifiers;
    bool headBreak = false;
    bool headEx = false;
    bool slideHeadUsesTapMaterial = false;
    SlideHeadlessMode headlessMode = SlideHeadlessMode::None;
};

bool parseTapModifierSequence(
    const QString& prefixModifiers,
    const QString& suffixModifiers,
    TapModifierState* state)
{
    if (state == nullptr) {
        return false;
    }
    *state = TapModifierState();

    const auto parsePart = [state](const QString& part) -> bool {
        for (int i = 0; i < part.size();) {
            const QChar ch = part.at(i);
            const QChar lower = ch.toLower();
            if (lower == QLatin1Char('b')) {
                if (state->hasBreak) {
                    return false;
                }
                state->hasBreak = true;
                ++i;
                continue;
            }
            if (lower == QLatin1Char('x')) {
                if (state->hasEx) {
                    return false;
                }
                state->hasEx = true;
                ++i;
                continue;
            }
            if (lower == QLatin1Char('h')) {
                if (state->hasHold) {
                    return false;
                }
                state->hasHold = true;
                ++i;
                continue;
            }
            if (ch == QLatin1Char('$')) {
                if (state->tapUsesStarMaterial) {
                    return false;
                }
                state->tapUsesStarMaterial = true;
                if (i + 1 < part.size() && part.at(i + 1) == QLatin1Char('$')) {
                    state->tapStarDouble = true;
                    i += 2;
                } else {
                    ++i;
                }
                continue;
            }
            return false;
        }
        return true;
    };

    if (!parsePart(prefixModifiers) || !parsePart(suffixModifiers)) {
        return false;
    }
    if (state->hasHold && state->tapUsesStarMaterial) {
        return false;
    }
    return true;
}

bool parseSlideHeadModifierPrefix(const QString& token, int* modifierCount, SlideHeadModifierState* state)
{
    if (modifierCount == nullptr || state == nullptr) {
        return false;
    }
    *modifierCount = 0;
    *state = SlideHeadModifierState();

    while ((1 + *modifierCount) < token.size()) {
        const QChar ch = token.at(1 + *modifierCount);
        const QChar lower = ch.toLower();
        if (lower == QLatin1Char('b')) {
            if (state->headBreak) {
                return false;
            }
            state->headBreak = true;
        } else if (lower == QLatin1Char('x')) {
            if (state->headEx) {
                return false;
            }
            state->headEx = true;
        } else if (ch == QLatin1Char('@')) {
            if (state->slideHeadUsesTapMaterial) {
                return false;
            }
            state->slideHeadUsesTapMaterial = true;
        } else if (ch == QLatin1Char('?')) {
            if (state->headlessMode != SlideHeadlessMode::None) {
                return false;
            }
            state->headlessMode = SlideHeadlessMode::Gradual;
        } else if (ch == QLatin1Char('!')) {
            if (state->headlessMode != SlideHeadlessMode::None) {
                return false;
            }
            state->headlessMode = SlideHeadlessMode::Immediate;
        } else if (lower == QLatin1Char('h')) {
            return false;
        } else {
            break;
        }
        state->rawModifiers.append(ch);
        ++(*modifierCount);
    }

    if (state->slideHeadUsesTapMaterial && state->headlessMode != SlideHeadlessMode::None) {
        return false;
    }
    return true;
}

bool slideCoreHasDisallowedModifiers(const QString& core)
{
    for (int i = 1; i < core.size(); ++i) {
        const QChar ch = core.at(i);
        const QChar lower = ch.toLower();
        if (lower == QLatin1Char('x')
            || lower == QLatin1Char('h')
            || ch == QLatin1Char('@')
            || ch == QLatin1Char('?')
            || ch == QLatin1Char('!')
            || ch == QLatin1Char('$')) {
            return true;
        }
    }
    return false;
}

QString tokenInsideBrackets(const QString& token)
{
    const int open = token.indexOf(QLatin1Char('['));
    const int close = token.indexOf(QLatin1Char(']'), open + 1);
    if (open < 0 || close <= open) {
        return QString();
    }
    return token.mid(open + 1, close - open - 1);
}

double parseHoldDurationSignature(const QString& signature, double bpm, bool* ok)
{
    bool localOk = false;
    auto* okOut = ok != nullptr ? ok : &localOk;
    *okOut = false;

    const auto parseBeatFraction = [](const QString& text, double useBpm, bool* localOk) -> double {
        const int colon = text.indexOf(QLatin1Char(':'));
        if (colon <= 0 || colon >= text.size() - 1) {
            *localOk = false;
            return 0.0;
        }
        bool beatsOk = false;
        bool numOk = false;
        const int beats = text.left(colon).toInt(&beatsOk);
        const int num = text.mid(colon + 1).toInt(&numOk);
        if (!beatsOk || !numOk || beats <= 0 || useBpm <= 0.0) {
            *localOk = false;
            return 0.0;
        }
        *localOk = true;
        return 240.0 * static_cast<double>(num) / (useBpm * static_cast<double>(beats));
    };

    if (signature.isEmpty()) {
        return 0.0;
    }
    if (signature.contains(QLatin1Char('#'))) {
        const QStringList hashParts = signature.split(QLatin1Char('#'), Qt::KeepEmptyParts);
        if (hashParts.size() == 2) {
            bool bpmOk = false;
            const double tempBpm = hashParts.at(0).toDouble(&bpmOk);
            if (!bpmOk || tempBpm <= 0.0) {
                return 0.0;
            }
            return parseBeatFraction(hashParts.at(1), tempBpm, okOut);
        }
    }
    return parseBeatFraction(signature, bpm, okOut);
}

bool parseSlideWaitAndDuration(const QString& signature, double bpm, double* waitSecond, double* durationSecond)
{
    if (waitSecond == nullptr || durationSecond == nullptr || signature.isEmpty()) {
        return false;
    }

    *waitSecond = 0.0;
    *durationSecond = 0.0;
    const auto parseFraction = [](const QString& text, double useBpm, double* outSeconds) -> bool {
        const int colon = text.indexOf(QLatin1Char(':'));
        if (colon <= 0 || colon >= text.size() - 1) {
            return false;
        }
        bool beatsOk = false;
        bool numOk = false;
        const int beats = text.left(colon).toInt(&beatsOk);
        const int num = text.mid(colon + 1).toInt(&numOk);
        if (!beatsOk || !numOk || beats <= 0 || useBpm <= 0.0) {
            return false;
        }
        *outSeconds = 240.0 * static_cast<double>(num) / (useBpm * static_cast<double>(beats));
        return true;
    };

    if (signature.contains(QStringLiteral("###")) || signature.count(QLatin1Char('#')) > 3) {
        return false;
    }

    const int doubleHashIndex = signature.indexOf(QStringLiteral("##"));
    if (doubleHashIndex >= 0) {
        bool waitOk = false;
        const double explicitWait = signature.left(doubleHashIndex).toDouble(&waitOk);
        if (!waitOk) {
            return false;
        }
        *waitSecond = std::max(0.0, explicitWait);
        const QString tail = signature.mid(doubleHashIndex + 2);
        const int hashIndex = tail.indexOf(QLatin1Char('#'));
        if (hashIndex >= 0) {
            bool bpmOk = false;
            const double tempBpm = tail.left(hashIndex).toDouble(&bpmOk);
            if (!bpmOk || tempBpm <= 0.0) {
                return false;
            }
            return parseFraction(tail.mid(hashIndex + 1), tempBpm, durationSecond);
        }
        if (parseFraction(tail, bpm, durationSecond)) {
            return true;
        }
        bool durationOk = false;
        const double seconds = tail.toDouble(&durationOk);
        if (!durationOk) {
            return false;
        }
        *durationSecond = std::max(0.0, seconds);
        return true;
    }

    const int hashIndex = signature.indexOf(QLatin1Char('#'));
    if (hashIndex >= 0) {
        bool bpmOk = false;
        const double tempBpm = signature.left(hashIndex).toDouble(&bpmOk);
        if (!bpmOk || tempBpm <= 0.0) {
            return false;
        }
        *waitSecond = 60.0 / tempBpm;
        const QString tail = signature.mid(hashIndex + 1);
        if (parseFraction(tail, tempBpm, durationSecond)) {
            return true;
        }
        bool durationOk = false;
        const double seconds = tail.toDouble(&durationOk);
        if (!durationOk) {
            return false;
        }
        *durationSecond = std::max(0.0, seconds);
        return true;
    }

    *waitSecond = 60.0 / std::max(1.0, bpm);
    return parseFraction(signature, bpm, durationSecond);
}

bool extractSlideTiming(const QString& slideCore, double bpm, double* waitSecond, double* durationSecond)
{
    if (waitSecond == nullptr || durationSecond == nullptr) {
        return false;
    }

    QVector<QString> signatures;
    for (int i = 0; i < slideCore.size(); ++i) {
        if (slideCore.at(i) != QLatin1Char('[')) {
            continue;
        }
        QString signature;
        ++i;
        while (i < slideCore.size() && slideCore.at(i) != QLatin1Char(']')) {
            signature.append(slideCore.at(i));
            ++i;
        }
        if (i >= slideCore.size() || slideCore.at(i) != QLatin1Char(']')) {
            return false;
        }
        signatures.append(signature);
    }

    if (signatures.isEmpty()) {
        return false;
    }

    *waitSecond = 0.0;
    *durationSecond = 0.0;
    bool first = true;
    for (const QString& signature : signatures) {
        double localWait = 0.0;
        double localDuration = 0.0;
        if (!parseSlideWaitAndDuration(signature, bpm, &localWait, &localDuration)) {
            return false;
        }
        if (first) {
            *waitSecond = std::max(0.0, localWait);
            first = false;
        }
        *durationSecond += std::max(0.0, localDuration);
    }
    return true;
}

bool parseTouchSuffix(
    const QString& token,
    QString* durationSignature,
    bool* hasHold,
    bool* hasFirework,
    bool* hasBreak)
{
    if (durationSignature == nullptr || hasHold == nullptr || hasFirework == nullptr || hasBreak == nullptr) {
        return false;
    }
    durationSignature->clear();
    *hasHold = false;
    *hasFirework = false;
    *hasBreak = false;

    const int prefixLength = touchPrefixLength(token);
    if (prefixLength <= 0 || prefixLength > token.size()) {
        return false;
    }

    const QString suffix = token.mid(prefixLength);
    const int openBracket = suffix.indexOf(QLatin1Char('['));
    const int closeBracket = suffix.indexOf(QLatin1Char(']'));
    if (openBracket < 0 && closeBracket >= 0) {
        return false;
    }

    QString prefixModifiers = suffix;
    QString suffixModifiers;
    if (openBracket >= 0) {
        if (closeBracket <= openBracket) {
            return false;
        }
        prefixModifiers = suffix.left(openBracket);
        suffixModifiers = suffix.mid(closeBracket + 1);
        *durationSignature = suffix.mid(openBracket + 1, closeBracket - openBracket - 1);
    }

    const auto parseModifierPart = [hasHold, hasFirework, hasBreak](const QString& modifiers) -> bool {
        for (QChar ch : modifiers) {
            if (ch == QLatin1Char('h')) {
                *hasHold = true;
            } else if (ch == QLatin1Char('f')) {
                *hasFirework = true;
            } else if (ch == QLatin1Char('b')) {
                *hasBreak = true;
            } else if (ch == QLatin1Char('x') || ch.isSpace()) {
                continue;
            } else {
                return false;
            }
        }
        return true;
    };

    if (!parseModifierPart(prefixModifiers) || !parseModifierPart(suffixModifiers)) {
        return false;
    }
    if (openBracket >= 0 && !*hasHold) {
        return false;
    }
    if (*hasHold && (openBracket < 0 || durationSignature->isEmpty())) {
        return false;
    }
    return true;
}

int inferSlideEndLane(const QString& token, int fallbackLane)
{
    QString stripped;
    stripped.reserve(token.size());
    bool insideBracket = false;
    for (QChar ch : token) {
        if (ch == QLatin1Char('[')) {
            insideBracket = true;
            continue;
        }
        if (ch == QLatin1Char(']')) {
            insideBracket = false;
            continue;
        }
        if (!insideBracket) {
            stripped.append(ch);
        }
    }

    for (int i = stripped.size() - 1; i >= 0; --i) {
        if (isDigitLane(stripped.at(i))) {
            return stripped.at(i).digitValue();
        }
    }
    return fallbackLane;
}

QString lineTextForBlock(const QTextBlock& block)
{
    QString text = block.text();
    if (text.endsWith(QLatin1Char('\r'))) {
        text.chop(1);
    }
    return text;
}

}  // namespace

void TimelineQuickModel::clear()
{
    nextLineId_ = 1;
    nextEachGroupId_ = 0;
    lines_.clear();
    snapshot_.lines.clear();
    snapshot_.noteVisualEndPrefixMaxWithSlideTracks.clear();
    snapshot_.noteVisualEndPrefixMaxWithoutSlideTracks.clear();
    snapshot_.durationSeconds = 0.0;
    snapshot_.minimumSecond = -0.5;
    snapshot_.maximumSecond = 1.0;
    everyCommaAnchorsBySecond_.clear();
    linesWithEveryComma_.clear();
    linesWithNotes_.clear();
}

bool TimelineQuickModel::replaceDocumentTail(
    const QTextDocument* document,
    int startLineIndex,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    Q_UNUSED(startLineIndex);
    return rebuildFromDocument(document, firstSeconds, timingMetadata);
}

bool TimelineQuickModel::rebuildFromText(
    const QString& text,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    QVector<QString> lines = text.split(QLatin1Char('\n')).toVector();
    if (lines.isEmpty()) {
        lines.append(QString());
    }
    return rebuildFromLineTexts(lines, firstSeconds, timingMetadata);
}

bool TimelineQuickModel::rebuildFromDocument(
    const QTextDocument* document,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    return rebuildFromLineTexts(collectDocumentLines(document), firstSeconds, timingMetadata);
}

bool TimelineQuickModel::applyContentsChange(
    const QTextDocument* document,
    int position,
    int charsRemoved,
    int charsAdded,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    if (document == nullptr || lines_.isEmpty()) {
        return rebuildFromDocument(document, firstSeconds, timingMetadata);
    }

    const int startLineIndex = lineIndexForStoredPosition(position);
    const int oldEndLineIndex = charsRemoved > 0
        ? lineIndexForStoredPosition(position + charsRemoved)
        : startLineIndex;
    const int newEndLineIndex = charsAdded > 0
        ? lineIndexForDocumentPosition(document, position + charsAdded)
        : startLineIndex;
    const QVector<QString> replacementLines = collectDocumentLines(document, startLineIndex, newEndLineIndex);

    QVector<LineState> inserted;
    inserted.reserve(replacementLines.size());
    for (const QString& text : replacementLines) {
        LineState line;
        line.lineId = allocateLineId();
        line.text = text;
        inserted.append(line);
    }

    const int replaceCount = qMax(0, oldEndLineIndex - startLineIndex + 1);
    lines_.erase(lines_.begin() + startLineIndex, lines_.begin() + startLineIndex + replaceCount);
    for (int insertIndex = 0; insertIndex < inserted.size(); ++insertIndex) {
        lines_.insert(startLineIndex + insertIndex, inserted.at(insertIndex));
    }
    resequenceLineMetadata(startLineIndex);

    ParseState currentState;
    if (startLineIndex > 0) {
        currentState = lines_.at(startLineIndex - 1).endState;
    } else {
        currentState.second = firstSeconds;
        currentState.bpm = kDefaultBpm;
        currentState.beats = kDefaultBeats;
        currentState.subdivisionIndex = 0;
        applyInitialTimingMetadata(
            &currentState.meterNumerator,
            &currentState.meterDenominator,
            timingMetadata);
        currentState.currentMeasureStartSecond = firstSeconds;
    }

    const int guaranteedReparseEnd = qMin(lines_.size() - 1, startLineIndex + inserted.size() - 1);
    for (int index = startLineIndex; index < lines_.size(); ++index) {
        LineState& line = lines_[index];
        const ParseState oldStartState = line.startState;
        const ParseState oldEndState = line.endState;
        const bool mustReparse = index <= guaranteedReparseEnd
            || qAbs(currentState.bpm - oldStartState.bpm) > kTimelineEpsilon
            || currentState.beats != oldStartState.beats
            || currentState.subdivisionIndex != oldStartState.subdivisionIndex
            || currentState.meterNumerator != oldStartState.meterNumerator
            || currentState.meterDenominator != oldStartState.meterDenominator
            || qAbs(currentState.currentMeasureStartSecond - oldStartState.currentMeasureStartSecond) > kTimelineEpsilon;

        if (mustReparse) {
            parseLine(&line, currentState);
        } else {
            shiftLineTiming(&line, currentState.second - oldStartState.second);
            line.startState = currentState;
            line.endState.bpm = oldEndState.bpm;
            line.endState.beats = oldEndState.beats;
            line.endState.subdivisionIndex = oldEndState.subdivisionIndex;
            line.endState.meterNumerator = oldEndState.meterNumerator;
            line.endState.meterDenominator = oldEndState.meterDenominator;
        }

        currentState = line.endState;
        if (index > guaranteedReparseEnd && parseStatesEqual(line.endState, oldEndState)) {
            break;
        }
    }

    rebuildSlideDerivedFlags();
    rebuildAnchorLineIndices();
    rebuildSnapshotDuration();
    return true;
}

const TimelineRenderSnapshot& TimelineQuickModel::snapshot() const
{
    return snapshot_;
}

double TimelineQuickModel::timelineSecondForCursor(int lineNumber, int col) const
{
    double second = 0.0;
    resolvePreviousCursorAnchorForTextPosition(lineNumber, col, nullptr, nullptr, &second);
    return second;
}

bool TimelineQuickModel::resolveTimelineNavigateCursor(double second, int* line, int* col, double* cursorSecond) const
{
    const int targetLineIndex = resolveRecentPastLineIndex(second);
    if (targetLineIndex >= 0) {
        const LineState& targetLine = lines_.at(targetLineIndex);
        if (!targetLine.cursorCache.everyComma.isEmpty()) {
            const TimelineCursorAnchor& anchor = targetLine.cursorCache.everyComma.constFirst();
            if (line != nullptr) {
                *line = targetLine.lineNumber;
            }
            if (col != nullptr) {
                *col = anchor.sourceCol;
            }
            if (cursorSecond != nullptr) {
                *cursorSecond = qMax(0.0, timelineRenderAbsoluteSecond(targetLine.render, anchor.secondOffset));
            }
            return true;
        }
    }

    if (resolvePreviousCursorAnchorForSecond(second, line, col, cursorSecond)) {
        return true;
    }

    if (line != nullptr) {
        *line = 1;
    }
    if (col != nullptr) {
        *col = 1;
    }
    if (cursorSecond != nullptr) {
        *cursorSecond = 0.0;
    }
    return false;
}

bool TimelineQuickModel::resolveNearestTimelineNote(double second, int lane, int* line, int* col, double* noteSecond) const
{
    if (linesWithNotes_.isEmpty()) {
        return false;
    }

    struct Candidate {
        bool found = false;
        int line = 1;
        int col = 1;
        double second = 0.0;
        double delta = 0.0;
    };

    const auto inspectLine = [second](const LineState& lineState, int laneFilter, Candidate* best) {
        if (best == nullptr || lineState.render.notes.isEmpty()) {
            return;
        }
        for (const TimelineRenderNote& note : lineState.render.notes) {
            if (laneFilter >= 1 && note.lane != laneFilter) {
                continue;
            }
            const double absoluteSecond = timelineRenderAbsoluteSecond(lineState.render, note.secondOffset);
            const double delta = qAbs(absoluteSecond - second);
            if (!best->found
                || delta + kTimelineEpsilon < best->delta
                || (qAbs(delta - best->delta) <= kTimelineEpsilon
                    && (lineState.lineNumber < best->line
                        || (lineState.lineNumber == best->line && note.sourceCol < best->col)))) {
                best->found = true;
                best->line = lineState.lineNumber;
                best->col = note.sourceCol;
                best->second = absoluteSecond;
                best->delta = delta;
            }
        }
    };

    const auto findCandidate = [&](int laneFilter) -> Candidate {
        Candidate best;
        int right = static_cast<int>(std::lower_bound(
                        linesWithNotes_.cbegin(),
                        linesWithNotes_.cend(),
                        second,
                        [this](int lineIndex, double targetSecond) {
                            return lines_.at(lineIndex).lastNoteSecond < targetSecond;
                        })
                        - linesWithNotes_.cbegin());
        int left = right - 1;
        while (left >= 0 || right < linesWithNotes_.size()) {
            double leftMinDelta = std::numeric_limits<double>::infinity();
            double rightMinDelta = std::numeric_limits<double>::infinity();
            if (left >= 0) {
                const LineState& lineState = lines_.at(linesWithNotes_.at(left));
                if (second < lineState.firstNoteSecond) {
                    leftMinDelta = lineState.firstNoteSecond - second;
                } else if (second > lineState.lastNoteSecond) {
                    leftMinDelta = second - lineState.lastNoteSecond;
                } else {
                    leftMinDelta = 0.0;
                }
            }
            if (right < linesWithNotes_.size()) {
                const LineState& lineState = lines_.at(linesWithNotes_.at(right));
                if (second < lineState.firstNoteSecond) {
                    rightMinDelta = lineState.firstNoteSecond - second;
                } else if (second > lineState.lastNoteSecond) {
                    rightMinDelta = second - lineState.lastNoteSecond;
                } else {
                    rightMinDelta = 0.0;
                }
            }
            if (best.found && leftMinDelta > best.delta + kTimelineEpsilon && rightMinDelta > best.delta + kTimelineEpsilon) {
                break;
            }

            if (rightMinDelta <= leftMinDelta) {
                if (right < linesWithNotes_.size()) {
                    inspectLine(lines_.at(linesWithNotes_.at(right)), laneFilter, &best);
                    ++right;
                } else {
                    inspectLine(lines_.at(linesWithNotes_.at(left)), laneFilter, &best);
                    --left;
                }
            } else {
                inspectLine(lines_.at(linesWithNotes_.at(left)), laneFilter, &best);
                --left;
            }
        }
        return best;
    };

    Candidate best = (lane >= 1) ? findCandidate(lane) : Candidate();
    if (!best.found) {
        best = findCandidate(-1);
    }
    if (!best.found) {
        return false;
    }

    if (line != nullptr) {
        *line = best.line;
    }
    if (col != nullptr) {
        *col = best.col;
    }
    if (noteSecond != nullptr) {
        *noteSecond = best.second;
    }
    return true;
}

bool TimelineQuickModel::resolvePreviewFollowCursor(
    double second,
    int* line,
    int* col,
    double* noteSecond) const
{
    if (resolvePreviousCursorAnchorForSecond(second, line, col, noteSecond)) {
        return true;
    }

    if (line != nullptr) {
        *line = 1;
    }
    if (col != nullptr) {
        *col = 1;
    }
    if (noteSecond != nullptr) {
        *noteSecond = 0.0;
    }
    return false;
}

bool TimelineQuickModel::rebuildFromLineTexts(
    const QVector<QString>& lines,
    double firstSeconds,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    clear();
    if (lines.isEmpty()) {
        return true;
    }

    lines_.reserve(lines.size());
    ParseState state;
    state.second = firstSeconds;
    state.bpm = kDefaultBpm;
    state.beats = kDefaultBeats;
    state.subdivisionIndex = 0;
    applyInitialTimingMetadata(&state.meterNumerator, &state.meterDenominator, timingMetadata);
    state.currentMeasureStartSecond = firstSeconds;
    int startPosition = 0;
    for (int index = 0; index < lines.size(); ++index) {
        LineState line;
        line.lineId = allocateLineId();
        line.lineNumber = index + 1;
        line.startPosition = startPosition;
        line.text = lines.at(index);
        parseLine(&line, state);
        state = line.endState;
        startPosition += line.text.size() + 1;
        lines_.append(line);
    }

    rebuildSlideDerivedFlags();
    rebuildAnchorLineIndices();
    rebuildSnapshotDuration();
    return true;
}

QVector<QString> TimelineQuickModel::collectDocumentLines(const QTextDocument* document) const
{
    if (document == nullptr) {
        return {QString()};
    }
    return collectDocumentLines(document, 0, qMax(0, document->blockCount() - 1));
}

QVector<QString> TimelineQuickModel::collectDocumentLines(
    const QTextDocument* document,
    int startLineIndex,
    int endLineIndex) const
{
    QVector<QString> lines;
    if (document == nullptr) {
        lines.append(QString());
        return lines;
    }

    const int boundedStart = qMax(0, startLineIndex);
    const int boundedEnd = qMax(boundedStart, endLineIndex);
    QTextBlock block = document->findBlockByNumber(boundedStart);
    int currentLine = boundedStart;
    while (block.isValid() && currentLine <= boundedEnd) {
        lines.append(lineTextForBlock(block));
        block = block.next();
        ++currentLine;
    }
    if (lines.isEmpty()) {
        lines.append(QString());
    }
    return lines;
}

int TimelineQuickModel::lineIndexForDocumentPosition(const QTextDocument* document, int position) const
{
    if (document == nullptr) {
        return 0;
    }
    const int boundedPosition = qBound(0, position, document->characterCount());
    const QTextBlock block = document->findBlock(boundedPosition);
    return block.isValid() ? qMax(0, block.blockNumber()) : qMax(0, document->blockCount() - 1);
}

int TimelineQuickModel::lineIndexForStoredPosition(int position) const
{
    if (lines_.isEmpty()) {
        return 0;
    }
    const int boundedPosition = qMax(0, position);
    auto it = std::upper_bound(
        lines_.cbegin(),
        lines_.cend(),
        boundedPosition,
        [](int storedPosition, const LineState& lineState) { return storedPosition < lineState.startPosition; });
    if (it == lines_.cbegin()) {
        return 0;
    }
    return static_cast<int>(std::distance(lines_.cbegin(), std::prev(it)));
}

void TimelineQuickModel::shiftLineTiming(LineState* lineState, double deltaSeconds) const
{
    if (lineState == nullptr || qAbs(deltaSeconds) <= kTimelineEpsilon) {
        return;
    }
    lineState->startState.second += deltaSeconds;
    lineState->endState.second += deltaSeconds;
    lineState->startState.currentMeasureStartSecond += deltaSeconds;
    lineState->endState.currentMeasureStartSecond += deltaSeconds;
    lineState->render.startSecond += deltaSeconds;
    lineState->render.endSecond += deltaSeconds;
    if (lineState->hasNotes) {
        lineState->firstNoteSecond += deltaSeconds;
        lineState->lastNoteSecond += deltaSeconds;
    }
}

void TimelineQuickModel::rebuildSnapshotDuration()
{
    snapshot_.lines.clear();
    snapshot_.measureLineSeconds.clear();
    snapshot_.noteVisualEndPrefixMaxWithSlideTracks.clear();
    snapshot_.noteVisualEndPrefixMaxWithoutSlideTracks.clear();
    snapshot_.lines.reserve(lines_.size());
    snapshot_.noteVisualEndPrefixMaxWithSlideTracks.reserve(lines_.size());
    snapshot_.noteVisualEndPrefixMaxWithoutSlideTracks.reserve(lines_.size());

    double minSecond = 0.0;
    double maxSecond = 0.0;
    double noteVisualEndPrefixWithSlideTracks = -std::numeric_limits<double>::infinity();
    double noteVisualEndPrefixWithoutSlideTracks = -std::numeric_limits<double>::infinity();
    bool hasData = false;
    for (const LineState& lineState : lines_) {
        snapshot_.lines.append(lineState.render);
        for (double secondOffset : lineState.render.measureLineSecondOffsets) {
            appendDistinctSecond(&snapshot_.measureLineSeconds, lineState.render.startSecond + secondOffset);
        }
        minSecond = hasData ? qMin(minSecond, lineState.render.startSecond) : lineState.render.startSecond;
        maxSecond = hasData ? qMax(maxSecond, lineState.render.endSecond) : lineState.render.endSecond;
        noteVisualEndPrefixWithSlideTracks = qMax(
            noteVisualEndPrefixWithSlideTracks,
            timelineRenderLineVisualEndSecond(lineState.render, true)
        );
        noteVisualEndPrefixWithoutSlideTracks = qMax(
            noteVisualEndPrefixWithoutSlideTracks,
            timelineRenderLineVisualEndSecond(lineState.render, false)
        );
        snapshot_.noteVisualEndPrefixMaxWithSlideTracks.append(noteVisualEndPrefixWithSlideTracks);
        snapshot_.noteVisualEndPrefixMaxWithoutSlideTracks.append(noteVisualEndPrefixWithoutSlideTracks);
        hasData = true;
    }

    snapshot_.durationSeconds = hasData ? qMax(0.0, maxSecond) : 0.0;
    snapshot_.minimumSecond = hasData ? minSecond : -0.5;
    snapshot_.maximumSecond = hasData ? qMax(maxSecond, snapshot_.durationSeconds) : 1.0;
}

void TimelineQuickModel::resequenceLineMetadata(int startIndex)
{
    int nextStartPosition = 0;
    if (startIndex > 0 && startIndex <= lines_.size()) {
        const LineState& previousLine = lines_.at(startIndex - 1);
        nextStartPosition = previousLine.startPosition + previousLine.text.size() + 1;
    }

    for (int index = startIndex; index < lines_.size(); ++index) {
        LineState& line = lines_[index];
        line.lineNumber = index + 1;
        line.startPosition = nextStartPosition;
        line.render.lineNumber = line.lineNumber;
        line.render.startPosition = line.startPosition;
        nextStartPosition += line.text.size() + 1;
    }
}

void TimelineQuickModel::rebuildAnchorLineIndices()
{
    everyCommaAnchorsBySecond_.clear();
    linesWithEveryComma_.clear();
    linesWithNotes_.clear();
    linesWithEveryComma_.reserve(lines_.size());
    linesWithNotes_.reserve(lines_.size());

    int totalEveryCommaCount = 0;
    for (const LineState& line : lines_) {
        totalEveryCommaCount += line.cursorCache.everyComma.size();
    }
    everyCommaAnchorsBySecond_.reserve(totalEveryCommaCount);

    for (int index = 0; index < lines_.size(); ++index) {
        const LineState& line = lines_.at(index);
        if (!line.cursorCache.everyComma.isEmpty()) {
            linesWithEveryComma_.append(index);
        }
        for (const TimelineCursorAnchor& anchor : line.cursorCache.everyComma) {
            AbsoluteCursorAnchor absoluteAnchor;
            absoluteAnchor.lineNumber = line.lineNumber;
            absoluteAnchor.sourceCol = anchor.sourceCol;
            absoluteAnchor.second = qMax(0.0, timelineRenderAbsoluteSecond(line.render, anchor.secondOffset));
            everyCommaAnchorsBySecond_.append(absoluteAnchor);
        }
        if (line.hasNotes) {
            linesWithNotes_.append(index);
        }
    }

    std::sort(
        everyCommaAnchorsBySecond_.begin(),
        everyCommaAnchorsBySecond_.end(),
        [](const AbsoluteCursorAnchor& left, const AbsoluteCursorAnchor& right) {
            if (qAbs(left.second - right.second) > kTimelineEpsilon) {
                return left.second < right.second;
            }
            if (left.lineNumber != right.lineNumber) {
                return left.lineNumber > right.lineNumber;
            }
            return left.sourceCol > right.sourceCol;
        });
}

bool TimelineQuickModel::resolvePreviousCursorAnchorForTextPosition(
    int lineNumber,
    int sourceCol,
    int* line,
    int* col,
    double* second) const
{
    if (!lines_.isEmpty()) {
        const int targetLineIndex = qBound(0, lineNumber - 1, lines_.size() - 1);
        for (int lineIndex = targetLineIndex; lineIndex >= 0; --lineIndex) {
            const LineState& lineState = lines_.at(lineIndex);
            if (lineState.cursorCache.everyComma.isEmpty()) {
                continue;
            }

            const QVector<TimelineCursorAnchor>& anchors = lineState.cursorCache.everyComma;
            const TimelineCursorAnchor* resolvedAnchor = nullptr;
            if (lineIndex == targetLineIndex) {
                const auto it = std::upper_bound(
                    anchors.cbegin(),
                    anchors.cend(),
                    sourceCol,
                    [](int value, const TimelineCursorAnchor& anchor) { return value < anchor.sourceCol; });
                if (it != anchors.cbegin()) {
                    resolvedAnchor = &(*std::prev(it));
                }
            } else {
                resolvedAnchor = &anchors.constLast();
            }

            if (resolvedAnchor == nullptr) {
                continue;
            }
            if (line != nullptr) {
                *line = lineState.lineNumber;
            }
            if (col != nullptr) {
                *col = resolvedAnchor->sourceCol;
            }
            if (second != nullptr) {
                *second = qMax(0.0, timelineRenderAbsoluteSecond(lineState.render, resolvedAnchor->secondOffset));
            }
            return true;
        }
    }

    if (line != nullptr) {
        *line = 1;
    }
    if (col != nullptr) {
        *col = 1;
    }
    if (second != nullptr) {
        *second = 0.0;
    }
    return false;
}

bool TimelineQuickModel::resolvePreviousCursorAnchorForSecond(
    double second,
    int* line,
    int* col,
    double* anchorSecond) const
{
    if (everyCommaAnchorsBySecond_.isEmpty()) {
        if (line != nullptr) {
            *line = 1;
        }
        if (col != nullptr) {
            *col = 1;
        }
        if (anchorSecond != nullptr) {
            *anchorSecond = 0.0;
        }
        return false;
    }

    const double targetSecond = qMax(0.0, second);
    const auto endIt = std::upper_bound(
        everyCommaAnchorsBySecond_.cbegin(),
        everyCommaAnchorsBySecond_.cend(),
        targetSecond + kTimelineEpsilon,
        [](double target, const AbsoluteCursorAnchor& anchor) {
            return target < anchor.second;
        });
    if (endIt == everyCommaAnchorsBySecond_.cbegin()) {
        if (line != nullptr) {
            *line = 1;
        }
        if (col != nullptr) {
            *col = 1;
        }
        if (anchorSecond != nullptr) {
            *anchorSecond = 0.0;
        }
        return false;
    }

    const AbsoluteCursorAnchor& bestAnchor = *std::prev(endIt);
    if (line != nullptr) {
        *line = bestAnchor.lineNumber;
    }
    if (col != nullptr) {
        *col = bestAnchor.sourceCol;
    }
    if (anchorSecond != nullptr) {
        *anchorSecond = bestAnchor.second;
    }
    return true;
}

int TimelineQuickModel::resolveRecentPastLineIndex(double second) const
{
    if (lines_.isEmpty()) {
        return -1;
    }

    const double targetSecond = qMax(0.0, second);
    const auto it = std::upper_bound(
        lines_.cbegin(),
        lines_.cend(),
        targetSecond + kTimelineEpsilon,
        [](double target, const LineState& lineState) {
            return target < qMax(0.0, lineState.render.startSecond);
        });
    if (it == lines_.cbegin()) {
        return -1;
    }
    return static_cast<int>(std::distance(lines_.cbegin(), std::prev(it)));
}

bool TimelineQuickModel::parseLine(LineState* lineState, const ParseState& startState)
{
    if (lineState == nullptr) {
        return false;
    }

    lineState->startState = startState;
    lineState->endState = startState;
    lineState->render.lineId = lineState->lineId;
    lineState->render.lineNumber = lineState->lineNumber;
    lineState->render.startPosition = lineState->startPosition;
    lineState->render.startSecond = startState.second;
    lineState->render.endSecond = startState.second;
    lineState->render.measureLineSecondOffsets.clear();
    lineState->render.beats.clear();
    lineState->render.notes.clear();
    lineState->cursorCache.everyComma.clear();
    lineState->hasRawZeroAnchor = false;
    lineState->hasNotes = false;
    lineState->firstNoteSecond = 0.0;
    lineState->lastNoteSecond = 0.0;

    ParseState state = startState;
    double lineMaxSecond = startState.second;
    QString token;
    int tokenColumn = 1;
    QVector<int> currentGroup;

    if (lineState->text.trimmed().compare(QStringLiteral("E"), Qt::CaseInsensitive) == 0) {
        return true;
    }

    const auto flushToken = [&]() {
        if (token.isEmpty()) {
            return;
        }
        parseNoteToken(lineState, &state, token, lineState->lineNumber, tokenColumn, &currentGroup);
        token.clear();
    };
    const auto appendMeasureLine = [&](double absoluteSecond) {
        appendDistinctSecond(
            &lineState->render.measureLineSecondOffsets,
            absoluteSecond - lineState->render.startSecond);
    };
    const auto advanceMeasureLinesTo = [&](double targetSecond) {
        const double measureDuration = measureDurationSeconds(
            state.bpm,
            state.meterNumerator,
            state.meterDenominator);
        while (state.currentMeasureStartSecond + measureDuration <= targetSecond + kTimelineEpsilon) {
            state.currentMeasureStartSecond += measureDuration;
            appendMeasureLine(state.currentMeasureStartSecond);
        }
    };

    if (lineState->lineNumber == 1) {
        appendMeasureLine(state.currentMeasureStartSecond);
    }

    for (int index = 0; index < lineState->text.size(); ++index) {
        const QChar ch = lineState->text.at(index);
        if (ch == QLatin1Char('|') && index + 1 < lineState->text.size() && lineState->text.at(index + 1) == QLatin1Char('|')) {
            flushToken();
            int numerator = 0;
            int denominator = 0;
            if (miacode::simai::parseInlineTimeSignatureComment(
                    lineState->text,
                    index,
                    &numerator,
                    &denominator,
                    nullptr)) {
                state.meterNumerator = numerator;
                state.meterDenominator = denominator;
                state.currentMeasureStartSecond = state.second;
                appendMeasureLine(state.currentMeasureStartSecond);
            }
            break;
        }
        if (ch.isSpace()) {
            flushToken();
            continue;
        }
        if (ch == QLatin1Char('(')) {
            flushToken();
            const int close = lineState->text.indexOf(QLatin1Char(')'), index + 1);
            if (close < 0) {
                break;
            }
            bool bpmOk = false;
            const double bpm = lineState->text.mid(index + 1, close - index - 1).trimmed().toDouble(&bpmOk);
            if (bpmOk && bpm > 0.0) {
                if (qAbs(state.bpm - bpm) > kTimelineEpsilon) {
                    state.currentMeasureStartSecond = state.second;
                    appendMeasureLine(state.currentMeasureStartSecond);
                }
                state.bpm = bpm;
            }
            index = close;
            continue;
        }
        if (ch == QLatin1Char('{')) {
            flushToken();
            const int close = lineState->text.indexOf(QLatin1Char('}'), index + 1);
            if (close < 0) {
                break;
            }
            bool beatsOk = false;
            const int beats = lineState->text.mid(index + 1, close - index - 1).trimmed().toInt(&beatsOk);
            if (beatsOk && beats > 0) {
                state.beats = beats;
                state.subdivisionIndex = 0;
            }
            index = close;
            continue;
        }
        if (ch == QLatin1Char('H') && lineState->text.mid(index, 3) == QStringLiteral("HS*")) {
            flushToken();
            const int close = lineState->text.indexOf(QLatin1Char('>'), index + 3);
            if (close < 0) {
                break;
            }
            index = close;
            continue;
        }
        if (ch == QLatin1Char('/')) {
            flushToken();
            continue;
        }
        if (ch == QLatin1Char('`')) {
            flushToken();
            finalizeEachGroup(lineState, currentGroup);
            currentGroup.clear();
            continue;
        }
        if (ch == QLatin1Char(',')) {
            flushToken();
            finalizeEachGroup(lineState, currentGroup);
            currentGroup.clear();

            TimelineRenderBeat beat;
            beat.secondOffset = state.second - lineState->render.startSecond;
            beat.sourceCol = index + 1;
            beat.subdivisionBeats = qMax(1, state.beats);
            beat.subdivisionIndex = qBound(0, beat.subdivisionBeats - 1, state.subdivisionIndex);
            beat.major = false;
            lineState->render.beats.append(beat);
            lineState->hasRawZeroAnchor = lineState->hasRawZeroAnchor || qAbs(beat.secondOffset) <= kTimelineEpsilon;
            lineMaxSecond = qMax(lineMaxSecond, state.second);
            state.subdivisionIndex = (state.subdivisionIndex + 1) % qMax(1, state.beats);
            state.second += noteStepSeconds(state.bpm, state.beats);
            advanceMeasureLinesTo(state.second);
            continue;
        }
        if (token.isEmpty()
            && (ch == QLatin1Char('E') || ch == QLatin1Char('e'))
            && lineState->text.mid(index).trimmed().compare(QStringLiteral("E"), Qt::CaseInsensitive) == 0) {
            flushToken();
            break;
        }
        if (token.isEmpty()) {
            tokenColumn = index + 1;
        }
        token.append(ch);
    }

    flushToken();
    finalizeEachGroup(lineState, currentGroup);

    for (const TimelineRenderBeat& beat : lineState->render.beats) {
        TimelineCursorAnchor anchor;
        anchor.sourceCol = beat.sourceCol;
        anchor.lane = -1;
        anchor.secondOffset = beat.secondOffset;
        lineState->cursorCache.everyComma.append(anchor);
    }
    std::sort(
        lineState->cursorCache.everyComma.begin(),
        lineState->cursorCache.everyComma.end(),
        [](const TimelineCursorAnchor& left, const TimelineCursorAnchor& right) {
            if (left.sourceCol != right.sourceCol) {
                return left.sourceCol < right.sourceCol;
            }
            return left.secondOffset < right.secondOffset;
        });

    lineState->endState = state;
    lineState->render.endSecond = qMax(lineMaxSecond, state.second);
    return true;
}

bool TimelineQuickModel::parseNoteToken(
    LineState* lineState,
    ParseState* state,
    const QString& token,
    int lineNumber,
    int column,
    QVector<int>* groupIndices) const
{
    if (lineState == nullptr || state == nullptr || token.isEmpty()) {
        return false;
    }

    const double lineBaseSecond = lineState->render.startSecond;
    const auto appendNote = [lineState, groupIndices, lineBaseSecond](const TimelineRenderNote& note) {
        const int noteIndex = lineState->render.notes.size();
        lineState->render.notes.append(note);
        if (groupIndices != nullptr) {
            groupIndices->append(noteIndex);
        }
        const double absoluteSecond = lineBaseSecond + note.secondOffset;
        lineState->hasNotes = true;
        if (noteIndex == 0) {
            lineState->firstNoteSecond = absoluteSecond;
            lineState->lastNoteSecond = absoluteSecond;
        } else {
            lineState->firstNoteSecond = qMin(lineState->firstNoteSecond, absoluteSecond);
            lineState->lastNoteSecond = qMax(lineState->lastNoteSecond, absoluteSecond);
        }
        lineState->render.endSecond = qMax(lineState->render.endSecond, absoluteSecond);
        if (note.endSecondOffset >= 0.0) {
            lineState->render.endSecond = qMax(lineState->render.endSecond, lineBaseSecond + note.endSecondOffset);
        }
    };

    bool simpleDigitCluster = token.size() > 1;
    for (QChar ch : token) {
        if (!isDigitLane(ch)) {
            simpleDigitCluster = false;
            break;
        }
    }
    if (simpleDigitCluster) {
        for (int index = 0; index < token.size(); ++index) {
            parseNoteToken(lineState, state, token.mid(index, 1), lineNumber, column + index, groupIndices);
        }
        return true;
    }

    const double currentSecond = state->second;
    if (isTouchPrefix(token)) {
        QString normalizedToken = token;
        if (normalizedToken.size() >= 2
            && normalizedToken.at(0).toUpper() == QLatin1Char('C')
            && (normalizedToken.at(1) == QLatin1Char('1') || normalizedToken.at(1) == QLatin1Char('2'))) {
            normalizedToken.remove(1, 1);
        }

        QString durationSignature;
        bool hasHold = false;
        bool hasFirework = false;
        bool hasBreak = false;
        if (!parseTouchSuffix(normalizedToken, &durationSignature, &hasHold, &hasFirework, &hasBreak)) {
            return false;
        }

        TimelineRenderNote note;
        note.secondOffset = currentSecond - lineBaseSecond;
        note.sourceCol = qMax(1, column);
        note.lane = 9;
        note.endLane = 9;
        note.kind = hasHold ? TimelineRenderNoteKind::TouchHold : TimelineRenderNoteKind::Touch;
        note.flags = 0u;
        if (hasBreak) {
            note.flags |= TimelineRenderFlagIsBreak;
        }
        if (hasFirework) {
            note.flags |= TimelineRenderFlagIsFirework;
        }
        if (hasHold) {
            bool ok = false;
            const double durationSecond = parseHoldDurationSignature(durationSignature, state->bpm, &ok);
            if (!ok) {
                return false;
            }
            note.endSecondOffset = note.secondOffset + qMax(0.0, durationSecond);
        }
        appendNote(note);
        lineState->hasRawZeroAnchor = lineState->hasRawZeroAnchor || qAbs(note.secondOffset) <= kTimelineEpsilon;
        return true;
    }

    if (!isDigitLane(token.at(0))) {
        return false;
    }

    const bool slideLike = token.contains(QLatin1Char('['))
        && (token.contains(QLatin1Char('-')) || token.contains(QLatin1Char('^')) || token.contains(QLatin1Char('v'))
            || token.contains(QLatin1Char('<')) || token.contains(QLatin1Char('>')) || token.contains(QLatin1Char('V'))
            || token.contains(QLatin1Char('p')) || token.contains(QLatin1Char('q')) || token.contains(QLatin1Char('s'))
            || token.contains(QLatin1Char('z')) || token.contains(QLatin1Char('w')));
    if (slideLike) {
        int modifierCount = 0;
        SlideHeadModifierState modifierState;
        if (!parseSlideHeadModifierPrefix(token, &modifierCount, &modifierState)) {
            return false;
        }

        QString core;
        core.reserve(token.size());
        core.append(token.at(0));
        core.append(token.mid(1 + modifierCount));
        if (core.contains(QLatin1Char('*'))) {
            const QString prefix = token.left(1) + modifierState.rawModifiers;
            const QChar startLane = token.at(0);
            const QStringList branches = core.split(QLatin1Char('*'), Qt::KeepEmptyParts);
            for (const QString& branchRaw : branches) {
                QString branch = branchRaw;
                if (branch.isEmpty()) {
                    continue;
                }
                if (!isDigitLane(branch.at(0))) {
                    branch.prepend(startLane);
                }
                const QString branchToken = prefix + branch.mid(1);
                if (!parseNoteToken(lineState, state, branchToken, lineNumber, column, groupIndices)) {
                    return false;
                }
            }
            return true;
        }
        if (slideCoreHasDisallowedModifiers(core)) {
            return false;
        }
        QString sanitizedCore;
        sanitizedCore.reserve(core.size());
        bool trackBreak = false;
        for (QChar ch : core) {
            if (ch == QLatin1Char('b')) {
                trackBreak = true;
                continue;
            }
            sanitizedCore.append(ch);
        }

        double waitSecond = 0.0;
        double durationSecond = 0.0;
        if (!extractSlideTiming(sanitizedCore, state->bpm, &waitSecond, &durationSecond)) {
            return false;
        }

        TimelineRenderNote note;
        note.secondOffset = currentSecond - lineBaseSecond;
        note.sourceCol = qMax(1, column);
        note.lane = token.at(0).digitValue();
        note.endLane = inferSlideEndLane(sanitizedCore, note.lane);
        note.kind = sanitizedCore.contains(QLatin1Char('w'), Qt::CaseInsensitive)
            ? TimelineRenderNoteKind::Wifi
            : TimelineRenderNoteKind::Slide;
        note.slideTraceSecondOffset = note.secondOffset + qMax(0.0, waitSecond);
        note.endSecondOffset = note.slideTraceSecondOffset + qMax(0.0, durationSecond);
        note.flags = 0u;
        if (modifierState.headlessMode == SlideHeadlessMode::None) {
            note.flags |= TimelineRenderFlagHasHeadStar;
        }
        if (modifierState.headBreak) {
            note.flags |= TimelineRenderFlagHeadBreak | TimelineRenderFlagIsBreak;
        }
        if (modifierState.headEx) {
            note.flags |= TimelineRenderFlagHeadEx;
        }
        if (modifierState.slideHeadUsesTapMaterial) {
            note.flags |= TimelineRenderFlagSlideHeadUsesTapMaterial;
        }
        if (modifierState.headlessMode == SlideHeadlessMode::Immediate) {
            note.flags |= TimelineRenderFlagHeadlessImmediate;
        }
        if (trackBreak) {
            note.flags |= TimelineRenderFlagTrackBreak | TimelineRenderFlagIsBreak;
        }
        appendNote(note);
        lineState->hasRawZeroAnchor = lineState->hasRawZeroAnchor || qAbs(note.secondOffset) <= kTimelineEpsilon;
        return true;
    }

    TimelineRenderNote note;
    note.secondOffset = currentSecond - lineBaseSecond;
    note.sourceCol = qMax(1, column);
    note.lane = token.at(0).digitValue();
    note.endLane = note.lane;
    note.kind = TimelineRenderNoteKind::Tap;
    note.flags = 0u;

    const QString suffix = token.mid(1);
    const int openBracket = suffix.indexOf(QLatin1Char('['));
    const int closeBracket = suffix.indexOf(QLatin1Char(']'));
    const bool hasBracket = openBracket >= 0 && closeBracket > openBracket;
    const QString prefixModifiers = hasBracket ? suffix.left(openBracket) : suffix;
    const QString suffixModifiers = hasBracket ? suffix.mid(closeBracket + 1) : QString();
    if (suffixModifiers.contains(QLatin1Char('h'), Qt::CaseInsensitive)) {
        return false;
    }

    TapModifierState modifierState;
    if (!parseTapModifierSequence(prefixModifiers, suffixModifiers, &modifierState)) {
        return false;
    }

    const bool hasHold = modifierState.hasHold;
    if (modifierState.hasBreak) {
        note.flags |= TimelineRenderFlagIsBreak;
    }
    if (modifierState.hasEx) {
        note.flags |= TimelineRenderFlagIsEx;
    }
    if (modifierState.tapUsesStarMaterial) {
        note.flags |= TimelineRenderFlagTapUsesStarMaterial;
    }
    if (modifierState.tapStarDouble) {
        note.flags |= TimelineRenderFlagTapStarDouble;
    }

    if (hasHold) {
        note.kind = TimelineRenderNoteKind::Hold;
        double durationSecond = 0.0;
        bool ok = true;
        if (hasBracket) {
            durationSecond = parseHoldDurationSignature(tokenInsideBrackets(token), state->bpm, &ok);
        }
        if (!ok) {
            return false;
        }
        note.endSecondOffset = note.secondOffset + qMax(0.0, durationSecond);
    }

    appendNote(note);
    lineState->hasRawZeroAnchor = lineState->hasRawZeroAnchor || qAbs(note.secondOffset) <= kTimelineEpsilon;
    return true;
}

void TimelineQuickModel::rebuildSlideDerivedFlags()
{
    for (LineState& lineState : lines_) {
        for (TimelineRenderNote& note : lineState.render.notes) {
            note.flags &= ~static_cast<quint32>(TimelineRenderFlagSlideEach);
        }
    }

    QHash<int, QHash<qint64, QVector<QPair<int, int>>>> slideTraceGroups;
    for (int lineIndex = 0; lineIndex < lines_.size(); ++lineIndex) {
        const LineState& lineState = lines_.at(lineIndex);
        for (int noteIndex = 0; noteIndex < lineState.render.notes.size(); ++noteIndex) {
            const TimelineRenderNote& note = lineState.render.notes.at(noteIndex);
            if ((note.kind != TimelineRenderNoteKind::Slide && note.kind != TimelineRenderNoteKind::Wifi)
                || note.slideTraceSecondOffset < 0.0
                || note.eachGroupId < 0) {
                continue;
            }
            const qint64 traceKey = qRound64(
                timelineRenderAbsoluteSecond(lineState.render, note.slideTraceSecondOffset) * 1000000.0
            );
            slideTraceGroups[note.eachGroupId][traceKey].append(qMakePair(lineIndex, noteIndex));
        }
    }

    for (auto groupIt = slideTraceGroups.begin(); groupIt != slideTraceGroups.end(); ++groupIt) {
        auto& traceGroups = groupIt.value();
        for (auto traceIt = traceGroups.begin(); traceIt != traceGroups.end(); ++traceIt) {
            const QVector<QPair<int, int>>& group = traceIt.value();
            if (group.size() < 2) {
                continue;
            }
            for (const QPair<int, int>& ref : group) {
                lines_[ref.first].render.notes[ref.second].flags |= static_cast<quint32>(TimelineRenderFlagSlideEach);
            }
        }
    }
}

void TimelineQuickModel::finalizeEachGroup(LineState* lineState, const QVector<int>& groupIndices)
{
    if (lineState == nullptr || groupIndices.isEmpty()) {
        return;
    }

    const int eachGroupId = nextEachGroupId_++;

    QVector<int> touchIndices;
    QVector<int> tapIndices;
    QVector<int> holdIndices;
    QVector<int> touchHoldIndices;
    QVector<int> slideIndices;
    QSet<int> headStarLanes;
    int headlessSlideCount = 0;

    for (int index : groupIndices) {
        if (index < 0 || index >= lineState->render.notes.size()) {
            continue;
        }
        lineState->render.notes[index].eachGroupId = eachGroupId;
        const TimelineRenderNote& note = lineState->render.notes.at(index);
        switch (note.kind) {
        case TimelineRenderNoteKind::Touch:
            touchIndices.append(index);
            break;
        case TimelineRenderNoteKind::Tap:
            tapIndices.append(index);
            break;
        case TimelineRenderNoteKind::Hold:
            holdIndices.append(index);
            break;
        case TimelineRenderNoteKind::TouchHold:
            touchHoldIndices.append(index);
            break;
        case TimelineRenderNoteKind::Slide:
        case TimelineRenderNoteKind::Wifi:
            slideIndices.append(index);
            if (timelineRenderFlagSet(note, TimelineRenderFlagHasHeadStar)) {
                headStarLanes.insert(note.lane);
            } else {
                ++headlessSlideCount;
            }
            break;
        default:
            break;
        }
    }

    for (int i = 0; i < groupIndices.size(); ++i) {
        const int leftIndex = groupIndices.at(i);
        if (leftIndex < 0 || leftIndex >= lineState->render.notes.size()) {
            continue;
        }
        TimelineRenderNote& left = lineState->render.notes[leftIndex];
        if (left.kind != TimelineRenderNoteKind::Slide && left.kind != TimelineRenderNoteKind::Wifi) {
            continue;
        }
        for (int j = 0; j < groupIndices.size(); ++j) {
            if (i == j) {
                continue;
            }
            const int rightIndex = groupIndices.at(j);
            if (rightIndex < 0 || rightIndex >= lineState->render.notes.size()) {
                continue;
            }
            const TimelineRenderNote& right = lineState->render.notes.at(rightIndex);
            if ((right.kind == TimelineRenderNoteKind::Slide || right.kind == TimelineRenderNoteKind::Wifi)
                && right.lane == left.lane) {
                left.flags |= static_cast<quint32>(TimelineRenderFlagSameHeadSlide);
                break;
            }
        }
    }

    const int logicalUnitCount =
        tapIndices.size() + holdIndices.size() + touchHoldIndices.size() + headStarLanes.size() + headlessSlideCount;
    const int noteEachGroupCount = touchIndices.size() + logicalUnitCount;
    if (noteEachGroupCount < 2) {
        return;
    }

    const auto setFlagForIndices = [lineState](const QVector<int>& indices, TimelineRenderNoteFlag flag) {
        for (int index : indices) {
            if (index >= 0 && index < lineState->render.notes.size()) {
                lineState->render.notes[index].flags |= static_cast<quint32>(flag);
            }
        }
    };

    setFlagForIndices(tapIndices, TimelineRenderFlagIsEach);
    setFlagForIndices(holdIndices, TimelineRenderFlagIsEach);
    setFlagForIndices(touchHoldIndices, TimelineRenderFlagIsEach);
    setFlagForIndices(touchIndices, TimelineRenderFlagIsEach);
    setFlagForIndices(slideIndices, TimelineRenderFlagHeadEach);
}

int TimelineQuickModel::allocateLineId()
{
    return nextLineId_++;
}

bool TimelineQuickModel::parseStatesEqual(const ParseState& a, const ParseState& b)
{
    return qAbs(a.second - b.second) <= kTimelineEpsilon
        && qAbs(a.bpm - b.bpm) <= kTimelineEpsilon
        && a.beats == b.beats
        && a.subdivisionIndex == b.subdivisionIndex
        && a.meterNumerator == b.meterNumerator
        && a.meterDenominator == b.meterDenominator
        && qAbs(a.currentMeasureStartSecond - b.currentMeasureStartSecond) <= kTimelineEpsilon;
}
