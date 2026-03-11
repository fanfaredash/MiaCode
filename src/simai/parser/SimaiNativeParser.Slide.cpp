void parseSlideToken(ParseState* state, const QString& token, int lineNumber, int column, QVector<int>* groupIndices)
{
    if (state == nullptr || token.isEmpty() || !isDigitLane(token.at(0))) {
        return;
    }

    QString prefixModifiers;
    int modifierCount = 0;
    while ((1 + modifierCount) < token.size()) {
        const QChar modifier = token.at(1 + modifierCount);
        if (modifier != QChar('b') && modifier != QChar('x') && modifier != QChar('h')) {
            break;
        }
        if (prefixModifiers.contains(modifier)) {
            appendTokenError(state, lineNumber, column, QString("Invalid note: %1").arg(token));
            return;
        }
        prefixModifiers.append(modifier);
        ++modifierCount;
    }

    if (prefixModifiers.contains(QChar('h'))) {
        appendTokenError(state, lineNumber, column, QString("Invalid note: %1").arg(token));
        return;
    }

    QString noteCore;
    noteCore.reserve(token.size());
    noteCore.append(token.at(0));
    noteCore.append(token.mid(1 + modifierCount));

    if (noteCore.contains(QChar('*'))) {
        const QString prefix = token.left(1) + prefixModifiers;
        const QChar startLane = token.at(0);
        const QStringList branches = noteCore.split(QChar('*'), Qt::KeepEmptyParts);
        for (const QString& branchRaw : branches) {
            QString branch = branchRaw;
            if (branch.isEmpty()) {
                continue;
            }
            if (!isDigitLane(branch.at(0))) {
                branch.prepend(startLane);
            }
            const QString branchToken = prefix + branch.mid(1);
            parseSlideToken(state, branchToken, lineNumber, column, groupIndices);
        }
        return;
    }

    bool trackBreak = false;
    QString sanitizedCore;
    sanitizedCore.reserve(noteCore.size());
    for (int i = 0; i < noteCore.size(); ++i) {
        const QChar ch = noteCore.at(i);
        if (ch == QChar('b')) {
            if (i > 0 && i + 1 < noteCore.size() && noteCore.at(i + 1) == QChar('[')) {
                trackBreak = true;
            }
            continue;
        }
        sanitizedCore.append(ch);
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
    marker.hasHeadStar = !sanitizedCore.contains('?') && !sanitizedCore.contains('!');
    marker.headBreak = prefixModifiers.contains(QChar('b'));
    marker.trackBreak = trackBreak;
    marker.isBreak = marker.headBreak || marker.trackBreak;
    marker.headEx = prefixModifiers.contains(QChar('x'));
    marker.isEx = false;

    if (marker.type == "slide" && parseStandardSlideChain(sanitizedCore, state->bpm, &chainShapes, &waitSecond, &chainDurations)) {
        for (QString& shapeKey : chainShapes) {
            shapeKey = canonicalSlideKey(shapeKey);
        }
        marker.slideTraceSecond = marker.second + qMax(0.0, waitSecond);
        marker.slideTrackKey = chainShapes.isEmpty() ? QString() : chainShapes.constFirst();
        marker.slideSegmentKeys = chainShapes;
        marker.slideSegmentDurations = chainDurations;
        marker.slideSegmentShootSeconds.clear();
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

    if (!populateSlideFromLookup(marker.slideTrackKey, &marker)) {
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
        }
    }

    appendNote(state, marker, groupIndices);
}

