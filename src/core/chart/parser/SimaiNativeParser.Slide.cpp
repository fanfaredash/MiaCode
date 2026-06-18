QVector<MuriPadTimeEntry> loadPadEnterTimes(const QJsonArray& padEnterTimes)
{
    QVector<MuriPadTimeEntry> result;
    result.reserve(padEnterTimes.size());
    for (const QJsonValue& value : padEnterTimes) {
        if (!value.isObject()) {
            continue;
        }
        const QJsonObject object = value.toObject();
        MuriPadTimeEntry entry;
        entry.pad = object.value("pad").toString();
        entry.proportion = object.value("t").toDouble();
        if (!entry.pad.isEmpty()) {
            result.append(entry);
        }
    }
    return result;
}

void parseSlideToken(ParseState* state, const QString& token, int lineNumber, int column, QVector<int>* groupIndices)
{
    if (state == nullptr || token.isEmpty() || !isDigitLane(token.at(0))) {
        return;
    }

    int modifierCount = 0;
    SlideHeadModifierState modifierState;
    if (!parseSlideHeadModifierPrefix(token, &modifierCount, &modifierState)) {
        appendTokenError(state, lineNumber, column, classifyInvalidNoteMessage(token));
        return;
    }

    QString noteCore;
    noteCore.reserve(token.size());
    noteCore.append(token.at(0));
    noteCore.append(token.mid(1 + modifierCount));

    if (noteCore.contains(QChar('*'))) {
        const QString prefix = token.left(1) + modifierState.rawModifiers;
        const QChar startLane = token.at(0);
        const QStringList branches = noteCore.split(QChar('*'), Qt::KeepEmptyParts);
        bool flaggedHeadedBranch = false;
        for (int branchIndex = 0; branchIndex < branches.size(); ++branchIndex) {
            QString branch = branches.at(branchIndex);
            if (branch.isEmpty()) {
                continue;
            }
            if (!isDigitLane(branch.at(0))) {
                branch.prepend(startLane);
            } else if (branchIndex > 0 && state->strictMode && !flaggedHeadedBranch) {
                // A '*' branch must be a headless slide body (`5q2[4:1]*p8[4:1]`).
                // Repeating a lane digit after '*' is a syntax error — the digit
                // is NOT a new head; lenient parsing below substitutes the shared
                // head lane for it, so `5q2[4:1]*4p8[4:1]` silently becomes
                // 5p8[4:1]. Flag once and keep that lenient substitution so the
                // chart still previews.
                appendTokenError(
                    state,
                    lineNumber,
                    column,
                    QString("Invalid '*' slide branch (must omit the slide head): %1").arg(token),
                    column + token.size() - 1
                );
                flaggedHeadedBranch = true;
            }
            const QString branchToken = prefix + branch.mid(1);
            parseSlideToken(state, branchToken, lineNumber, column, groupIndices);
        }
        return;
    }

    if (slideCoreHasDisallowedModifiers(noteCore)) {
        appendTokenError(state, lineNumber, column, classifyInvalidNoteMessage(token));
        return;
    }

    bool trackBreak = false;
    bool warnedInvalidBreakPos = false;
    const int firstBracketIndex = noteCore.indexOf(QChar('['));
    for (int i = 1; i < noteCore.size(); ++i) {
        if (noteCore.at(i) != QChar('b')) {
            continue;
        }
        trackBreak = true;
        const bool validStrictBreakPos = firstBracketIndex > 0 && i == firstBracketIndex - 1;
        if (state->strictMode && !validStrictBreakPos && !warnedInvalidBreakPos) {
            // Non-canonical placement; flag once and let the slide parse
            // through. The `b` characters are stripped from sanitizedCore
            // below, so the slide shape resolves normally with trackBreak set.
            appendTokenWarning(
                state,
                lineNumber,
                column,
                QString("Invalid break slide modifier position: %1").arg(token)
            );
            warnedInvalidBreakPos = true;
        }
    }

    // Mine slide (`m` suffix, = MajdataPlay IsMineSlide). Canonically the
    // `m` sits at the very end (`1-3[2:1]m`), but we scan the whole core
    // like the `b` loop above so any placement is tolerated. The `m`
    // characters are stripped from sanitizedCore below so slide-shape
    // resolution is unaffected; head star + track both render as mines.
    bool trackMine = false;
    for (int i = 1; i < noteCore.size(); ++i) {
        const QChar ch = noteCore.at(i);
        if (ch == QChar('m') || ch == QChar('M')) {
            trackMine = true;
        }
    }

    QString sanitizedCore;
    sanitizedCore.reserve(noteCore.size());
    for (int i = 0; i < noteCore.size(); ++i) {
        const QChar ch = noteCore.at(i);
        if (ch == QChar('b') || ch == QChar('m') || ch == QChar('M')) {
            continue;
        }
        sanitizedCore.append(ch);
    }

    // Strict-only chain syntax check. Catches things the lenient chain
    // parser silently elides — orphan digits between shapes (`1v35-7`),
    // wrong digit counts after a shape, dangling brackets. Lenient mode
    // is unchanged (the existing parsers continue to do their best).
    // We emit and continue so downstream parsing still runs and may
    // produce additional, more specific errors (e.g. unknown-shape from
    // the slide-data lookup at parse time).
    if (state->strictMode && !isValidSlideChainStrict(sanitizedCore)) {
        appendTokenError(state, lineNumber, column,
            QString("Invalid slide chain syntax: %1").arg(token));
    }

    const int lane = token.at(0).digitValue();
    double waitSecond = 0.0;
    double durationSecond = 0.0;
    QStringList chainShapes;
    QVector<double> chainDurations;

    TimelineNoteMarker marker;
    marker.second = state->second;
    marker.availableSecond = marker.second;
    marker.sourceLine = lineNumber;
    marker.sourceCol = qMax(1, column);
    marker.lane = lane;
    marker.type = sanitizedCore.contains('w', Qt::CaseInsensitive) ? "wifi" : "slide";
    marker.slideDisplayKey = normalizedSlideLookupKey(sanitizedCore);
    marker.hasHeadStar = modifierState.headlessMode == SlideHeadlessMode::None;
    marker.headlessImmediate = modifierState.headlessMode == SlideHeadlessMode::Immediate;
    marker.headBreak = modifierState.headBreak;
    marker.trackBreak = trackBreak;
    marker.isBreak = marker.headBreak || marker.trackBreak;
    marker.trackMine = trackMine;
    marker.headMine = trackMine;
    marker.headEx = modifierState.headEx;
    marker.slideHeadUsesTapMaterial = modifierState.slideHeadUsesTapMaterial;
    marker.isEx = false;

    if (state->strictMode && marker.type == "slide" && sanitizedCore.count(QChar('[')) > 1) {
        // Per-segment ("分段") slide timing — non-canonical; festival (fes)
        // charts sometimes write it by mistake. Flag the syntax error but do
        // NOT bail: the chain parser below folds the per-segment durations into
        // the equivalent total-duration slide so the note still parses.
        appendTokenError(state, lineNumber, column, QString("Invalid slide duration placement: %1").arg(token));
    }

    if (marker.type == "slide"
        && parseStandardSlideChain(sanitizedCore, state->bpm, &chainShapes, &waitSecond, &chainDurations)) {
        for (QString& shapeKey : chainShapes) {
            shapeKey = canonicalSlideKey(shapeKey);
        }
        marker.slideTraceSecond = marker.second + qMax(0.0, waitSecond);
        marker.slideTrackKey = chainShapes.isEmpty() ? QString() : chainShapes.constFirst();
        marker.slideSegmentKeys = chainShapes;
        marker.slideSegmentDurations = chainDurations;
        marker.slideSegmentShootSeconds.clear();
        marker.slideSegmentPadEnterTimes.clear();
        marker.slideSegmentCriticalProportions.clear();
        marker.slideSegmentPoints.clear();
        marker.slideSegmentAngles.clear();
        marker.slideTrackAreaPoints.clear();
        marker.slideTrackAreaRotations.clear();
        marker.slideTrackAreaThresholds.clear();
        marker.slideTrackAreaCheckpoints.clear();
        marker.slideTrackAreaCutIndices.clear();

        double segmentShootSecond = marker.slideTraceSecond;
        marker.endLane = lane;
        for (int shapeIndex = 0; shapeIndex < chainShapes.size(); ++shapeIndex) {
            const QString& shapeKey = chainShapes.at(shapeIndex);
            const QJsonObject entry = slideDataRoot().value("slides").toObject().value(shapeKey).toObject();
            if (entry.isEmpty()) {
                appendTokenError(state, lineNumber, column, QString("Invalid note: %1 unknown shape %2").arg(token, shapeKey));
                return;
            }

            marker.slideSegmentShootSeconds.append(segmentShootSecond);
            segmentShootSecond += qMax(0.001, chainDurations.value(shapeIndex, 0.001));
            marker.endLane = qBound(1, entry.value("end").toInt(marker.endLane), 8);
            marker.slideSegmentPadEnterTimes.append(loadPadEnterTimes(entry.value("pad_enter_times").toArray()));
            marker.slideSegmentCriticalProportions.append(entry.value("critical_proportion").toDouble(1.0));

            QVector<QPointF> points;
            QVector<double> angles;
            loadSamplePath(entry.value("samples").toArray(), &points, &angles);
            marker.slideSegmentPoints.append(points);
            marker.slideSegmentAngles.append(angles);

            QVector<QVector<QPointF>> segmentAreas;
            QVector<QVector<double>> segmentRotations;
            const QJsonArray areaArray = entry.value("track_arrows").toArray();
            segmentAreas.reserve(areaArray.size());
            segmentRotations.reserve(areaArray.size());
            for (const QJsonValue& areaValue : areaArray) {
                QVector<QPointF> areaPoints;
                QVector<double> areaRotations;
                const QJsonArray arrowArray = areaValue.toArray();
                areaPoints.reserve(arrowArray.size());
                areaRotations.reserve(arrowArray.size());
                for (const QJsonValue& arrowValue : arrowArray) {
                    const QJsonObject arrowObject = arrowValue.toObject();
                    areaPoints.append(QPointF(arrowObject.value("x").toDouble(), arrowObject.value("y").toDouble()));
                    areaRotations.append(arrowObject.value("rotation").toDouble());
                }
                segmentAreas.append(areaPoints);
                segmentRotations.append(areaRotations);
            }
            marker.slideTrackAreaPoints.append(segmentAreas);
            marker.slideTrackAreaRotations.append(segmentRotations);

            QVector<double> thresholds;
            const QJsonArray thresholdArray = entry.value("track_thresholds").toArray();
            thresholds.reserve(thresholdArray.size());
            for (const QJsonValue& thresholdValue : thresholdArray) {
                thresholds.append(thresholdValue.toDouble());
            }
            marker.slideTrackAreaThresholds.append(thresholds);

            QVector<QVector<double>> checkpointGroups;
            const QJsonArray checkpointArray = entry.value("track_checkpoints").toArray();
            checkpointGroups.reserve(checkpointArray.size());
            for (const QJsonValue& groupValue : checkpointArray) {
                QVector<double> values;
                const QJsonArray valuesArray = groupValue.toArray();
                values.reserve(valuesArray.size());
                for (const QJsonValue& value : valuesArray) {
                    values.append(value.toDouble());
                }
                checkpointGroups.append(values);
            }
            marker.slideTrackAreaCheckpoints.append(checkpointGroups);

            QVector<QVector<int>> cutGroups;
            const QJsonArray cutArray = entry.value("track_cut_indices").toArray();
            cutGroups.reserve(cutArray.size());
            for (const QJsonValue& groupValue : cutArray) {
                QVector<int> values;
                const QJsonArray valuesArray = groupValue.toArray();
                values.reserve(valuesArray.size());
                for (const QJsonValue& value : valuesArray) {
                    values.append(value.toInt());
                }
                cutGroups.append(values);
            }
            marker.slideTrackAreaCutIndices.append(cutGroups);
        }

        marker.endSecond = segmentShootSecond;
        populateSlideTrackLengths(&marker);
        appendNote(state, marker, groupIndices);
        return;
    }

    const QString signature = tokenInsideBrackets(sanitizedCore);
    if (!parseSlideWaitAndDuration(signature, state->bpm, &waitSecond, &durationSecond)) {
        appendTokenError(state, lineNumber, column, QString("Invalid slide duration: %1").arg(token));
        return;
    }
    marker.slideTraceSecond = marker.second + qMax(0.0, waitSecond);
    marker.endSecond = marker.slideTraceSecond + qMax(0.0, durationSecond);
    marker.endLane = inferSlideEndLane(sanitizedCore, lane);
    const QString lookupKey = normalizedSlideLookupKey(sanitizedCore);
    marker.slideTrackKey = canonicalSlideKey(lookupKey.isEmpty() ? sanitizedCore : lookupKey);
    marker.slideSegmentKeys = QStringList{marker.slideTrackKey};
    marker.slideSegmentShootSeconds = QVector<double>{marker.slideTraceSecond};
    marker.slideSegmentDurations = QVector<double>{qMax(0.001, durationSecond)};
    marker.slideSegmentPadEnterTimes.clear();
    marker.slideSegmentCriticalProportions.clear();
    marker.wifiPadEnterTimes.clear();
    marker.wifiCriticalProportion = 1.0;

        if (!populateSlideFromLookup(marker.slideTrackKey, &marker)) {
        if (!state->allowInvalidStarFallback) {
            appendTokenError(state, lineNumber, column, classifyInvalidNoteMessage(token));
            return;
        }
        const QPointF start = polarPoint(kOuterLaneRadius, lane);
        const QPointF end = polarPoint(kOuterLaneRadius, marker.endLane);
        QVector<QPointF> segmentPoints;
        QVector<double> segmentAngles;
        buildLinearSamples(start, end, &segmentPoints, &segmentAngles);
        marker.slideSegmentPoints = QVector<QVector<QPointF>>{segmentPoints};
        marker.slideSegmentAngles = QVector<QVector<double>>{segmentAngles};

        if (marker.type == "slide") {
            QVector<QPointF> areaPoints = buildTrackArrowPoints(start, end, true);
            QVector<double> areaRotations(areaPoints.size(), -slideAngleDegrees(start, end));
            marker.slideTrackAreaPoints = QVector<QVector<QVector<QPointF>>>{QVector<QVector<QPointF>>{areaPoints}};
            marker.slideTrackAreaRotations = QVector<QVector<QVector<double>>>{QVector<QVector<double>>{areaRotations}};
            marker.slideTrackAreaThresholds = QVector<QVector<double>>{QVector<double>{0.0}};
            marker.slideTrackAreaCheckpoints = QVector<QVector<QVector<double>>>{QVector<QVector<double>>{QVector<double>()}};
            marker.slideTrackAreaCutIndices = QVector<QVector<QVector<int>>>{QVector<QVector<int>>{QVector<int>()}};
            marker.slideSegmentPadEnterTimes = QVector<QVector<MuriPadTimeEntry>>{QVector<MuriPadTimeEntry>()};
            marker.slideSegmentCriticalProportions = QVector<double>{1.0};
        } else {
            const QPointF delta = end - start;
            const QPointF normal(-delta.y(), delta.x());
            const double normalLength = std::hypot(normal.x(), normal.y());
            const QPointF unitNormal = normalLength > 0.0
                ? QPointF(normal.x() / normalLength, normal.y() / normalLength)
                : QPointF(0.0, 0.0);
            const QVector<double> laneOffsets{-28.0, 0.0, 28.0};
            for (double offset : laneOffsets) {
                QVector<QPointF> lanePoints;
                QVector<double> laneAngles;
                buildLinearSamples(
                    QPointF(start.x() + unitNormal.x() * offset, start.y() + unitNormal.y() * offset),
                    QPointF(end.x() + unitNormal.x() * offset, end.y() + unitNormal.y() * offset),
                    &lanePoints,
                    &laneAngles
                );
                marker.wifiLanePoints.append(lanePoints);
                marker.wifiLaneAngles.append(laneAngles);
            }
            QVector<QPointF> areaPoints = buildTrackArrowPoints(start, end, false);
            QVector<double> areaRotations(areaPoints.size(), slideAngleDegrees(start, end));
            QVector<int> imageIndices;
            imageIndices.reserve(areaPoints.size());
            for (int i = 0; i < areaPoints.size(); ++i) {
                imageIndices.append(i % 11);
            }
            marker.wifiTrackAreaPoints = QVector<QVector<QPointF>>{areaPoints};
            marker.wifiTrackAreaRotations = QVector<QVector<double>>{areaRotations};
            marker.wifiTrackAreaImageIndices = QVector<QVector<int>>{imageIndices};
            marker.wifiTrackAreaThresholds = QVector<double>{0.0};
            marker.wifiTrackAreaCheckpoints = QVector<QVector<double>>{QVector<double>()};
            marker.wifiPadEnterTimes.clear();
            marker.wifiCriticalProportion = 1.0;
        }
    }

    populateSlideTrackLengths(&marker);
    appendNote(state, marker, groupIndices);
}
