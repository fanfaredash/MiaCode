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
    const QString suffix = token.mid(1);
    const int openBracket = suffix.indexOf('[');
    const int closeBracket = suffix.indexOf(']');
    const bool hasOpenBracket = openBracket >= 0;
    const bool hasCloseBracket = closeBracket >= 0;
    if (hasOpenBracket != hasCloseBracket) {
        appendTokenError(state, lineNumber, column, QString("Invalid hold duration: %1").arg(token));
        return;
    }
    if (hasOpenBracket) {
        if (closeBracket <= openBracket
            || suffix.indexOf('[', openBracket + 1) >= 0
            || suffix.indexOf(']', closeBracket + 1) >= 0) {
            appendTokenError(state, lineNumber, column, QString("Invalid hold duration: %1").arg(token));
            return;
        }
    }

    const QString prefixModifiers = hasOpenBracket ? suffix.left(openBracket) : suffix;
    const QString suffixModifiers = hasOpenBracket ? suffix.mid(closeBracket + 1) : QString();
    int breakCount = 0;
    int exCount = 0;
    int holdCount = 0;
    int firstHoldIndex = -1;
    bool hasNonCanonicalHoldModifierPlacement = false;

    const auto registerModifier = [&](QChar ch, int globalIndex) -> bool {
        const QChar lower = ch.toLower();
        if (lower == QChar('b')) {
            ++breakCount;
            return breakCount <= 1;
        }
        if (lower == QChar('x')) {
            ++exCount;
            return exCount <= 1;
        }
        if (lower == QChar('h')) {
            ++holdCount;
            if (firstHoldIndex < 0) {
                firstHoldIndex = globalIndex;
            }
            return holdCount <= 1;
        }
        return false;
    };

    for (int i = 0; i < prefixModifiers.size(); ++i) {
        if (!registerModifier(prefixModifiers.at(i), i)) {
            appendTokenError(state, lineNumber, column, QString("Invalid hold modifier sequence: %1").arg(token));
            return;
        }
    }
    for (int i = 0; i < suffixModifiers.size(); ++i) {
        if (!registerModifier(suffixModifiers.at(i), prefixModifiers.size() + i)) {
            appendTokenError(state, lineNumber, column, QString("Invalid hold modifier sequence: %1").arg(token));
            return;
        }
    }

    const bool hasHold = holdCount > 0;
    marker.isBreak = breakCount > 0;
    marker.isEx = exCount > 0;

    if (!hasHold && hasOpenBracket) {
        appendTokenError(state, lineNumber, column, QString("Invalid hold duration: %1").arg(token));
        return;
    }

    if (hasHold) {
        if (suffixModifiers.contains(QChar('h'), Qt::CaseInsensitive)) {
            appendTokenError(state, lineNumber, column, QString("Invalid hold modifier sequence: %1").arg(token));
            return;
        }
        double durationSecond = 0.0;
        bool durationOk = true;
        if (hasOpenBracket) {
            if (!suffixModifiers.isEmpty()) {
                hasNonCanonicalHoldModifierPlacement = true;
            }
            durationSecond = parseHoldDurationSignature(tokenInsideBrackets(token), state->bpm, &durationOk);
        }
        if (!durationOk) {
            appendTokenError(state, lineNumber, column, QString("Invalid hold duration: %1").arg(token));
            return;
        }

        marker.type = "hold";
        marker.endSecond = marker.second + qMax(0.0, durationSecond);
        if (state->strictMode && hasOpenBracket && hasNonCanonicalHoldModifierPlacement) {
            appendTokenWarning(
                state,
                lineNumber,
                column,
                QString("Non-canonical hold modifier placement: %1").arg(token)
            );
        }
    }

    appendNote(state, marker, groupIndices);
}

