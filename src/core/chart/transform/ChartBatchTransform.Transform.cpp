#include "ChartBatchTransform.h"
#include "ChartBatchTransform.Internal.h"

#include <functional>

#include <QStringList>

namespace miacode::chart_transform {

using namespace detail;

QString transformChartText(const QString& input, ChartTransformOp op, int* changedCount)
{
    MC_OP("miacode::chart_transform::transformChartText");
    _mc_op_.note(QStringLiteral("op=%1 input_len=%2").arg(static_cast<int>(op)).arg(input.size()));
    auto isMirrorOp = [](ChartTransformOp current) {
        return current == ChartTransformOp::MirrorLeftRight || current == ChartTransformOp::MirrorUpDown;
    };
    auto mapLaneGeneral = [op](int lane) -> int {
        static const int mapMirrorLR[8] = {8, 7, 6, 5, 4, 3, 2, 1};
        static const int mapMirrorUD[8] = {4, 3, 2, 1, 8, 7, 6, 5};
        if (lane < 1 || lane > 8) {
            return lane;
        }
        switch (op) {
        case ChartTransformOp::MirrorLeftRight:
            return mapMirrorLR[lane - 1];
        case ChartTransformOp::MirrorUpDown:
            return mapMirrorUD[lane - 1];
        case ChartTransformOp::Rotate180:
            return ((lane - 1 + 4) % 8) + 1;
        case ChartTransformOp::Rotate45CounterClockwise:
            return ((lane - 1 + 7) % 8) + 1;
        case ChartTransformOp::Rotate45Clockwise:
            return ((lane - 1 + 1) % 8) + 1;
        }
        return lane;
    };
    auto mapLaneTouchDE = [op, mapLaneGeneral](int lane) -> int {
        static const int mapMirrorLR[8] = {1, 8, 7, 6, 5, 4, 3, 2};
        static const int mapMirrorUD[8] = {5, 4, 3, 2, 1, 8, 7, 6};
        if (lane < 1 || lane > 8) {
            return lane;
        }
        if (op == ChartTransformOp::MirrorLeftRight) {
            return mapMirrorLR[lane - 1];
        }
        if (op == ChartTransformOp::MirrorUpDown) {
            return mapMirrorUD[lane - 1];
        }
        return mapLaneGeneral(lane);
    };
    auto laneChar = [](int lane) -> QChar {
        return QChar('0' + qBound(1, lane, 8));
    };
    auto isClockwiseArc = [](int startLane, QChar arcType) -> bool {
        const bool groupA = (startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8);
        return groupA ? (arcType == QChar('>')) : (arcType == QChar('<'));
    };
    auto arcTypeFromDirection = [](int startLane, bool clockwise) -> QChar {
        const bool groupA = (startLane == 1 || startLane == 2 || startLane == 7 || startLane == 8);
        if (clockwise) {
            return groupA ? QChar('>') : QChar('<');
        }
        return groupA ? QChar('<') : QChar('>');
    };

    int changed = 0;

    std::function<QString(const QString&)> transformToken = [&](const QString& token) -> QString {
        if (token.isEmpty()) {
            return token;
        }

        const auto transformTouchToken = [&](const QString& in) -> QString {
            if (in.isEmpty()) {
                return in;
            }
            QString out = in;
            const QChar head = in.at(0).toUpper();
            if (head == QChar('C')) {
                return out;
            }
            if (in.size() >= 2
                && (head == QChar('A') || head == QChar('B') || head == QChar('D') || head == QChar('E'))
                && isDigitLane(in.at(1))) {
                const int lane = in.at(1).digitValue();
                const int mapped = (head == QChar('D') || head == QChar('E')) ? mapLaneTouchDE(lane) : mapLaneGeneral(lane);
                const QChar mappedChar = laneChar(mapped);
                if (mappedChar != in.at(1)) {
                    out[1] = mappedChar;
                    ++changed;
                }
            }
            return out;
        };

        const auto transformSlideToken = [&](const QString& in) -> QString {
            if (in.isEmpty() || !isDigitLane(in.at(0))) {
                return in;
            }
            auto hasSlideOperatorAhead = [](const QString& text) -> bool {
                for (QChar c : text) {
                    if (isSlideOperatorChar(c)) {
                        return true;
                    }
                }
                return false;
            };
            if (!hasSlideOperatorAhead(in.mid(1))) {
                return in;
            }
            const int headStartOriginal = in.at(0).digitValue();
            const int headStartMapped = mapLaneGeneral(headStartOriginal);
            const auto transformSlideCore = [&](const QString& core, int defaultStartOriginal, int defaultStartMapped) -> QString {
                if (core.isEmpty()) {
                    return core;
                }

                QString out;
                out.reserve(core.size());
                int i = 0;
                int segmentStartOriginal = defaultStartOriginal;
                int segmentStartMapped = defaultStartMapped;
                if (isDigitLane(core.at(0))) {
                    segmentStartOriginal = core.at(0).digitValue();
                    segmentStartMapped = mapLaneGeneral(segmentStartOriginal);
                    const QChar mappedStart = laneChar(segmentStartMapped);
                    out.append(mappedStart);
                    if (mappedStart != core.at(0)) {
                        ++changed;
                    }
                    i = 1;
                }

                while (i < core.size()) {
                    QString opToken;
                    int opLength = 0;
                    const QChar c = core.at(i);
                    if (i + 1 < core.size()
                        && ((c == QChar('p') && core.at(i + 1) == QChar('p'))
                            || (c == QChar('q') && core.at(i + 1) == QChar('q')))) {
                        opToken = QString(c) + core.at(i + 1);
                        opLength = 2;
                    } else if (isSlideOperatorChar(c)) {
                        opToken = QString(c);
                        opLength = 1;
                    }

                    if (opLength == 0) {
                        if (isDigitLane(c)) {
                            const QChar mapped = laneChar(mapLaneGeneral(c.digitValue()));
                            out.append(mapped);
                            if (mapped != c) {
                                ++changed;
                            }
                        } else {
                            out.append(c);
                        }
                        ++i;
                        continue;
                    }

                    i += opLength;
                    QString transformedOp = opToken;

                    if (opToken == "<" || opToken == ">") {
                        const bool clockwiseOriginal = isClockwiseArc(segmentStartOriginal, opToken.at(0));
                        const bool clockwiseTarget = isMirrorOp(op) ? !clockwiseOriginal : clockwiseOriginal;
                        transformedOp = QString(arcTypeFromDirection(segmentStartMapped, clockwiseTarget));
                    } else if (isMirrorOp(op)) {
                        if (opToken == "p") transformedOp = "q";
                        else if (opToken == "q") transformedOp = "p";
                        else if (opToken == "s") transformedOp = "z";
                        else if (opToken == "z") transformedOp = "s";
                        else if (opToken == "pp") transformedOp = "qq";
                        else if (opToken == "qq") transformedOp = "pp";
                    }

                    out.append(transformedOp);
                    if (transformedOp != opToken) {
                        ++changed;
                    }

                    if (opToken == "V") {
                        if (i + 1 >= core.size() || !isDigitLane(core.at(i)) || !isDigitLane(core.at(i + 1))) {
                            out.append(core.mid(i));
                            break;
                        }
                        const int midOriginal = core.at(i).digitValue();
                        const int endOriginal = core.at(i + 1).digitValue();
                        const QChar midMapped = laneChar(mapLaneGeneral(midOriginal));
                        const QChar endMapped = laneChar(mapLaneGeneral(endOriginal));
                        out.append(midMapped);
                        out.append(endMapped);
                        if (midMapped != core.at(i)) {
                            ++changed;
                        }
                        if (endMapped != core.at(i + 1)) {
                            ++changed;
                        }
                        segmentStartOriginal = endOriginal;
                        segmentStartMapped = endMapped.digitValue();
                        i += 2;
                    } else {
                        if (i >= core.size() || !isDigitLane(core.at(i))) {
                            out.append(core.mid(i));
                            break;
                        }
                        const int endOriginal = core.at(i).digitValue();
                        const QChar endMapped = laneChar(mapLaneGeneral(endOriginal));
                        out.append(endMapped);
                        if (endMapped != core.at(i)) {
                            ++changed;
                        }
                        segmentStartOriginal = endOriginal;
                        segmentStartMapped = endMapped.digitValue();
                        ++i;
                    }
                }

                return out;
            };

            QStringList segments;
            QString currentSegment;
            int bracketDepth = 0;
            for (QChar ch : in) {
                if (ch == QChar('*') && bracketDepth == 0) {
                    segments.append(currentSegment);
                    currentSegment.clear();
                    continue;
                }
                currentSegment.append(ch);
                if (ch == QChar('[')) {
                    ++bracketDepth;
                } else if (ch == QChar(']') && bracketDepth > 0) {
                    --bracketDepth;
                }
            }
            segments.append(currentSegment);

            QString out;
            out.reserve(in.size());
            for (int segmentIndex = 0; segmentIndex < segments.size(); ++segmentIndex) {
                if (segmentIndex > 0) {
                    out.append(QChar('*'));
                }

                const QString& segment = segments.at(segmentIndex);
                const int bracketPos = segment.indexOf('[');
                const QString core = bracketPos >= 0 ? segment.left(bracketPos) : segment;
                const QString suffix = bracketPos >= 0 ? segment.mid(bracketPos) : QString();
                if (core.isEmpty()) {
                    out.append(segment);
                    continue;
                }

                const QString operatorScan = isDigitLane(core.at(0)) ? core.mid(1) : core;
                if (!hasSlideOperatorAhead(operatorScan)) {
                    out.append(segment);
                    continue;
                }

                const int defaultStartOriginal = isDigitLane(core.at(0)) ? core.at(0).digitValue() : headStartOriginal;
                const int defaultStartMapped = isDigitLane(core.at(0)) ? mapLaneGeneral(defaultStartOriginal) : headStartMapped;
                out.append(transformSlideCore(core, defaultStartOriginal, defaultStartMapped));
                out.append(suffix);
            }

            return out;
        };

        if (token.at(0).toUpper() == QChar('C')) {
            return transformTouchToken(token);
        }
        if (token.size() >= 2) {
            const QChar head = token.at(0).toUpper();
            if ((head == QChar('A') || head == QChar('B') || head == QChar('D') || head == QChar('E'))
                && isDigitLane(token.at(1))) {
                return transformTouchToken(token);
            }
        }
        if (!isDigitLane(token.at(0))) {
            return token;
        }

        bool allDigits = true;
        for (QChar ch : token) {
            if (!isDigitLane(ch)) {
                allDigits = false;
                break;
            }
        }
        if (allDigits) {
            QString out = token;
            for (int i = 0; i < out.size(); ++i) {
                const QChar mapped = laneChar(mapLaneGeneral(out.at(i).digitValue()));
                if (mapped != out.at(i)) {
                    out[i] = mapped;
                    ++changed;
                }
            }
            return out;
        }

        bool hasSlideOps = false;
        for (QChar ch : token) {
            if (isSlideOperatorChar(ch)) {
                hasSlideOps = true;
                break;
            }
        }
        if (hasSlideOps) {
            return transformSlideToken(token);
        }

        QString out = token;
        const QChar mapped = laneChar(mapLaneGeneral(token.at(0).digitValue()));
        if (mapped != token.at(0)) {
            out[0] = mapped;
            ++changed;
        }
        return out;
    };

    QString transformed;
    transformed.reserve(input.size());
    const QStringList lines = input.split('\n', Qt::KeepEmptyParts);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString& line = lines.at(lineIndex);
        QString token;
        // Tracks matched [ inside the current token (e.g. slide duration
        // brackets in `8-3[8:1]` and chained `8b-3[8:1]*^5[8:1]`). When
        // we see a `]` and depth > 0, it belongs to this token and gets
        // appended; only depth == 0 means a stray closer and triggers
        // the defensive flush below.
        int tokenBracketDepth = 0;
        const auto flushToken = [&]() {
            if (!token.isEmpty()) {
                transformed.append(transformToken(token));
                token.clear();
            }
            tokenBracketDepth = 0;
        };

        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QChar('|') && i + 1 < line.size() && line.at(i + 1) == QChar('|')) {
                flushToken();
                transformed.append(line.mid(i));
                i = line.size();
                break;
            }
            if (ch.isSpace() || ch == QChar('/') || ch == QChar('`') || ch == QChar(',')) {
                flushToken();
                transformed.append(ch);
                continue;
            }
            // Defense-in-depth for selection-based callers: a closing
            // bracket without its opener (e.g. selection that begins with
            // "}") would otherwise be glued onto the next note and make
            // the whole token unrecognizable. Flushing here keeps the
            // stray closer in its own (untransformed) token so the
            // following note still parses cleanly. Well-formed whole-line
            // input never reaches this branch because the matching
            // opener handlers below swallow each bracket pair end-to-end.
            //
            // Exception: a `]` matching a `[` previously appended to the
            // current token (e.g. slide duration `8-3[8:1]`, or chained
            // slides `8b-3[8:1]*^5[8:1]` where the token spans several
            // bracketed sub-segments) MUST stay inside the token —
            // otherwise the second-segment leading operator (`*^5…`)
            // becomes a stand-alone unrecognizable token. Track depth
            // and only flush when the `]` is genuinely stray.
            if (ch == QChar(')') || ch == QChar('}')
                || (ch == QChar(']') && tokenBracketDepth == 0)) {
                flushToken();
                transformed.append(ch);
                continue;
            }
            if (ch == QChar(']') && tokenBracketDepth > 0) {
                token.append(ch);
                --tokenBracketDepth;
                continue;
            }
            if (ch == QChar('[')) {
                token.append(ch);
                ++tokenBracketDepth;
                continue;
            }
            if (ch == QChar('(')) {
                flushToken();
                const int close = line.indexOf(')', i + 1);
                if (close < 0) {
                    transformed.append(line.mid(i));
                    break;
                }
                transformed.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            if (ch == QChar('{')) {
                flushToken();
                const int close = line.indexOf('}', i + 1);
                if (close < 0) {
                    transformed.append(line.mid(i));
                    break;
                }
                transformed.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            if (ch == QChar('<') && line.mid(i, 4) == QStringLiteral("<HS*")) {
                flushToken();
                const int close = line.indexOf('>', i + 4);
                if (close < 0) {
                    transformed.append(line.mid(i));
                    break;
                }
                transformed.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            token.append(ch);
        }

        flushToken();
        if (lineIndex + 1 < lines.size()) {
            transformed.append('\n');
        }
    }

    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return transformed;
}

QString transformChartSelectionText(const QString& input, ChartTransformOp op, int* changedCount)
{
    MC_OP("miacode::chart_transform::transformChartSelectionText");
    _mc_op_.note(QStringLiteral("op=%1 input_len=%2").arg(static_cast<int>(op)).arg(input.size()));
    int changed = 0;
    const QString output = rewriteSelectionCore(input, [&](const QString& core) {
        return transformChartText(core, op, &changed);
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

}  // namespace miacode::chart_transform
