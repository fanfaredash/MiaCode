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

const QString& kMissingBeatSeparator()
{
    static const QString value = QStringLiteral("Missing beat separator ','");
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
        {kChartEmpty(), QStringLiteral("谱面为空。")},
    };
    return map;
}

const QHash<QString, QString>& zhPrefixMap()
{
    static const QHash<QString, QString> map{
        {kInvalidBreakSlideModifierPositionPrefix(), QStringLiteral("Break Slide 修饰符 b 位置可能导致转谱错误：")},
        {kInvalidSlideDurationPlacementPrefix(), QStringLiteral("Slide 时值块位置可能导致转谱错误：")},
        {kInvalidSlideDurationPrefix(), QStringLiteral("Slide 时值无效：")},
        {kInvalidHoldDurationPrefix(), QStringLiteral("Hold 时值无效：")},
        {kInvalidTouchHoldDurationPrefix(), QStringLiteral("TouchHold 时值无效：")},
        {kTouchDurationRequiresHPrefix(), QStringLiteral("Touch 时值需要 'h' 修饰符：")},
        {kInvalidTouchTokenPrefix(), QStringLiteral("Touch 音符无效：")},
        {kInvalidTouchModifierPrefix(), QStringLiteral("Touch 修饰符无效：")},
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
        kInvalidTouchHoldDurationPrefix(),
        kTouchDurationRequiresHPrefix(),
        kInvalidTouchTokenPrefix(),
        kInvalidTouchModifierPrefix(),
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

    appendTokenError(state, lineNumber, column, ValidationMessage::formatInvalidNote(token));
}

SimaiNativeParseResult parseInternal(const QString& text, bool strictMode, bool allowInvalidStarFallback = false)
{
    ParseState state;
    state.strictMode = strictMode;
    state.allowInvalidStarFallback = allowInvalidStarFallback;

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
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kUnterminatedBpmBlock());
                    break;
                }
                bool bpmOk = false;
                const double bpm = line.mid(i + 1, close - i - 1).trimmed().toDouble(&bpmOk);
                if (!bpmOk || bpm <= 0.0) {
                    appendTokenError(&state, lineNumber, i + 1, ValidationMessage::kInvalidBpmValue());
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
                flushToken(lineNumber);
                continue;
            }

            if (ch == QChar('`')) {
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

SimaiNativeParseResult SimaiNativeParser::parseForTimeline(const QString& text)
{
    return parseInternal(text, false, g_invalidStarPreviewEnabled);
}

SimaiNativeParseResult SimaiNativeParser::validateSyntax(const QString& text)
{
    SimaiNativeParseResult result = parseInternal(text, true, false);
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
    SimaiNativeValidationLocale locale)
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

    const SimaiNativeParseResult lenientResult = parseForTimeline(text);
    const SimaiNativeParseResult strictResult = validateSyntax(text);

    report.lenientNoteCount = lenientResult.noteMarkers.size();
    report.lenientErrorCount = lenientResult.errors.size();
    report.strictNoteCount = strictResult.noteMarkers.size();
    report.strictErrorCount = strictResult.errors.size();

    QSet<QString> lenientErrorKeys;
    for (const SimaiNativeMessage& error : lenientResult.errors) {
        lenientErrorKeys.insert(makeValidationMessageKey(error));
    }

    report.issues.reserve(strictResult.errors.size() + strictResult.warnings.size());
    for (const SimaiNativeMessage& error : strictResult.errors) {
        const bool lenientAlsoFailed = lenientErrorKeys.contains(makeValidationMessageKey(error));
        const SimaiNativeValidationSeverity severity = lenientAlsoFailed
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
