#include "timeline/TimelineQuickModel.h"

#include <QTextBlock>
#include <QTextDocument>

#include <algorithm>
#include <limits>
#include <utility>

#include "timeline/TimelineQuickModelPrivate.h"

using namespace miacode::timeline::tqm_detail;

namespace {

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

bool isTerminalMarkerText(QString text)
{
    text = text.trimmed();
    return text.compare(QStringLiteral("E"), Qt::CaseInsensitive) == 0;
}

bool lineTailIsTerminalMarker(const QString& line, int startIndex)
{
    if (startIndex < 0 || startIndex >= line.size()) {
        return false;
    }
    QString tail = line.mid(startIndex);
    const int commentIndex = tail.indexOf(QStringLiteral("||"));
    if (commentIndex >= 0) {
        tail = tail.left(commentIndex);
    }
    return isTerminalMarkerText(tail);
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
    bool hasMine = false;
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

    if (prefixModifiers.contains(QLatin1Char('B'))
        || prefixModifiers.contains(QLatin1Char('X'))
        || prefixModifiers.contains(QLatin1Char('M'))
        || suffixModifiers.contains(QLatin1Char('B'))
        || suffixModifiers.contains(QLatin1Char('X'))
        || suffixModifiers.contains(QLatin1Char('M'))) {
        return false;
    }

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
            if (ch == QLatin1Char('m')) {
                if (state->hasMine) {
                    return false;
                }
                state->hasMine = true;
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
        if (ch == QLatin1Char('B') || ch == QLatin1Char('X')) {
            return false;
        }
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

    if (signature.isEmpty()) {
        return 0.0;
    }

    // Mirror the authoritative SimaiNativeParser hold-duration grammar so the Timeline
    // shows the same hold length the renderer uses:
    //   [beats:num]       beat fraction at the current BPM
    //   [#seconds]        absolute seconds
    //   [tempo#beats:num] beat fraction at a temporary BPM
    //   [tempo#seconds]   absolute seconds (fallback when the tail is not a fraction)
    const QStringList hashParts = signature.split(QLatin1Char('#'), Qt::KeepEmptyParts);
    if (hashParts.size() > 2) {
        return 0.0;
    }

    const auto parseBeatFraction = [](const QString& text, double useBpm, bool* fractionOk) -> double {
        *fractionOk = false;
        const int colon = text.indexOf(QLatin1Char(':'));
        if (colon < 0) {
            return 0.0;
        }
        bool beatsOk = false;
        bool numOk = false;
        const int beats = text.left(colon).toInt(&beatsOk);
        const int num = text.mid(colon + 1).toInt(&numOk);
        if (!beatsOk || !numOk || beats <= 0 || useBpm <= 0.0) {
            return 0.0;
        }
        *fractionOk = true;
        return 240.0 * static_cast<double>(num) / (useBpm * static_cast<double>(beats));
    };

    if (hashParts.size() == 2) {
        if (hashParts.at(0).isEmpty()) {
            bool secondsOk = false;
            const double seconds = hashParts.at(1).toDouble(&secondsOk);
            if (!secondsOk) {
                return 0.0;
            }
            *okOut = true;
            return qMax(0.0, seconds);
        }

        bool tempBpmOk = false;
        const double tempBpm = hashParts.at(0).toDouble(&tempBpmOk);
        if (!tempBpmOk) {
            return 0.0;
        }
        bool fractionOk = false;
        const double beatsDuration = parseBeatFraction(hashParts.at(1), tempBpm, &fractionOk);
        if (fractionOk) {
            *okOut = true;
            return beatsDuration;
        }
        bool secondsOk = false;
        const double seconds = hashParts.at(1).toDouble(&secondsOk);
        if (!secondsOk) {
            return 0.0;
        }
        *okOut = true;
        return qMax(0.0, seconds);
    }

    bool fractionOk = false;
    const double beatsDuration = parseBeatFraction(signature, bpm, &fractionOk);
    if (fractionOk) {
        *okOut = true;
        return beatsDuration;
    }
    return 0.0;
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
    bool* hasBreak,
    bool* hasMine)
{
    if (durationSignature == nullptr || hasHold == nullptr || hasFirework == nullptr || hasBreak == nullptr
        || hasMine == nullptr) {
        return false;
    }
    durationSignature->clear();
    *hasHold = false;
    *hasFirework = false;
    *hasBreak = false;
    *hasMine = false;

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

    const auto parseModifierPart = [hasHold, hasFirework, hasBreak, hasMine](const QString& modifiers) -> bool {
        for (QChar ch : modifiers) {
            if (ch == QLatin1Char('h')) {
                *hasHold = true;
            } else if (ch == QLatin1Char('f')) {
                *hasFirework = true;
            } else if (ch == QLatin1Char('b')) {
                *hasBreak = true;
            } else if (ch == QLatin1Char('m')) {
                *hasMine = true;
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

}  // namespace

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
    lineState->render.measureLineMeterNumerators.clear();
    lineState->render.measureLineMeterDenominators.clear();
    lineState->render.measureLineBeatStepSeconds.clear();
    lineState->render.beats.clear();
    lineState->render.notes.clear();
    lineState->cursorCache.segmentStarts.clear();
    lineState->cursorCache.followSelectionRanges.clear();
    lineState->cursorCache.followSelectionSpans.clear();
    lineState->isTerminalE = false;
    lineState->terminalSecond = -1.0;
    lineState->hasNotes = false;
    lineState->firstNoteSecond = 0.0;
    lineState->lastNoteSecond = 0.0;

    ParseState state = startState;
    double lineMaxSecond = startState.second;
    QString token;
    int tokenColumn = 1;
    QVector<int> currentGroup;

    if (lineTailIsTerminalMarker(lineState->text, 0)) {
        lineState->isTerminalE = true;
        lineState->terminalSecond = startState.second;
        return true;
    }

    TimelineCursorAnchor lineStartAnchor;
    lineStartAnchor.sourceCol = 1;
    lineStartAnchor.lane = -1;
    lineStartAnchor.secondOffset = 0.0;
    lineState->cursorCache.segmentStarts.append(lineStartAnchor);

    const auto flushToken = [&]() {
        if (token.isEmpty()) {
            return;
        }
        parseNoteToken(lineState, &state, token, lineState->lineNumber, tokenColumn, &currentGroup);
        token.clear();
    };
    const auto appendMeasureLine = [&](double absoluteSecond) {
        const double offset = absoluteSecond - lineState->render.startSecond;
        const int meterNumerator = qMax(1, state.meterNumerator);
        const int meterDenominator = qMax(1, state.meterDenominator);
        const double beatStep = noteStepSeconds(state.bpm, state.meterDenominator);
        QVector<double>& offsets = lineState->render.measureLineSecondOffsets;
        if (!offsets.isEmpty() && qAbs(offsets.constLast() - offset) <= kTimelineEpsilon) {
            // A meter/BPM restart (|| x/y or (bpm)) landing exactly on the
            // previous measure line: keep the single line, but let the NEW meter
            // govern the span AFTER it, so old and new subdivisions never mix.
            lineState->render.measureLineMeterNumerators.last() = meterNumerator;
            lineState->render.measureLineMeterDenominators.last() = meterDenominator;
            lineState->render.measureLineBeatStepSeconds.last() = beatStep;
            return;
        }
        offsets.append(offset);
        lineState->render.measureLineMeterNumerators.append(meterNumerator);
        lineState->render.measureLineMeterDenominators.append(meterDenominator);
        lineState->render.measureLineBeatStepSeconds.append(beatStep);
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

    if (!state.initialMeasureLineEmitted) {
        // Seed the chart-start measure line on the FIRST non-terminal line
        // (terminal `E` lines return early above without setting the flag), so a
        // leading `E` no longer drops the chart-start grid. Mirrors the strict
        // parser's `initializedMeasureLines`.
        appendMeasureLine(state.currentMeasureStartSecond);
        state.initialMeasureLineEmitted = true;
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
                // Any (bpm) directive restarts the measure phase, even when the
                // value is unchanged (变BPM 一律重启小节相位). Update the bpm BEFORE
                // emitting the measure line so its beat step reflects the NEW tempo
                // (the line governs the span after it).
                state.bpm = bpm;
                state.currentMeasureStartSecond = state.second;
                appendMeasureLine(state.currentMeasureStartSecond);
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
        if (ch == QLatin1Char('<') && lineState->text.mid(index, 4) == QStringLiteral("<HS*")) {
            flushToken();
            const int close = lineState->text.indexOf(QLatin1Char('>'), index + 4);
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
            lineMaxSecond = qMax(lineMaxSecond, state.second);
            state.subdivisionIndex = (state.subdivisionIndex + 1) % qMax(1, state.beats);
            state.second += noteStepSeconds(state.bpm, state.beats);
            advanceMeasureLinesTo(state.second);

            TimelineCursorAnchor anchor;
            anchor.sourceCol = index + 2;
            anchor.lane = -1;
            anchor.secondOffset = state.second - lineState->render.startSecond;
            lineState->cursorCache.segmentStarts.append(anchor);
            continue;
        }
        if (token.isEmpty()
            && (ch == QLatin1Char('E') || ch == QLatin1Char('e'))
            && lineTailIsTerminalMarker(lineState->text, index)) {
            flushToken();
            lineState->isTerminalE = true;
            lineState->terminalSecond = state.second;
            break;
        }
        if (token.isEmpty()) {
            tokenColumn = index + 1;
        }
        token.append(ch);
    }

    flushToken();
    finalizeEachGroup(lineState, currentGroup);

    std::sort(
        lineState->cursorCache.segmentStarts.begin(),
        lineState->cursorCache.segmentStarts.end(),
        [](const TimelineCursorAnchor& left, const TimelineCursorAnchor& right) {
            if (left.sourceCol != right.sourceCol) {
                return left.sourceCol < right.sourceCol;
            }
            return left.secondOffset < right.secondOffset;
        });
    rebuildFollowSelectionRanges(lineState);

    lineState->endState = state;
    // Preserve the hold/slide/touch-hold tail extent accumulated by appendNote()
    // instead of clobbering it with the bar cursor. Otherwise the snapshot duration
    // and horizontal scroll range drop long note tails that the slow parser path
    // (TimelineSlowRefresh::computeDurationSeconds) and the visible-range cull
    // (noteVisualEndPrefixMax) both retain, leaving the three out of sync.
    lineState->render.endSecond = qMax(lineState->render.endSecond, qMax(lineMaxSecond, state.second));
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
        bool hasMine = false;
        if (!parseTouchSuffix(normalizedToken, &durationSignature, &hasHold, &hasFirework, &hasBreak, &hasMine)) {
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
        if (hasMine) {
            note.flags |= TimelineRenderFlagIsMine;
        }
        if (hasHold) {
            bool ok = true;
            double durationSecond = 0.0;
            if (!durationSignature.isEmpty()) {
                durationSecond = parseHoldDurationSignature(durationSignature, state->bpm, &ok);
                if (!ok) {
                    return false;
                }
            }
            note.endSecondOffset = note.secondOffset + qMax(0.0, durationSecond);
        }
        appendNote(note);
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
        if (core.contains(QLatin1Char('M'))) {
            return false;
        }
        QString sanitizedCore;
        sanitizedCore.reserve(core.size());
        bool trackBreak = false;
        bool trackMine = false;
        for (QChar ch : core) {
            if (ch == QLatin1Char('b')) {
                trackBreak = true;
                continue;
            }
            if (ch == QLatin1Char('m')) {
                trackMine = true;
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
        if (trackMine) {
            note.flags |= TimelineRenderFlagTrackMine | TimelineRenderFlagIsMine;
        }
        appendNote(note);
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
    if (modifierState.hasMine) {
        note.flags |= TimelineRenderFlagIsMine;
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
    return true;
}

void TimelineQuickModel::rebuildSlideDerivedFlags()
{
    for (LineState& lineState : lines_) {
        for (TimelineRenderNote& note : lineState.render.notes) {
            note.flags &= ~static_cast<quint32>(TimelineRenderFlagSlideEach);
        }
    }

    QHash<int, QVector<QPair<int, int>>> slideEachGroups;
    QHash<int, QHash<qint64, QVector<QPair<int, int>>>> slideTraceGroups;
    for (int lineIndex = 0; lineIndex < lines_.size(); ++lineIndex) {
        const LineState& lineState = lines_.at(lineIndex);
        for (int noteIndex = 0; noteIndex < lineState.render.notes.size(); ++noteIndex) {
            const TimelineRenderNote& note = lineState.render.notes.at(noteIndex);
            if ((note.kind != TimelineRenderNoteKind::Slide && note.kind != TimelineRenderNoteKind::Wifi)
                || note.eachGroupId < 0) {
                continue;
            }
            const QPair<int, int> ref = qMakePair(lineIndex, noteIndex);
            slideEachGroups[note.eachGroupId].append(ref);
            if (note.slideTraceSecondOffset < 0.0) {
                continue;
            }
            const qint64 traceKey = qRound64(
                timelineRenderAbsoluteSecond(lineState.render, note.slideTraceSecondOffset) * 1000000.0
            );
            slideTraceGroups[note.eachGroupId][traceKey].append(ref);
        }
    }

    for (auto groupIt = slideEachGroups.begin(); groupIt != slideEachGroups.end(); ++groupIt) {
        const QVector<QPair<int, int>>& group = groupIt.value();
        if (group.size() < 2) {
            continue;
        }
        for (const QPair<int, int>& ref : group) {
            lines_[ref.first].render.notes[ref.second].flags |= static_cast<quint32>(TimelineRenderFlagSlideEach);
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
        const TimelineRenderNote& note = lineState->render.notes.at(index);
        // Mines never participate in each-grouping (parity with the native
        // parser's finalizeEachGroup) — no eachGroupId, no IsEach flag.
        if (timelineRenderFlagSet(note, TimelineRenderFlagIsMine)
            || timelineRenderFlagSet(note, TimelineRenderFlagTrackMine)) {
            continue;
        }
        lineState->render.notes[index].eachGroupId = eachGroupId;
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
        if (timelineRenderFlagSet(left, TimelineRenderFlagTrackMine)) {
            continue;  // mines never form a same-head (double-star) each pair
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
                && !timelineRenderFlagSet(right, TimelineRenderFlagTrackMine)
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
    if (slideIndices.size() >= 2) {
        setFlagForIndices(slideIndices, TimelineRenderFlagSlideEach);
    }
}
