#include "ChartBatchTransform.h"
#include "ChartBatchTransform.Internal.h"

#include <QRandomGenerator>
#include <QStringList>

namespace miacode::chart_transform::detail {

void scanSelectionTokens(const QString& input, const std::function<void(const QString&)>& visitToken)
{
    const QStringList lines = input.split('\n', Qt::KeepEmptyParts);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString& line = lines.at(lineIndex);
        QString token;
        const auto flushToken = [&]() {
            if (!token.isEmpty()) {
                visitToken(token);
                token.clear();
            }
        };

        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QChar('|') && i + 1 < line.size() && line.at(i + 1) == QChar('|')) {
                flushToken();
                break;
            }
            if (ch.isSpace() || ch == QChar('/') || ch == QChar('`') || ch == QChar(',')) {
                flushToken();
                continue;
            }
            if (ch == QChar('(')) {
                flushToken();
                const int close = line.indexOf(QChar(')'), i + 1);
                if (close < 0) {
                    break;
                }
                i = close;
                continue;
            }
            if (ch == QChar('{')) {
                flushToken();
                const int close = line.indexOf(QChar('}'), i + 1);
                if (close < 0) {
                    break;
                }
                i = close;
                continue;
            }
            if (ch == QChar('<') && line.mid(i, 4) == QStringLiteral("<HS*")) {
                flushToken();
                const int close = line.indexOf(QChar('>'), i + 4);
                if (close < 0) {
                    break;
                }
                i = close;
                continue;
            }
            token.append(ch);
        }

        flushToken();
    }
}

QString rewriteSelectionTokens(const QString& input, const std::function<QString(const QString&)>& rewriteToken)
{
    QString transformed;
    transformed.reserve(input.size() + 32);
    const QStringList lines = input.split('\n', Qt::KeepEmptyParts);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString& line = lines.at(lineIndex);
        QString token;
        const auto flushToken = [&]() {
            if (!token.isEmpty()) {
                transformed.append(rewriteToken(token));
                token.clear();
            }
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
            if (ch == QChar('(')) {
                flushToken();
                const int close = line.indexOf(QChar(')'), i + 1);
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
                const int close = line.indexOf(QChar('}'), i + 1);
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
                const int close = line.indexOf(QChar('>'), i + 4);
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
    return transformed;
}

ToggleStats collectBreakStats(const QString& input)
{
    ToggleStats stats;
    scanSelectionTokens(input, [&stats](const QString& token) {
        if (isSimpleDigitCluster(token)) {
            stats.eligibleObjects += token.size();
            return;
        }

        TouchTokenParts touch;
        if (parseTouchTokenParts(token, &touch) && touch.valid) {
            if (!touch.hasHold) {
                ++stats.eligibleObjects;
                if (touch.hasBreak) {
                    ++stats.flaggedObjects;
                }
            }
            return;
        }

        SlideTokenParts slide;
        if (parseSlideTokenParts(token, &slide) && slide.valid) {
            stats.eligibleObjects += 2;
            if (slide.headBreak) {
                ++stats.flaggedObjects;
            }
            if (slide.trackBreak) {
                ++stats.flaggedObjects;
            }
            return;
        }

        NoteTokenParts note;
        if (parseNoteTokenParts(token, &note) && note.valid) {
            ++stats.eligibleObjects;
            if (note.hasBreak) {
                ++stats.flaggedObjects;
            }
        }
    });
    return stats;
}

ToggleStats collectExStats(const QString& input)
{
    ToggleStats stats;
    scanSelectionTokens(input, [&stats](const QString& token) {
        if (isSimpleDigitCluster(token)) {
            stats.eligibleObjects += token.size();
            return;
        }

        SlideTokenParts slide;
        if (parseSlideTokenParts(token, &slide) && slide.valid) {
            ++stats.eligibleObjects;
            if (slide.headEx) {
                ++stats.flaggedObjects;
            }
            return;
        }

        NoteTokenParts note;
        if (parseNoteTokenParts(token, &note) && note.valid) {
            ++stats.eligibleObjects;
            if (note.hasEx) {
                ++stats.flaggedObjects;
            }
        }
    });
    return stats;
}

ToggleStats collectFireworkStats(const QString& input)
{
    ToggleStats stats;
    scanSelectionTokens(input, [&stats](const QString& token) {
        TouchTokenParts touch;
        if (parseTouchTokenParts(token, &touch) && touch.valid) {
            ++stats.eligibleObjects;
            if (touch.hasFirework) {
                ++stats.flaggedObjects;
            }
        }
    });
    return stats;
}

QString toggleBreakToken(const QString& token, bool enable, int* changedCount)
{
    if (isSimpleDigitCluster(token)) {
        if (!enable) {
            return token;
        }
        QStringList expanded;
        expanded.reserve(token.size());
        for (QChar lane : token) {
            expanded.append(QString(lane) + QChar('b'));
        }
        if (changedCount != nullptr) {
            *changedCount += token.size();
        }
        return expanded.join(QChar('/'));
    }

    TouchTokenParts touch;
    if (parseTouchTokenParts(token, &touch) && touch.valid) {
        if (touch.hasHold) {
            return token;
        }
        const QString rebuilt = buildTouchToken(touch, enable, touch.hasEx, touch.hasFirework);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    SlideTokenParts slide;
    if (parseSlideTokenParts(token, &slide) && slide.valid) {
        const QString rebuilt = buildSlideToken(slide, enable, slide.headEx, enable, slide.coreWithoutTrackBreak);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += (slide.headBreak != enable ? 1 : 0) + (slide.trackBreak != enable ? 1 : 0);
        }
        return rebuilt;
    }

    NoteTokenParts note;
    if (parseNoteTokenParts(token, &note) && note.valid) {
        const QString rebuilt = buildNoteToken(note, enable, note.hasEx);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    return token;
}

QString toggleExToken(const QString& token, bool enable, int* changedCount)
{
    if (isSimpleDigitCluster(token)) {
        if (!enable) {
            return token;
        }
        QStringList expanded;
        expanded.reserve(token.size());
        for (QChar lane : token) {
            expanded.append(QString(lane) + QChar('x'));
        }
        if (changedCount != nullptr) {
            *changedCount += token.size();
        }
        return expanded.join(QChar('/'));
    }

    SlideTokenParts slide;
    if (parseSlideTokenParts(token, &slide) && slide.valid) {
        const QString rebuilt = buildSlideToken(slide, slide.headBreak, enable, slide.trackBreak, slide.coreWithoutTrackBreak);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    NoteTokenParts note;
    if (parseNoteTokenParts(token, &note) && note.valid) {
        const QString rebuilt = buildNoteToken(note, note.hasBreak, enable);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    return token;
}

QString toggleFireworkToken(const QString& token, bool enable, int* changedCount)
{
    TouchTokenParts touch;
    if (parseTouchTokenParts(token, &touch) && touch.valid) {
        const QString rebuilt = buildTouchToken(touch, touch.hasBreak, touch.hasEx, enable);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }
    return token;
}

QString rotateRandomToken(const QString& token, const std::function<int()>& nextStep, int* changedCount)
{
    if (!nextStep) {
        return token;
    }

    if (isSimpleDigitCluster(token)) {
        QString rotated = token;
        for (int i = 0; i < rotated.size(); ++i) {
            rotated[i] = rotateLaneChar(rotated.at(i), nextStep());
        }
        if (rotated != token && changedCount != nullptr) {
            int delta = 0;
            for (int i = 0; i < token.size(); ++i) {
                if (token.at(i) != rotated.at(i)) {
                    ++delta;
                }
            }
            *changedCount += delta;
        }
        return rotated;
    }

    TouchTokenParts touch;
    if (parseTouchTokenParts(token, &touch) && touch.valid) {
        const int step = nextStep();
        QString rebuilt = buildTouchToken(touch, touch.hasBreak, touch.hasEx, touch.hasFirework);
        if (touch.prefix.size() >= 2 && isDigitLane(touch.prefix.at(1))) {
            rebuilt[1] = rotateLaneChar(rebuilt.at(1), step);
        }
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    SlideTokenParts slide;
    if (parseSlideTokenParts(token, &slide) && slide.valid) {
        const int step = nextStep();
        const QString rotatedCore = rotateSlideCoreOutsideBrackets(slide.coreWithoutTrackBreak, step);
        const QString rebuilt = buildSlideToken(slide, slide.headBreak, slide.headEx, slide.trackBreak, rotatedCore);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    NoteTokenParts note;
    if (parseNoteTokenParts(token, &note) && note.valid) {
        const int step = nextStep();
        QString rebuilt = buildNoteToken(note, note.hasBreak, note.hasEx);
        rebuilt[0] = rotateLaneChar(rebuilt.at(0), step);
        if (rebuilt != token && changedCount != nullptr) {
            *changedCount += 1;
        }
        return rebuilt;
    }

    return token;
}

}  // namespace miacode::chart_transform::detail

namespace miacode::chart_transform {

using namespace detail;

QString toggleBreakForSelection(const QString& input, int* changedCount)
{
    int changed = 0;
    const SelectionEdgeSplit split = splitSelectionEdges(input);
    const ToggleStats stats = collectBreakStats(split.core);
    const bool enable = stats.eligibleObjects > 0 && stats.flaggedObjects != stats.eligibleObjects;
    const QString output = rewriteSelectionCore(split, [&](const QString& core) {
        return rewriteSelectionTokens(core, [&](const QString& token) {
            return toggleBreakToken(token, enable, &changed);
        });
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString toggleExForSelection(const QString& input, int* changedCount)
{
    int changed = 0;
    const SelectionEdgeSplit split = splitSelectionEdges(input);
    const ToggleStats stats = collectExStats(split.core);
    const bool enable = stats.eligibleObjects > 0 && stats.flaggedObjects != stats.eligibleObjects;
    const QString output = rewriteSelectionCore(split, [&](const QString& core) {
        return rewriteSelectionTokens(core, [&](const QString& token) {
            return toggleExToken(token, enable, &changed);
        });
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString toggleFireworkForSelection(const QString& input, int* changedCount)
{
    int changed = 0;
    const SelectionEdgeSplit split = splitSelectionEdges(input);
    const ToggleStats stats = collectFireworkStats(split.core);
    const bool enable = stats.eligibleObjects > 0 && stats.flaggedObjects != stats.eligibleObjects;
    const QString output = rewriteSelectionCore(split, [&](const QString& core) {
        return rewriteSelectionTokens(core, [&](const QString& token) {
            return toggleFireworkToken(token, enable, &changed);
        });
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString randomRotateForSelection(const QString& input, int* changedCount)
{
    return randomRotateForSelection(
        input,
        []() {
            return QRandomGenerator::global()->bounded(8);
        },
        changedCount
    );
}

QString randomRotateForSelection(const QString& input, const std::function<int()>& nextStep, int* changedCount)
{
    int changed = 0;
    const QString output = rewriteSelectionCore(input, [&](const QString& core) {
        return rewriteSelectionTokens(core, [&](const QString& token) {
            return rotateRandomToken(token, nextStep, &changed);
        });
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

}  // namespace miacode::chart_transform
