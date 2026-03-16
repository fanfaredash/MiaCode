#include "ChartBatchTransform.h"

#include <QRandomGenerator>
#include <QStringList>

namespace miacode::chart_transform {
namespace {

bool isDigitLane(QChar ch)
{
    return ch >= QChar('1') && ch <= QChar('8');
}

bool isSimpleDigitCluster(const QString& token)
{
    if (token.size() <= 1) {
        return false;
    }
    for (QChar ch : token) {
        if (!isDigitLane(ch)) {
            return false;
        }
    }
    return true;
}

bool hasSlideOperator(const QString& token)
{
    static const QString kSlideOps = QStringLiteral("-^v<>Vpqszw");
    for (QChar ch : token) {
        if (kSlideOps.contains(ch)) {
            return true;
        }
    }
    return false;
}

int touchPrefixLength(const QString& token)
{
    if (token.isEmpty()) {
        return 0;
    }
    const QChar head = token.at(0).toUpper();
    if (head == QChar('C')) {
        if (token.size() >= 2 && (token.at(1) == QChar('1') || token.at(1) == QChar('2'))) {
            return 2;
        }
        return 1;
    }
    if (token.size() >= 2
        && (head == QChar('A') || head == QChar('B') || head == QChar('D') || head == QChar('E'))
        && isDigitLane(token.at(1))) {
        return 2;
    }
    return 0;
}

struct TouchTokenParts {
    QString prefix;
    QString bracketSuffix;
    bool hasHold = false;
    bool hasBreak = false;
    bool hasEx = false;
    bool hasFirework = false;
    bool valid = false;
};

struct NoteTokenParts {
    QChar lane;
    QString bracketSuffix;
    bool hasHold = false;
    bool hasBreak = false;
    bool hasEx = false;
    bool valid = false;
};

struct SlideTokenParts {
    QString coreWithoutTrackBreak;
    bool headBreak = false;
    bool headEx = false;
    bool trackBreak = false;
    bool valid = false;
};

struct ToggleStats {
    int eligibleObjects = 0;
    int flaggedObjects = 0;
};

bool parseTouchTokenParts(const QString& token, TouchTokenParts* parts)
{
    if (parts == nullptr) {
        return false;
    }
    *parts = TouchTokenParts();

    const int prefixLength = touchPrefixLength(token);
    if (prefixLength <= 0 || prefixLength > token.size()) {
        return false;
    }

    const QString suffix = token.mid(prefixLength);
    const int openBracket = suffix.indexOf(QChar('['));
    const int closeBracket = suffix.lastIndexOf(QChar(']'));
    if ((openBracket < 0) != (closeBracket < 0)) {
        return false;
    }
    if (openBracket >= 0 && closeBracket != suffix.size() - 1) {
        return false;
    }

    parts->prefix = token.left(prefixLength);
    parts->bracketSuffix = openBracket >= 0 ? suffix.mid(openBracket) : QString();
    const QString modifierPart = openBracket >= 0 ? suffix.left(openBracket) : suffix;
    for (QChar ch : modifierPart) {
        const QChar lower = ch.toLower();
        if (lower == QChar('b')) {
            parts->hasBreak = true;
        } else if (lower == QChar('x')) {
            parts->hasEx = true;
        } else if (lower == QChar('f')) {
            parts->hasFirework = true;
        } else if (lower == QChar('h')) {
            parts->hasHold = true;
        } else if (!ch.isSpace()) {
            return false;
        }
    }
    if (!parts->bracketSuffix.isEmpty()) {
        if (!parts->hasHold) {
            return false;
        }
    } else if (parts->hasHold) {
        return false;
    }
    parts->valid = true;
    return true;
}

bool parseNoteTokenParts(const QString& token, NoteTokenParts* parts)
{
    if (parts == nullptr) {
        return false;
    }
    *parts = NoteTokenParts();
    if (token.isEmpty() || !isDigitLane(token.at(0)) || hasSlideOperator(token) || isSimpleDigitCluster(token)) {
        return false;
    }

    const int openBracket = token.indexOf(QChar('['));
    const int closeBracket = token.lastIndexOf(QChar(']'));
    if ((openBracket < 0) != (closeBracket < 0)) {
        return false;
    }
    if (openBracket >= 0 && closeBracket != token.size() - 1) {
        return false;
    }

    const QString core = openBracket >= 0 ? token.left(openBracket) : token;
    if (core.isEmpty() || !isDigitLane(core.at(0))) {
        return false;
    }

    parts->lane = core.at(0);
    parts->bracketSuffix = openBracket >= 0 ? token.mid(openBracket) : QString();
    for (int i = 1; i < core.size(); ++i) {
        const QChar lower = core.at(i).toLower();
        if (lower == QChar('b')) {
            parts->hasBreak = true;
        } else if (lower == QChar('x')) {
            parts->hasEx = true;
        } else if (lower == QChar('h')) {
            parts->hasHold = true;
        } else if (!core.at(i).isSpace()) {
            return false;
        }
    }
    parts->valid = true;
    return true;
}

bool parseSlideTokenParts(const QString& token, SlideTokenParts* parts)
{
    if (parts == nullptr) {
        return false;
    }
    *parts = SlideTokenParts();
    if (token.isEmpty() || !isDigitLane(token.at(0)) || !hasSlideOperator(token)) {
        return false;
    }

    int prefixLength = 0;
    while ((1 + prefixLength) < token.size()) {
        const QChar lower = token.at(1 + prefixLength).toLower();
        if (lower != QChar('b') && lower != QChar('x') && lower != QChar('h')) {
            break;
        }
        if (lower == QChar('h')) {
            return false;
        }
        ++prefixLength;
    }

    const QString prefixModifiers = token.mid(1, prefixLength);
    const QString remainder = token.mid(1 + prefixLength);
    if (remainder.isEmpty()) {
        return false;
    }

    QString core = QString(token.at(0)) + remainder;
    parts->headBreak = prefixModifiers.contains(QChar('b'), Qt::CaseInsensitive);
    parts->headEx = prefixModifiers.contains(QChar('x'), Qt::CaseInsensitive);
    parts->trackBreak = core.mid(1).contains(QChar('b'), Qt::CaseInsensitive);
    parts->coreWithoutTrackBreak.reserve(core.size());
    parts->coreWithoutTrackBreak.append(core.at(0));
    for (int i = 1; i < core.size(); ++i) {
        if (core.at(i).toLower() == QChar('b')) {
            continue;
        }
        parts->coreWithoutTrackBreak.append(core.at(i));
    }
    parts->valid = true;
    return true;
}

QString buildTouchToken(const TouchTokenParts& parts, bool hasBreak, bool hasEx, bool hasFirework)
{
    QString token = parts.prefix;
    if (hasBreak) {
        token.append(QChar('b'));
    }
    if (hasEx) {
        token.append(QChar('x'));
    }
    if (hasFirework) {
        token.append(QChar('f'));
    }
    if (parts.hasHold) {
        token.append(QChar('h'));
    }
    token.append(parts.bracketSuffix);
    return token;
}

QString buildNoteToken(const NoteTokenParts& parts, bool hasBreak, bool hasEx)
{
    QString token;
    token.reserve(1 + (hasBreak ? 1 : 0) + (hasEx ? 1 : 0) + (parts.hasHold ? 1 : 0) + parts.bracketSuffix.size());
    token.append(parts.lane);
    if (hasBreak) {
        token.append(QChar('b'));
    }
    if (hasEx) {
        token.append(QChar('x'));
    }
    if (parts.hasHold) {
        token.append(QChar('h'));
    }
    token.append(parts.bracketSuffix);
    return token;
}

QString buildSlideToken(const SlideTokenParts& parts, bool headBreak, bool headEx, bool trackBreak, const QString& coreWithoutTrackBreak)
{
    QString token;
    token.reserve(coreWithoutTrackBreak.size() + 3);
    token.append(coreWithoutTrackBreak.at(0));
    if (headBreak) {
        token.append(QChar('b'));
    }
    if (headEx) {
        token.append(QChar('x'));
    }
    QString remainder = coreWithoutTrackBreak.mid(1);
    if (trackBreak) {
        const int firstBracket = remainder.indexOf(QChar('['));
        if (firstBracket >= 0) {
            remainder.insert(firstBracket, QChar('b'));
        } else {
            remainder.append(QChar('b'));
        }
    }
    token.append(remainder);
    return token;
}

QChar rotateLaneChar(QChar lane, int steps)
{
    if (!isDigitLane(lane)) {
        return lane;
    }
    const int normalized = ((steps % 8) + 8) % 8;
    return QChar('0' + (((lane.digitValue() - 1 + normalized) % 8) + 1));
}

QString rotateSlideCoreOutsideBrackets(const QString& coreWithoutTrackBreak, int steps)
{
    QString rotated = coreWithoutTrackBreak;
    int bracketDepth = 0;
    for (int i = 0; i < rotated.size(); ++i) {
        const QChar ch = rotated.at(i);
        if (ch == QChar('[')) {
            ++bracketDepth;
            continue;
        }
        if (ch == QChar(']')) {
            bracketDepth = qMax(0, bracketDepth - 1);
            continue;
        }
        if (bracketDepth == 0 && isDigitLane(ch)) {
            rotated[i] = rotateLaneChar(ch, steps);
        }
    }
    return rotated;
}

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
            if (ch == QChar('H') && line.mid(i, 3) == QStringLiteral("HS*")) {
                flushToken();
                const int close = line.indexOf(QChar('>'), i + 3);
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
            if (ch == QChar('H') && line.mid(i, 3) == QStringLiteral("HS*")) {
                flushToken();
                const int close = line.indexOf(QChar('>'), i + 3);
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

}  // namespace

QString toggleBreakForSelection(const QString& input, int* changedCount)
{
    int changed = 0;
    const ToggleStats stats = collectBreakStats(input);
    const bool enable = stats.eligibleObjects > 0 && stats.flaggedObjects != stats.eligibleObjects;
    const QString output = rewriteSelectionTokens(input, [&](const QString& token) {
        return toggleBreakToken(token, enable, &changed);
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString toggleExForSelection(const QString& input, int* changedCount)
{
    int changed = 0;
    const ToggleStats stats = collectExStats(input);
    const bool enable = stats.eligibleObjects > 0 && stats.flaggedObjects != stats.eligibleObjects;
    const QString output = rewriteSelectionTokens(input, [&](const QString& token) {
        return toggleExToken(token, enable, &changed);
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString toggleFireworkForSelection(const QString& input, int* changedCount)
{
    int changed = 0;
    const ToggleStats stats = collectFireworkStats(input);
    const bool enable = stats.eligibleObjects > 0 && stats.flaggedObjects != stats.eligibleObjects;
    const QString output = rewriteSelectionTokens(input, [&](const QString& token) {
        return toggleFireworkToken(token, enable, &changed);
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
    const QString output = rewriteSelectionTokens(input, [&](const QString& token) {
        return rotateRandomToken(token, nextStep, &changed);
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

}  // namespace miacode::chart_transform
