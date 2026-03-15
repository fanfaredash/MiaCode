void parseTouchToken(ParseState* state, const QString& token, int lineNumber, int column, QVector<int>* groupIndices)
{
    if (state == nullptr || token.isEmpty()) {
        return;
    }

    QString normalizedToken = token;
    if (!state->strictMode
        && normalizedToken.size() >= 2
        && normalizedToken.at(0).toUpper() == QChar('C')
        && (normalizedToken.at(1) == QChar('1') || normalizedToken.at(1) == QChar('2'))) {
        normalizedToken.remove(1, 1);
    }

    QString durationSignature;
    bool hasHold = false;
    bool hasFirework = false;
    bool hasBreak = false;
    QString errorMessage;
    if (!parseTouchSuffix(normalizedToken, &durationSignature, &hasHold, &hasFirework, &hasBreak, &errorMessage)) {
        appendTokenError(state, lineNumber, column, errorMessage.isEmpty() ? QString("Invalid touch token: %1").arg(token) : errorMessage);
        return;
    }

    TimelineNoteMarker marker;
    marker.second = state->second;
    marker.sourceLine = lineNumber;
    marker.sourceCol = qMax(1, column);
    marker.lane = 9;
    marker.endLane = 9;
    marker.type = "touch";
    marker.touchPoint = touchPointForToken(normalizedToken);
    marker.touchPad = touchPadForToken(normalizedToken);
    marker.isBreak = hasBreak;
    marker.isFirework = hasFirework;

    if (hasHold) {
        bool durationOk = false;
        const double durationSecond = parseHoldDurationSignature(durationSignature, state->bpm, &durationOk);
        if (!durationOk) {
            appendTokenError(state, lineNumber, column, QString("Invalid touch-hold duration: %1").arg(token));
            return;
        }
        marker.type = "touch_hold";
        marker.endSecond = marker.second + qMax(0.0, durationSecond);
    }

    appendNote(state, marker, groupIndices);
}

void parseTapOrHoldToken(ParseState* state, const QString& token, int lineNumber, int column, QVector<int>* groupIndices)
{
    if (state == nullptr || token.isEmpty() || !isDigitLane(token.at(0))) {
        return;
    }

    if (token.contains('[') && (token.contains('-') || token.contains('^') || token.contains('v')
            || token.contains('<') || token.contains('>') || token.contains('V')
            || token.contains('p') || token.contains('q') || token.contains('s')
            || token.contains('z') || token.contains('w'))) {
        parseSlideToken(state, token, lineNumber, column, groupIndices);
        return;
    }

    TimelineNoteMarker marker;
    marker.second = state->second;
    marker.sourceLine = lineNumber;
    marker.sourceCol = qMax(1, column);
    marker.lane = token.at(0).digitValue();
    marker.endLane = marker.lane;
    marker.type = "tap";
    marker.isBreak = token.contains('b');
    marker.isEx = token.contains('x');

    if (token.contains('h')) {
        const bool hasOpenBracket = token.contains('[');
        const bool hasCloseBracket = token.contains(']');
        if (hasOpenBracket != hasCloseBracket) {
            appendTokenError(state, lineNumber, column, QString("Invalid hold duration: %1").arg(token));
            return;
        }

        double durationSecond = 0.0;
        if (hasOpenBracket && hasCloseBracket) {
            bool durationOk = false;
            durationSecond = parseHoldDurationSignature(tokenInsideBrackets(token), state->bpm, &durationOk);
            if (!durationOk) {
                appendTokenError(state, lineNumber, column, QString("Invalid hold duration: %1").arg(token));
                return;
            }
        }

        marker.type = "hold";
        marker.endSecond = marker.second + qMax(0.0, durationSecond);
    }

    appendNote(state, marker, groupIndices);
}

