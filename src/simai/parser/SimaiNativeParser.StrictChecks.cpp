void appendTokenError(ParseState* state, int line, int col, const QString& message, int endCol = -1)
{
    if (state == nullptr) {
        return;
    }
    SimaiNativeMessage error;
    error.line = qMax(1, line);
    error.col = qMax(1, col);
    error.endCol = qMax(error.col, endCol >= 0 ? endCol : error.col);
    error.message = message;
    state->result.errors.append(error);
    state->result.ok = false;
}

void appendTokenWarning(ParseState* state, int line, int col, const QString& message, int endCol = -1)
{
    if (state == nullptr) {
        return;
    }
    SimaiNativeMessage warning;
    warning.line = qMax(1, line);
    warning.col = qMax(1, col);
    warning.endCol = qMax(warning.col, endCol >= 0 ? endCol : warning.col);
    warning.message = message;
    state->result.warnings.append(warning);
}

void runStrictFormatChecks(ParseState* state, const QStringList& lines)
{
    if (state == nullptr) {
        return;
    }

    struct OpenBracket {
        QChar ch;
        int line = 1;
        int col = 1;
    };
    QVector<OpenBracket> stack;

    auto stripControlBlocks = [](const QString& line) {
        QString stripped;
        stripped.reserve(line.size());
        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QChar('(')) {
                const int close = line.indexOf(QChar(')'), i + 1);
                if (close < 0) {
                    break;
                }
                i = close;
                continue;
            }
            if (ch == QChar('{')) {
                const int close = line.indexOf(QChar('}'), i + 1);
                if (close < 0) {
                    break;
                }
                i = close;
                continue;
            }
            if (ch == QChar('H') && line.mid(i, 3) == QStringLiteral("HS*")) {
                const int close = line.indexOf(QChar('>'), i + 3);
                if (close < 0) {
                    break;
                }
                i = close;
                continue;
            }
            stripped.append(ch);
        }
        return stripped;
    };

    auto noteLikeLine = [](const QString& line) {
        for (QChar ch : line) {
            if (isDigitLane(ch)) {
                return true;
            }
            const QChar lower = ch.toLower();
            if (lower == QChar('a') || lower == QChar('b')
                || lower == QChar('c') || lower == QChar('d') || lower == QChar('e')) {
                return true;
            }
        }
        return false;
    };

    auto matches = [](QChar open, QChar close) {
        return (open == QChar('(') && close == QChar(')'))
            || (open == QChar('[') && close == QChar(']'))
            || (open == QChar('{') && close == QChar('}'));
    };

    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        QString line = lines.at(lineIndex);
        if (line.endsWith('\r')) {
            line.chop(1);
        }
        const int lineNumber = lineIndex + 1;
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty()) {
            continue;
        }
        if (trimmed.startsWith(QStringLiteral("||"))) {
            continue;
        }

        const QString strippedLine = stripControlBlocks(line);
        if (trimmed != QLatin1String("E")
            && noteLikeLine(strippedLine)
            && !strippedLine.contains(',')) {
            appendTokenError(state, lineNumber, 1, "Missing beat separator ','");
        }

        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QChar('(') || ch == QChar('[') || ch == QChar('{')) {
                stack.append(OpenBracket{ch, lineNumber, i + 1});
                continue;
            }
            if (ch != QChar(')') && ch != QChar(']') && ch != QChar('}')) {
                continue;
            }
            if (stack.isEmpty() || !matches(stack.constLast().ch, ch)) {
                appendTokenError(state, lineNumber, i + 1, QString("Unmatched closing bracket '%1'").arg(ch));
                continue;
            }
            stack.removeLast();
        }
    }

    while (!stack.isEmpty()) {
        const OpenBracket open = stack.takeLast();
        appendTokenError(state, open.line, open.col, QString("Unclosed bracket '%1'").arg(open.ch));
    }
}

void appendNote(ParseState* state, const TimelineNoteMarker& marker, QVector<int>* groupIndices)
{
    if (state == nullptr) {
        return;
    }
    TimelineNoteMarker stored = marker;
    stored.parseOrder = state->result.noteMarkers.size();
    state->result.noteMarkers.append(stored);
    if (groupIndices != nullptr) {
        groupIndices->append(state->result.noteMarkers.size() - 1);
    }
    state->result.durationSeconds = qMax(
        state->result.durationSeconds,
        qMax(marker.second, qMax(marker.endSecond, marker.slideTraceSecond))
    );
}

