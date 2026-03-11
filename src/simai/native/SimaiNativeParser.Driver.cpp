void parseToken(ParseState* state, const QString& token, int lineNumber, int column, QVector<int>* groupIndices)
{
    if (state == nullptr) {
        return;
    }
    if (token.isEmpty()) {
        return;
    }

    bool simpleDigitCluster = true;
    for (QChar ch : token) {
        if (!isDigitLane(ch)) {
            simpleDigitCluster = false;
            break;
        }
    }
    if (simpleDigitCluster && token.size() > 1) {
        for (int i = 0; i < token.size(); ++i) {
            parseTapOrHoldToken(state, token.mid(i, 1), lineNumber, column + i, groupIndices);
        }
        return;
    }

    if (isTouchPrefix(token)) {
        parseTouchToken(state, token, lineNumber, column, groupIndices);
        return;
    }
    if (isDigitLane(token.at(0))) {
        parseTapOrHoldToken(state, token, lineNumber, column, groupIndices);
        return;
    }

    appendTokenError(state, lineNumber, column, QString("Invalid note: %1").arg(token));
}

SimaiNativeParseResult parseInternal(const QString& text, bool strictMode)
{
    ParseState state;

    QString token;
    int tokenColumn = 1;
    QVector<int> currentGroup;

    const auto flushToken = [&](int lineNumber) {
        if (token.isEmpty()) {
            return;
        }
        parseToken(&state, token, lineNumber, tokenColumn, &currentGroup);
        token.clear();
    };

    const QStringList lines = text.split('\n');
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        QString line = lines.at(lineIndex);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        const int lineNumber = lineIndex + 1;
        if (line.trimmed() == QChar('E')) {
            continue;
        }
        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);

            if (ch == QChar('|') && i + 1 < line.size() && line.at(i + 1) == QChar('|')) {
                flushToken(lineNumber);
                break;
            }

            if (ch.isSpace()) {
                flushToken(lineNumber);
                continue;
            }

            if (ch == QChar('(')) {
                flushToken(lineNumber);
                int close = line.indexOf(')', i + 1);
                if (close < 0) {
                    appendTokenError(&state, lineNumber, i + 1, "Unterminated BPM block");
                    break;
                }
                bool bpmOk = false;
                const double bpm = line.mid(i + 1, close - i - 1).trimmed().toDouble(&bpmOk);
                if (!bpmOk || bpm <= 0.0) {
                    appendTokenError(&state, lineNumber, i + 1, "Invalid BPM value");
                } else {
                    state.bpm = bpm;
                }
                i = close;
                continue;
            }

            if (ch == QChar('{')) {
                flushToken(lineNumber);
                int close = line.indexOf('}', i + 1);
                if (close < 0) {
                    appendTokenError(&state, lineNumber, i + 1, "Unterminated beat block");
                    break;
                }
                bool beatsOk = false;
                const int beats = line.mid(i + 1, close - i - 1).trimmed().toInt(&beatsOk);
                if (!beatsOk || beats <= 0) {
                    appendTokenError(&state, lineNumber, i + 1, "Invalid beat value");
                } else {
                    state.beats = beats;
                }
                i = close;
                continue;
            }

                if (ch == QChar('H') && line.mid(i, 3) == QStringLiteral("HS*")) {
                    flushToken(lineNumber);
                    const int close = line.indexOf('>', i + 3);
                    if (close < 0) {
                        if (strictMode) {
                            appendTokenError(&state, lineNumber, i + 1, "Unterminated HS* block");
                        }
                        break;
                    }
                    i = close;
                    continue;
                }

            if (ch == QChar('/')) {
                flushToken(lineNumber);
                continue;
            }

            if (ch == QChar('`')) {
                flushToken(lineNumber);
                finalizeEachGroup(&state.result.noteMarkers, currentGroup);
                currentGroup.clear();
                continue;
            }

            if (ch == QChar(',')) {
                flushToken(lineNumber);
                finalizeEachGroup(&state.result.noteMarkers, currentGroup);
                currentGroup.clear();
                TimelineBeatMarker marker;
                marker.second = state.second;
                marker.sourceLine = qMax(1, lineNumber);
                marker.major = (state.lastBeatSourceLine != marker.sourceLine);
                state.result.beatMarkers.append(marker);
                state.lastBeatSourceLine = marker.sourceLine;
                state.result.durationSeconds = qMax(state.result.durationSeconds, state.second);
                state.second += noteStepSeconds(state.bpm, state.beats);
                continue;
            }

            if (token.isEmpty()) {
                tokenColumn = i + 1;
            }
            token.append(ch);
        }

        flushToken(lineNumber);
        finalizeEachGroup(&state.result.noteMarkers, currentGroup);
        currentGroup.clear();
    }

    if (strictMode) {
        runStrictFormatChecks(&state, lines);
    }

    std::sort(
        state.result.noteMarkers.begin(),
        state.result.noteMarkers.end(),
        [](const TimelineNoteMarker& a, const TimelineNoteMarker& b) {
            if (!qFuzzyCompare(a.second + 1.0, b.second + 1.0)) {
                return a.second < b.second;
            }
            if (a.lane != b.lane) {
                return a.lane < b.lane;
            }
            return a.sourceLine < b.sourceLine;
        }
    );

    QHash<qint64, QVector<int>> slideTraceGroups;
    for (int i = 0; i < state.result.noteMarkers.size(); ++i) {
        const TimelineNoteMarker& marker = state.result.noteMarkers.at(i);
        if ((marker.type != "slide" && marker.type != "wifi") || marker.slideTraceSecond < 0.0) {
            continue;
        }
        const qint64 key = qRound64(marker.slideTraceSecond * 1000000.0);
        slideTraceGroups[key].append(i);
    }
    for (const QVector<int>& group : slideTraceGroups) {
        if (group.size() < 2) {
            continue;
        }
        for (int index : group) {
            if (index >= 0 && index < state.result.noteMarkers.size()) {
                state.result.noteMarkers[index].slideEach = true;
            }
        }
    }

    QVector<int> slideIndices;
    slideIndices.reserve(state.result.noteMarkers.size());
    for (int i = 0; i < state.result.noteMarkers.size(); ++i) {
        if (markerIsSlideLike(state.result.noteMarkers.at(i))) {
            slideIndices.append(i);
        }
    }

    for (int i = 0; i < state.result.noteMarkers.size(); ++i) {
        TimelineNoteMarker& marker = state.result.noteMarkers[i];

        if (marker.type == "tap") {
            for (int slideIndex : slideIndices) {
                const TimelineNoteMarker& slide = state.result.noteMarkers.at(slideIndex);
                if (marker.lane == slide.lane
                    && qAbs(marker.second - slide.slideTraceSecond) < kTapOnSlideThresholdSeconds) {
                    marker.slideHead = true;
                    break;
                }
            }
        }

        if (marker.type == "hold" && marker.endSecond >= 0.0) {
            for (int slideIndex : slideIndices) {
                const TimelineNoteMarker& slide = state.result.noteMarkers.at(slideIndex);
                if (marker.lane == slide.lane
                    && qAbs(marker.endSecond - slide.slideTraceSecond) < kTapOnSlideThresholdSeconds) {
                    marker.tailOnSlideHead = true;
                    break;
                }
            }
        }

        if (marker.type == "touch") {
            for (int slideIndex : slideIndices) {
                if (touchHitsSlide(marker, state.result.noteMarkers.at(slideIndex))) {
                    marker.onSlide = true;
                    break;
                }
            }
        }
    }

    for (int i = 0; i < slideIndices.size(); ++i) {
        TimelineNoteMarker& note = state.result.noteMarkers[slideIndices[i]];
        for (int j = 0; j < slideIndices.size(); ++j) {
            if (i == j) {
                continue;
            }
            TimelineNoteMarker& note2 = state.result.noteMarkers[slideIndices[j]];
            // This currently ports only the strict "tail hits next shoot moment"
            // single-stroke linkage. The more complex embedded-track merge checks
            // from Python post_parse_workup are still pending.
            if (note.endLane == note2.lane
                && qAbs(note.endSecond - note2.slideTraceSecond) < kTapOnSlideThresholdSeconds) {
                note.beforeSlide = true;
                note2.afterSlide = true;
            }
        }
    }

    state.result.durationSeconds = qMax(
        state.result.durationSeconds,
        state.result.noteMarkers.isEmpty() ? 0.0 : state.result.noteMarkers.constLast().second
    );
    return state.result;
}

}  // namespace

SimaiNativeParseResult SimaiNativeParser::parseForTimeline(const QString& text)
{
    return parseInternal(text, false);
}

SimaiNativeParseResult SimaiNativeParser::validateSyntax(const QString& text)
{
    SimaiNativeParseResult result = parseInternal(text, true);
    return result;
}
