namespace ValidationMessage {

const QString& kInvalidNotePrefix()
{
    static const QString value = QStringLiteral("Invalid note: ");
    return value;
}

const QString& kUnterminatedBpmBlock()
{
    static const QString value = QStringLiteral("Unterminated BPM block");
    return value;
}

const QString& kInvalidBpmValue()
{
    static const QString value = QStringLiteral("Invalid BPM value");
    return value;
}

const QString& kUnterminatedBeatBlock()
{
    static const QString value = QStringLiteral("Unterminated beat block");
    return value;
}

const QString& kInvalidBeatValue()
{
    static const QString value = QStringLiteral("Invalid beat value");
    return value;
}

const QString& kInvalidBeatValueStrictPrefix()
{
    static const QString value = QStringLiteral("Invalid beat value for strict mode: ");
    return value;
}

const QString& kStrictDivisorSuffix()
{
    static const QString value = QStringLiteral(" (must be a positive divisor of 384)");
    return value;
}

const QString& kBeatValueAbove384Prefix()
{
    static const QString value = QStringLiteral("Beat value above 384 may cause transfer issues: ");
    return value;
}

const QString& kUnterminatedHsBlock()
{
    static const QString value = QStringLiteral("Unterminated HS* block");
    return value;
}

const QString& kInvalidBreakSlideModifierPositionPrefix()
{
    static const QString value = QStringLiteral("Invalid break slide modifier position: ");
    return value;
}

const QString& kInvalidSlideDurationPlacementPrefix()
{
    static const QString value = QStringLiteral("Invalid slide duration placement: ");
    return value;
}

const QString& kInvalidSlideDurationPrefix()
{
    static const QString value = QStringLiteral("Invalid slide duration: ");
    return value;
}

const QString& kInvalidHoldDurationPrefix()
{
    static const QString value = QStringLiteral("Invalid hold duration: ");
    return value;
}

const QString& kInvalidHoldModifierSequencePrefix()
{
    static const QString value = QStringLiteral("Invalid hold modifier sequence: ");
    return value;
}

const QString& kNonCanonicalHoldModifierPlacementPrefix()
{
    static const QString value = QStringLiteral("Non-canonical hold modifier placement: ");
    return value;
}

const QString& kInvalidTouchHoldDurationPrefix()
{
    static const QString value = QStringLiteral("Invalid touch-hold duration: ");
    return value;
}

const QString& kTouchDurationRequiresHPrefix()
{
    static const QString value = QStringLiteral("Touch duration requires 'h': ");
    return value;
}

const QString& kInvalidTouchTokenPrefix()
{
    static const QString value = QStringLiteral("Invalid touch token: ");
    return value;
}

const QString& kInvalidTouchModifierPrefix()
{
    static const QString value = QStringLiteral("Invalid touch modifier: ");
    return value;
}

const QString& kFullwidthDigitPrefix()
{
    static const QString value = QStringLiteral("Fullwidth digit detected, use halfwidth digits: ");
    return value;
}

const QString& kFullwidthTouchRegionPrefix()
{
    static const QString value = QStringLiteral("Fullwidth touch region letter detected, use halfwidth region letters: ");
    return value;
}

const QString& kFullwidthModifierPrefix()
{
    static const QString value = QStringLiteral("Fullwidth modifier detected, use halfwidth modifiers: ");
    return value;
}

const QString& kFullwidthBracketPrefix()
{
    static const QString value = QStringLiteral("Fullwidth bracket detected, use halfwidth brackets: ");
    return value;
}

const QString& kFullwidthSeparatorPrefix()
{
    static const QString value = QStringLiteral("Fullwidth separator detected, use halfwidth separators: ");
    return value;
}

const QString& kFullwidthSlideSymbolPrefix()
{
    static const QString value = QStringLiteral("Fullwidth slide symbol detected, use halfwidth slide symbols: ");
    return value;
}

const QString& kFullwidthLatinLetterPrefix()
{
    static const QString value = QStringLiteral("Fullwidth latin letter detected, use halfwidth letters: ");
    return value;
}

const QString& kMissingBeatSeparator()
{
    static const QString value = QStringLiteral("Missing beat separator ','");
    return value;
}

const QString& kRepeatedSlashSeparator()
{
    static const QString value = QStringLiteral("Repeated separator '//' is not allowed");
    return value;
}

const QString& kRepeatedBacktickSeparator()
{
    static const QString value = QStringLiteral("Repeated separator '``' is not allowed");
    return value;
}

const QString& kUnmatchedClosingBracketPrefix()
{
    static const QString value = QStringLiteral("Unmatched closing bracket '");
    return value;
}

const QString& kUnclosedBracketPrefix()
{
    static const QString value = QStringLiteral("Unclosed bracket '");
    return value;
}

const QString& kChartEmpty()
{
    static const QString value = QStringLiteral("Chart is empty.");
    return value;
}

const QString& kInvalidTerminalMarkerPrefix()
{
    static const QString value = QStringLiteral("Invalid terminal marker placement: ");
    return value;
}

QString formatInvalidNote(const QString& token)
{
    return QStringLiteral("%1%2").arg(kInvalidNotePrefix(), token);
}

QString formatStrictBeatValue(int beats)
{
    return QStringLiteral("%1%2%3")
        .arg(kInvalidBeatValueStrictPrefix())
        .arg(beats)
        .arg(kStrictDivisorSuffix());
}

QString formatBeatValueClamped(int beats)
{
    return QStringLiteral("%1%2").arg(kBeatValueAbove384Prefix(), QString::number(beats));
}

const QHash<QString, QString>& zhExactMap()
{
    static const QHash<QString, QString> map{
        {kInvalidBpmValue(), QStringLiteral("BPM 数值无效")},
        {kInvalidBeatValue(), QStringLiteral("分拍数值无效")},
        {kUnterminatedBpmBlock(), QStringLiteral("BPM 块未闭合")},
        {kUnterminatedBeatBlock(), QStringLiteral("分拍块未闭合")},
        {kUnterminatedHsBlock(), QStringLiteral("HS* 块未闭合")},
        {kMissingBeatSeparator(), QStringLiteral("缺少拍间分隔符 ','")},
        {kRepeatedSlashSeparator(), QStringLiteral("不允许使用连续分隔符 '//'")},
        {kRepeatedBacktickSeparator(), QStringLiteral("不允许使用连续分隔符 '``'")},
        {kChartEmpty(), QStringLiteral("谱面为空。")},
    };
    return map;
}

bool shouldRemainValidationError(const QString& detail)
{
    return detail == kRepeatedSlashSeparator()
        || detail == kRepeatedBacktickSeparator()
        || detail.startsWith(kUnmatchedClosingBracketPrefix())
        || detail.startsWith(kUnclosedBracketPrefix());
}

const QHash<QString, QString>& zhPrefixMap()
{
    static const QHash<QString, QString> map{
        {kInvalidBreakSlideModifierPositionPrefix(), QStringLiteral("Break Slide 修饰符 b 位置可能导致转谱错误：")},
        {kInvalidSlideDurationPlacementPrefix(), QStringLiteral("Slide 时值块位置可能导致转谱错误：")},
        {kInvalidSlideDurationPrefix(), QStringLiteral("Slide 时值无效：")},
        {kInvalidHoldDurationPrefix(), QStringLiteral("Hold 时值无效：")},
        {kInvalidHoldModifierSequencePrefix(), QStringLiteral("Hold 修饰符顺序无效：")},
        {kNonCanonicalHoldModifierPlacementPrefix(), QStringLiteral("Hold 修饰符位置可能导致上机转换错误：")},
        {kInvalidTouchHoldDurationPrefix(), QStringLiteral("TouchHold 时值无效：")},
        {kTouchDurationRequiresHPrefix(), QStringLiteral("Touch 时值需要 'h' 修饰符：")},
        {kInvalidTouchTokenPrefix(), QStringLiteral("Touch 音符无效：")},
        {kInvalidTouchModifierPrefix(), QStringLiteral("Touch 修饰符无效：")},
        {kFullwidthDigitPrefix(), QStringLiteral("检测到全角数字，请改用半角数字：")},
        {kFullwidthTouchRegionPrefix(), QStringLiteral("检测到全角触摸区域字母，请改用半角区域字母：")},
        {kFullwidthModifierPrefix(), QStringLiteral("检测到全角修饰符，请改用半角修饰符：")},
        {kFullwidthBracketPrefix(), QStringLiteral("检测到全角括号，请改用半角括号：")},
        {kFullwidthSeparatorPrefix(), QStringLiteral("检测到全角分隔符，请改用半角分隔符：")},
        {kFullwidthSlideSymbolPrefix(), QStringLiteral("检测到全角 Slide 符号，请改用半角符号：")},
        {kFullwidthLatinLetterPrefix(), QStringLiteral("检测到全角字母，请改用半角字母：")},
        {kInvalidTerminalMarkerPrefix(), QStringLiteral("终止标记 E 位置无效：")},
        {kInvalidNotePrefix(), QStringLiteral("音符无效：")},
        {kInvalidBeatValueStrictPrefix(), QStringLiteral("分拍数值可能导致转谱错误：")},
        {kBeatValueAbove384Prefix(), QStringLiteral("分拍数值大于 384，可能导致转谱错误：")},
        {kUnmatchedClosingBracketPrefix(), QStringLiteral("未匹配的右括号 '")},
        {kUnclosedBracketPrefix(), QStringLiteral("未闭合的左括号 '")},
    };
    return map;
}

const QVector<QString>& zhPrefixOrder()
{
    static const QVector<QString> order{
        kInvalidBeatValueStrictPrefix(),
        kBeatValueAbove384Prefix(),
        kInvalidBreakSlideModifierPositionPrefix(),
        kInvalidSlideDurationPlacementPrefix(),
        kInvalidSlideDurationPrefix(),
        kInvalidHoldDurationPrefix(),
        kInvalidHoldModifierSequencePrefix(),
        kNonCanonicalHoldModifierPlacementPrefix(),
        kInvalidTouchHoldDurationPrefix(),
        kTouchDurationRequiresHPrefix(),
        kInvalidTouchTokenPrefix(),
        kInvalidTouchModifierPrefix(),
        kFullwidthDigitPrefix(),
        kFullwidthTouchRegionPrefix(),
        kFullwidthModifierPrefix(),
        kFullwidthBracketPrefix(),
        kFullwidthSeparatorPrefix(),
        kFullwidthSlideSymbolPrefix(),
        kFullwidthLatinLetterPrefix(),
        kInvalidTerminalMarkerPrefix(),
        kInvalidNotePrefix(),
        kUnmatchedClosingBracketPrefix(),
        kUnclosedBracketPrefix(),
    };
    return order;
}

}  // namespace ValidationMessage
void parseToken(ParseState* state, const QString& token, int lineNumber, int column, QVector<int>* groupIndices)
{
    if (state == nullptr) {
        return;
    }
    if (token.isEmpty()) {
        return;
    }

    if (const QString fullwidthIssue = detectFullwidthSyntaxIssueMessage(token); !fullwidthIssue.isEmpty()) {
        appendTokenError(state, lineNumber, column, fullwidthIssue, column + token.size() - 1);
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

    appendTokenError(state, lineNumber, column, classifyInvalidNoteMessage(token));
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

void applyInitialTimingMetadata(
    ParseState* state,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    if (state == nullptr) {
        return;
    }
    state->meterNumerator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureNumerator
        : kDefaultMeterNumerator;
    state->meterDenominator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureDenominator
        : kDefaultMeterDenominator;
}

struct TimedMarkerRef {
    int markerIndex = -1;
    double second = 0.0;
};

struct TouchWindowBuckets {
    QVector<TimedMarkerRef> slideHead;
    QVector<TimedMarkerRef> wifiHead;
    QVector<TimedMarkerRef> padEnter;
};

void sortTimedMarkerRefs(QVector<TimedMarkerRef>* refs)
{
    if (refs == nullptr || refs->size() < 2) {
        return;
    }
    std::sort(refs->begin(), refs->end(), [](const TimedMarkerRef& a, const TimedMarkerRef& b) {
        if (a.second != b.second) {
            return a.second < b.second;
        }
        return a.markerIndex < b.markerIndex;
    });
}

template<typename MarkFn>
void markOpenIntervalMatches(
    const QVector<TimedMarkerRef>& centers,
    const QVector<TimedMarkerRef>& queries,
    double lowerRadius,
    double upperRadius,
    MarkFn&& markFn,
    bool excludeSelf = false)
{
    if (centers.isEmpty() || queries.isEmpty()) {
        return;
    }

    int left = 0;
    int right = 0;
    const int centerCount = centers.size();
    for (const TimedMarkerRef& query : queries) {
        const double lowerBound = query.second - lowerRadius;
        const double upperBound = query.second + upperRadius;

        while (left < centerCount && !(centers[left].second > lowerBound)) {
            ++left;
        }
        if (right < left) {
            right = left;
        }
        while (right < centerCount && centers[right].second < upperBound) {
            ++right;
        }

        const int matchCount = right - left;
        if (matchCount <= 0) {
            continue;
        }
        if (excludeSelf && matchCount == 1 && centers[left].markerIndex == query.markerIndex) {
            continue;
        }
        markFn(query.markerIndex);
    }
}

SimaiNativeParseResult parseInternal(
    const QString& text,
    bool strictMode,
    bool allowInvalidStarFallback,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    ParseState state;
    state.strictMode = strictMode;
    state.allowInvalidStarFallback = allowInvalidStarFallback;
    applyInitialTimingMetadata(&state, timingMetadata);

    QString token;
    int tokenColumn = 1;
    QVector<int> currentGroup;
    bool initializedMeasureLines = false;

    const auto flushToken = [&](int lineNumber) {
        if (token.isEmpty()) {
            return;
        }
        parseToken(&state, token, lineNumber, tokenColumn, &currentGroup);
        token.clear();
    };
    const auto advanceMeasureLinesTo = [&](double targetSecond) {
        const double measureDuration = measureDurationSeconds(
            state.bpm,
            state.meterNumerator,
            state.meterDenominator);
        while (state.currentMeasureStartSecond + measureDuration <= targetSecond + kTimelineEpsilon) {
            state.currentMeasureStartSecond += measureDuration;
            appendDistinctSecond(&state.result.measureLineSeconds, state.currentMeasureStartSecond);
        }
    };

    const QStringList lines = text.split('\n');
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        QString line = lines.at(lineIndex);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        const int lineNumber = lineIndex + 1;
        if (isTerminalMarkerText(line)) {
            continue;
        }
        if (!initializedMeasureLines) {
            appendDistinctSecond(&state.result.measureLineSeconds, state.currentMeasureStartSecond);
            initializedMeasureLines = true;
        }
        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);

            if (ch == QChar('|') && i + 1 < line.size() && line.at(i + 1) == QChar('|')) {
                flushToken(lineNumber);
                int numerator = 0;
                int denominator = 0;
                if (miacode::simai::parseInlineTimeSignatureComment(
                        line,
                        i,
                        &numerator,
                        &denominator,
                        nullptr)) {
                    state.meterNumerator = numerator;
                    state.meterDenominator = denominator;
                    state.currentMeasureStartSecond = state.second;
                    appendDistinctSecond(&state.result.measureLineSeconds, state.currentMeasureStartSecond);
                }
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
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kUnterminatedBpmBlock());
                    break;
                }
                bool bpmOk = false;
                const double bpm = line.mid(i + 1, close - i - 1).trimmed().toDouble(&bpmOk);
                if (!bpmOk || bpm <= 0.0) {
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kInvalidBpmValue());
                } else {
                    if (qAbs(state.bpm - bpm) > kTimelineEpsilon) {
                        state.currentMeasureStartSecond = state.second;
                        appendDistinctSecond(&state.result.measureLineSeconds, state.currentMeasureStartSecond);
                    }
                    state.bpm = bpm;
                }
                i = close;
                continue;
            }

            if (ch == QChar('{')) {
                flushToken(lineNumber);
                int close = line.indexOf('}', i + 1);
                if (close < 0) {
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kUnterminatedBeatBlock());
                    break;
                }
                bool beatsOk = false;
                const int beats = line.mid(i + 1, close - i - 1).trimmed().toInt(&beatsOk);
                if (!beatsOk || beats <= 0) {
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kInvalidBeatValue());
                } else if (beats > 384) {
                    state.beats = beats;
                    if (strictMode) {
                        int warningEndCol = close + 1;
                        while (warningEndCol < line.size() && line.at(warningEndCol) == QChar(',')) {
                            ++warningEndCol;
                        }
                        appendTokenWarning(
                            &state,
                            lineNumber,
                            i + 1,
                            ValidationMessage::formatBeatValueClamped(beats),
                            warningEndCol
                        );
                    }
                } else if (strictMode && (384 % beats) != 0) {
                    appendTokenError(
                        &state,
                        lineNumber,
                        i + 1,
                        ValidationMessage::formatStrictBeatValue(beats)
                    );
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
                            appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kUnterminatedHsBlock());
                        }
                        break;
                    }
                    i = close;
                    continue;
                }

            if (ch == QChar('/')) {
                if (strictMode && i + 1 < line.size() && line.at(i + 1) == QChar('/')) {
                    flushToken(lineNumber);
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kRepeatedSlashSeparator(), i + 2);
                    ++i;
                    continue;
                }
                flushToken(lineNumber);
                continue;
            }

            if (ch == QChar('`')) {
                if (strictMode && i + 1 < line.size() && line.at(i + 1) == QChar('`')) {
                    flushToken(lineNumber);
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kRepeatedBacktickSeparator(), i + 2);
                    finalizeEachGroup(&state, currentGroup);
                    currentGroup.clear();
                    ++i;
                    continue;
                }
                flushToken(lineNumber);
                finalizeEachGroup(&state, currentGroup);
                currentGroup.clear();
                continue;
            }

            if (ch == QChar(',')) {
                flushToken(lineNumber);
                finalizeEachGroup(&state, currentGroup);
                currentGroup.clear();
                TimelineBeatMarker marker;
                marker.second = state.second;
                marker.sourceLine = qMax(1, lineNumber);
                marker.sourceCol = qMax(1, i + 1);
                marker.major = false;
                state.result.beatMarkers.append(marker);
                state.result.durationSeconds = qMax(state.result.durationSeconds, state.second);
                state.second += noteStepSeconds(state.bpm, state.beats);
                advanceMeasureLinesTo(state.second);
                continue;
            }

            if (token.isEmpty()
                && (ch == QChar('E') || ch == QChar('e'))
                && lineTailIsTerminalMarker(line, i)) {
                flushToken(lineNumber);
                break;
            }

            if (token.isEmpty()) {
                tokenColumn = i + 1;
            }
            token.append(ch);
        }

        flushToken(lineNumber);
        finalizeEachGroup(&state, currentGroup);
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

    QHash<int, QHash<qint64, QVector<int>>> slideTraceGroups;
    for (int i = 0; i < state.result.noteMarkers.size(); ++i) {
        const TimelineNoteMarker& marker = state.result.noteMarkers.at(i);
        if ((marker.type != "slide" && marker.type != "wifi") || marker.slideTraceSecond < 0.0) {
            continue;
        }
        const qint64 key = qRound64(marker.slideTraceSecond * 1000000.0);
        slideTraceGroups[marker.eachGroupId][key].append(i);
    }
    for (auto groupIt = slideTraceGroups.cbegin(); groupIt != slideTraceGroups.cend(); ++groupIt) {
        const auto& traceGroups = groupIt.value();
        for (auto traceIt = traceGroups.cbegin(); traceIt != traceGroups.cend(); ++traceIt) {
            const QVector<int>& group = traceIt.value();
            if (group.size() < 2) {
                continue;
            }
            for (int index : group) {
                if (index >= 0 && index < state.result.noteMarkers.size()) {
                    state.result.noteMarkers[index].slideEach = true;
                }
            }
        }
    }

    QHash<int, QVector<TimedMarkerRef>> traceByLane;
    QHash<int, QVector<TimedMarkerRef>> endByLane;
    QHash<QString, TouchWindowBuckets> touchWindowsByPad;
    QHash<int, QVector<TimedMarkerRef>> tapsByLane;
    QHash<int, QVector<TimedMarkerRef>> holdTailsByLane;
    QHash<QString, QVector<TimedMarkerRef>> touchesByPad;
    QHash<int, QVector<TimedMarkerRef>> traceQueriesByLane;
    QHash<int, QVector<TimedMarkerRef>> endQueriesByLane;

    for (int i = 0; i < state.result.noteMarkers.size(); ++i) {
        const TimelineNoteMarker& marker = state.result.noteMarkers.at(i);
        if (marker.type == "tap") {
            tapsByLane[marker.lane].append(TimedMarkerRef{i, marker.second});
            continue;
        }
        if (marker.type == "hold" && marker.endSecond >= 0.0) {
            holdTailsByLane[marker.lane].append(TimedMarkerRef{i, marker.endSecond});
            continue;
        }
        if (marker.type == "touch") {
            if (!marker.touchPad.isEmpty()) {
                touchesByPad[marker.touchPad.toUpper()].append(TimedMarkerRef{i, marker.second});
            }
            continue;
        }
        if (!markerIsSlideLike(marker)) {
            continue;
        }

        if (marker.slideTraceSecond >= 0.0) {
            traceByLane[marker.lane].append(TimedMarkerRef{i, marker.slideTraceSecond});
            traceQueriesByLane[marker.lane].append(TimedMarkerRef{i, marker.slideTraceSecond});
            if (marker.hasHeadStar) {
                tapsByLane[marker.lane].append(TimedMarkerRef{i, marker.second});
            }

            const QString headPad = slideHeadPad(marker.lane);
            if (!headPad.isEmpty()) {
                TouchWindowBuckets& buckets = touchWindowsByPad[headPad.toUpper()];
                if (marker.type == "slide") {
                    buckets.slideHead.append(TimedMarkerRef{i, marker.slideTraceSecond});
                } else if (marker.type == "wifi") {
                    buckets.wifiHead.append(TimedMarkerRef{i, marker.slideTraceSecond});
                }
            }
        }

        if (marker.endSecond >= marker.slideTraceSecond) {
            endByLane[marker.endLane].append(TimedMarkerRef{i, marker.endSecond});
            endQueriesByLane[marker.endLane].append(TimedMarkerRef{i, marker.endSecond});
        }

        if (marker.type == "slide") {
            const int segmentCount = qMin(
                marker.slideSegmentPadEnterTimes.size(),
                qMin(marker.slideSegmentShootSeconds.size(), marker.slideSegmentDurations.size()));
            for (int segmentIndex = 0; segmentIndex < segmentCount; ++segmentIndex) {
                const double shootSecond = marker.slideSegmentShootSeconds.at(segmentIndex);
                const double durationSecond = marker.slideSegmentDurations.at(segmentIndex);
                for (const MuriPadTimeEntry& entry : marker.slideSegmentPadEnterTimes.at(segmentIndex)) {
                    if (entry.pad.isEmpty()) {
                        continue;
                    }
                    touchWindowsByPad[entry.pad.toUpper()].padEnter.append(
                        TimedMarkerRef{i, shootSecond + entry.proportion * durationSecond});
                }
            }
        } else if (marker.type == "wifi" && marker.endSecond >= marker.slideTraceSecond) {
            const double durationSecond = marker.endSecond - marker.slideTraceSecond;
            for (const MuriPadTimeEntry& entry : marker.wifiPadEnterTimes) {
                if (entry.pad.isEmpty()) {
                    continue;
                }
                touchWindowsByPad[entry.pad.toUpper()].padEnter.append(
                    TimedMarkerRef{i, marker.slideTraceSecond + entry.proportion * durationSecond});
            }
        }
    }

    for (auto it = traceByLane.begin(); it != traceByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = endByLane.begin(); it != endByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = tapsByLane.begin(); it != tapsByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = holdTailsByLane.begin(); it != holdTailsByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = touchesByPad.begin(); it != touchesByPad.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = traceQueriesByLane.begin(); it != traceQueriesByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = endQueriesByLane.begin(); it != endQueriesByLane.end(); ++it) {
        sortTimedMarkerRefs(&it.value());
    }
    for (auto it = touchWindowsByPad.begin(); it != touchWindowsByPad.end(); ++it) {
        sortTimedMarkerRefs(&it.value().slideHead);
        sortTimedMarkerRefs(&it.value().wifiHead);
        sortTimedMarkerRefs(&it.value().padEnter);
    }

    for (auto it = tapsByLane.cbegin(); it != tapsByLane.cend(); ++it) {
        const auto traceIt = traceByLane.constFind(it.key());
        if (traceIt == traceByLane.constEnd()) {
            continue;
        }
        markOpenIntervalMatches(
            traceIt.value(),
            it.value(),
            kTapOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].slideHead = true;
            }
        );
    }

    for (auto it = holdTailsByLane.cbegin(); it != holdTailsByLane.cend(); ++it) {
        const auto traceIt = traceByLane.constFind(it.key());
        if (traceIt == traceByLane.constEnd()) {
            continue;
        }
        markOpenIntervalMatches(
            traceIt.value(),
            it.value(),
            kTapOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].tailOnSlideHead = true;
            }
        );
    }

    for (auto it = touchesByPad.cbegin(); it != touchesByPad.cend(); ++it) {
        const auto bucketIt = touchWindowsByPad.constFind(it.key());
        if (bucketIt == touchWindowsByPad.constEnd()) {
            continue;
        }
        const TouchWindowBuckets& buckets = bucketIt.value();
        const QVector<TimedMarkerRef>& queries = it.value();
        markOpenIntervalMatches(
            buckets.slideHead,
            queries,
            kTouchOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].onSlide = true;
            }
        );
        markOpenIntervalMatches(
            buckets.wifiHead,
            queries,
            kTouchOnSlideThresholdSeconds,
            kTouchOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].onSlide = true;
            }
        );
        markOpenIntervalMatches(
            buckets.padEnter,
            queries,
            kTouchOnSlideThresholdSeconds,
            kTouchOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].onSlide = true;
            }
        );
    }

    for (auto it = traceQueriesByLane.cbegin(); it != traceQueriesByLane.cend(); ++it) {
        const auto endIt = endByLane.constFind(it.key());
        if (endIt == endByLane.constEnd()) {
            continue;
        }
        markOpenIntervalMatches(
            endIt.value(),
            it.value(),
            kTapOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].afterSlide = true;
            },
            true
        );
    }

    for (auto it = endQueriesByLane.cbegin(); it != endQueriesByLane.cend(); ++it) {
        const auto traceIt = traceByLane.constFind(it.key());
        if (traceIt == traceByLane.constEnd()) {
            continue;
        }
        markOpenIntervalMatches(
            traceIt.value(),
            it.value(),
            kTapOnSlideThresholdSeconds,
            kTapOnSlideThresholdSeconds,
            [&state](int markerIndex) {
                state.result.noteMarkers[markerIndex].beforeSlide = true;
            },
            true
        );
    }

    state.result.durationSeconds = qMax(
        state.result.durationSeconds,
        state.result.noteMarkers.isEmpty() ? 0.0 : state.result.noteMarkers.constLast().second
    );
    return state.result;
}

QString makeValidationMessageKey(const SimaiNativeMessage& message)
{
    return QStringLiteral("%1:%2:%3:%4")
        .arg(message.line)
        .arg(message.col)
        .arg(message.endCol)
        .arg(message.message);
}

QString localizeValidationDetail(QString detail, SimaiNativeValidationLocale locale)
{
    if (locale == SimaiNativeValidationLocale::English) {
        return detail;
    }

    const auto exactIt = ValidationMessage::zhExactMap().constFind(detail);
    if (exactIt != ValidationMessage::zhExactMap().constEnd()) {
        return exactIt.value();
    }

    const QHash<QString, QString>& prefixMap = ValidationMessage::zhPrefixMap();
    for (const QString& prefix : ValidationMessage::zhPrefixOrder()) {
        if (!detail.startsWith(prefix)) {
            continue;
        }
        QString localized = prefixMap.value(prefix) + detail.mid(prefix.size());
        if (prefix == ValidationMessage::kInvalidBeatValueStrictPrefix()) {
            localized.replace(
                ValidationMessage::kStrictDivisorSuffix(),
                QStringLiteral("（必须是 384 的正整数约数）")
            );
        }
        return localized;
    }

    return detail;
}

QString validationSeverityPrefix(SimaiNativeValidationSeverity severity, SimaiNativeValidationLocale locale)
{
    if (locale == SimaiNativeValidationLocale::Chinese) {
        return severity == SimaiNativeValidationSeverity::Error
            ? QStringLiteral("[错误]")
            : QStringLiteral("[警告]");
    }
    return severity == SimaiNativeValidationSeverity::Error
        ? QStringLiteral("[ERROR]")
        : QStringLiteral("[WARNING]");
}

}  // namespace

SimaiNativeParseResult SimaiNativeParser::parseForTimeline(
    const QString& text,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    return parseInternal(text, false, g_invalidStarPreviewEnabled, timingMetadata);
}

SimaiNativeParseResult SimaiNativeParser::validateSyntax(
    const QString& text,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    SimaiNativeParseResult result = parseInternal(text, true, false, timingMetadata);
    return result;
}

void SimaiNativeParser::setInvalidStarPreviewEnabled(bool enabled)
{
    g_invalidStarPreviewEnabled = enabled;
}

bool SimaiNativeParser::invalidStarPreviewEnabled()
{
    return g_invalidStarPreviewEnabled;
}

SimaiNativeValidationReport SimaiNativeParser::buildValidationReport(
    const QString& text,
    SimaiNativeValidationLocale locale,
    const SimaiNativeParseResult* lenientResult,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    SimaiNativeValidationReport report;

    if (text.trimmed().isEmpty()) {
        SimaiNativeValidationIssue issue;
        issue.line = 1;
        issue.col = 1;
        issue.endCol = 1;
        issue.severity = SimaiNativeValidationSeverity::Error;
        issue.rawMessage = ValidationMessage::kChartEmpty();
        issue.displayMessage = QStringLiteral("%1 %2")
            .arg(validationSeverityPrefix(issue.severity, locale), localizeValidationDetail(issue.rawMessage, locale));
        report.issues.append(issue);
        report.errorCount = 1;
        report.strictErrorCount = 1;
        report.ok = false;
        return report;
    }

    SimaiNativeParseResult lenientOwned;
    const SimaiNativeParseResult* effectiveLenientResult = lenientResult;
    if (effectiveLenientResult == nullptr) {
        lenientOwned = parseForTimeline(text, timingMetadata);
        effectiveLenientResult = &lenientOwned;
    }
    const SimaiNativeParseResult strictResult = validateSyntax(text, timingMetadata);

    report.lenientNoteCount = effectiveLenientResult->noteMarkers.size();
    report.lenientErrorCount = effectiveLenientResult->errors.size();
    report.strictNoteCount = strictResult.noteMarkers.size();
    report.strictErrorCount = strictResult.errors.size();

    QSet<QString> lenientErrorKeys;
    for (const SimaiNativeMessage& error : effectiveLenientResult->errors) {
        lenientErrorKeys.insert(makeValidationMessageKey(error));
    }

    report.issues.reserve(strictResult.errors.size() + strictResult.warnings.size());
    for (const SimaiNativeMessage& error : strictResult.errors) {
        const bool lenientAlsoFailed = lenientErrorKeys.contains(makeValidationMessageKey(error));
        const SimaiNativeValidationSeverity severity = lenientAlsoFailed
            || ValidationMessage::shouldRemainValidationError(error.message)
            ? SimaiNativeValidationSeverity::Error
            : SimaiNativeValidationSeverity::Warning;
        if (severity == SimaiNativeValidationSeverity::Error) {
            ++report.errorCount;
        } else {
            ++report.warningCount;
        }

        SimaiNativeValidationIssue issue;
        issue.line = error.line;
        issue.col = error.col;
        issue.endCol = error.endCol;
        issue.severity = severity;
        issue.rawMessage = error.message;
        issue.displayMessage = QStringLiteral("%1 %2")
            .arg(validationSeverityPrefix(severity, locale), localizeValidationDetail(error.message, locale));
        report.issues.append(issue);
    }

    for (const SimaiNativeMessage& warning : strictResult.warnings) {
        ++report.warningCount;

        SimaiNativeValidationIssue issue;
        issue.line = warning.line;
        issue.col = warning.col;
        issue.endCol = warning.endCol;
        issue.severity = SimaiNativeValidationSeverity::Warning;
        issue.rawMessage = warning.message;
        issue.displayMessage = QStringLiteral("%1 %2")
            .arg(validationSeverityPrefix(issue.severity, locale), localizeValidationDetail(warning.message, locale));
        report.issues.append(issue);
    }

    report.ok = (report.errorCount == 0);
    return report;
}
