#include "ChartBatchTransform.h"

#include "common/OperationLog.h"

#include <limits>

#include <QRandomGenerator>
#include <QStringList>
#include <QtGlobal>

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

bool isSlideOperatorChar(QChar ch)
{
    static const QString kSlideOps = QStringLiteral("-^v<>Vpqszw");
    return kSlideOps.contains(ch);
}

bool hasSlideOperator(const QString& token)
{
    for (QChar ch : token) {
        if (isSlideOperatorChar(ch)) {
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
    bool tapUsesStarMaterial = false;
    bool tapStarDouble = false;
    bool valid = false;
};

struct SlideTokenParts {
    QString coreWithoutTrackBreak;
    bool headBreak = false;
    bool headEx = false;
    bool headUsesTapMaterial = false;
    QChar headlessModifier;
    bool trackBreak = false;
    bool valid = false;
};

struct ToggleStats {
    int eligibleObjects = 0;
    int flaggedObjects = 0;
};

struct SelectionEdgeSplit {
    QString prefix;
    QString core;
    QString suffix;
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
    for (int i = 1; i < core.size();) {
        const QChar ch = core.at(i);
        const QChar lower = ch.toLower();
        if (lower == QChar('b')) {
            parts->hasBreak = true;
            ++i;
        } else if (lower == QChar('x')) {
            parts->hasEx = true;
            ++i;
        } else if (lower == QChar('h')) {
            parts->hasHold = true;
            ++i;
        } else if (ch == QChar('$')) {
            if (parts->tapUsesStarMaterial) {
                return false;
            }
            parts->tapUsesStarMaterial = true;
            if (i + 1 < core.size() && core.at(i + 1) == QChar('$')) {
                parts->tapStarDouble = true;
                i += 2;
            } else {
                ++i;
            }
        } else if (!ch.isSpace()) {
            return false;
        } else {
            ++i;
        }
    }
    if (parts->hasHold && parts->tapUsesStarMaterial) {
        return false;
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
        const QChar ch = token.at(1 + prefixLength);
        const QChar lower = ch.toLower();
        if (lower != QChar('b')
            && lower != QChar('x')
            && ch != QChar('@')
            && ch != QChar('?')
            && ch != QChar('!')
            && lower != QChar('h')) {
            break;
        }
        if (lower == QChar('h')) {
            return false;
        }
        if (ch == QChar('@')) {
            parts->headUsesTapMaterial = true;
        } else if (ch == QChar('?') || ch == QChar('!')) {
            parts->headlessModifier = ch;
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
    token.reserve(
        1
        + (hasBreak ? 1 : 0)
        + (hasEx ? 1 : 0)
        + (parts.tapUsesStarMaterial ? (parts.tapStarDouble ? 2 : 1) : 0)
        + (parts.hasHold ? 1 : 0)
        + parts.bracketSuffix.size());
    token.append(parts.lane);
    if (hasBreak) {
        token.append(QChar('b'));
    }
    if (hasEx) {
        token.append(QChar('x'));
    }
    if (parts.tapUsesStarMaterial) {
        token.append(QChar('$'));
        if (parts.tapStarDouble) {
            token.append(QChar('$'));
        }
    }
    if (parts.hasHold) {
        token.append(QChar('h'));
    }
    token.append(parts.bracketSuffix);
    return token;
}

QString buildSlideToken(
    const SlideTokenParts& parts,
    bool headBreak,
    bool headEx,
    bool trackBreak,
    const QString& coreWithoutTrackBreak)
{
    QString token;
    token.reserve(coreWithoutTrackBreak.size() + 5);
    token.append(coreWithoutTrackBreak.at(0));
    if (headBreak) {
        token.append(QChar('b'));
    }
    if (headEx) {
        token.append(QChar('x'));
    }
    if (parts.headUsesTapMaterial) {
        token.append(QChar('@'));
    }
    if (!parts.headlessModifier.isNull()) {
        token.append(parts.headlessModifier);
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

bool isLineBreakChar(QChar ch)
{
    return ch == QChar('\n')
        || ch == QChar('\r')
        || ch == QChar::LineSeparator
        || ch == QChar::ParagraphSeparator;
}

int commentStartIndexInLine(const QString& line)
{
    for (int i = 0; i + 1 < line.size(); ++i) {
        if (line.at(i) == QChar('|') && line.at(i + 1) == QChar('|')) {
            return i;
        }
    }
    return -1;
}

int protectedPrefixLengthForSelectionStart(const QString& line)
{
    int parenDepth = 0;
    int braceDepth = 0;
    int squareDepth = 0;
    const int commentStart = commentStartIndexInLine(line);
    const int scanEnd = commentStart >= 0 ? commentStart : line.size();
    for (int i = 0; i < scanEnd; ++i) {
        const QChar ch = line.at(i);
        if (ch == QChar('(')) {
            ++parenDepth;
            continue;
        }
        if (ch == QChar('{')) {
            ++braceDepth;
            continue;
        }
        if (ch == QChar('[')) {
            ++squareDepth;
            continue;
        }
        if (ch == QChar(')')) {
            if (parenDepth > 0) {
                --parenDepth;
                continue;
            }
            return i + 1;
        }
        if (ch == QChar('}')) {
            if (braceDepth > 0) {
                --braceDepth;
                continue;
            }
            return i + 1;
        }
        if (ch == QChar(']')) {
            if (squareDepth > 0) {
                --squareDepth;
                continue;
            }
            return i + 1;
        }
    }
    return 0;
}

int protectedSuffixStartForSelectionEnd(const QString& line)
{
    int parenDepth = 0;
    int braceDepth = 0;
    int squareDepth = 0;
    int suffixStart = line.size();
    bool found = false;
    const int commentStart = commentStartIndexInLine(line);
    const int scanEnd = commentStart >= 0 ? commentStart : line.size();
    for (int i = scanEnd - 1; i >= 0; --i) {
        const QChar ch = line.at(i);
        if (ch == QChar(')')) {
            ++parenDepth;
            continue;
        }
        if (ch == QChar('}')) {
            ++braceDepth;
            continue;
        }
        if (ch == QChar(']')) {
            ++squareDepth;
            continue;
        }
        if (ch == QChar('(')) {
            if (parenDepth > 0) {
                --parenDepth;
                continue;
            }
            suffixStart = i;
            found = true;
            continue;
        }
        if (ch == QChar('{')) {
            if (braceDepth > 0) {
                --braceDepth;
                continue;
            }
            suffixStart = i;
            found = true;
            continue;
        }
        if (ch == QChar('[')) {
            if (squareDepth > 0) {
                --squareDepth;
                continue;
            }
            suffixStart = i;
            found = true;
            continue;
        }
    }
    return found ? suffixStart : line.size();
}

SelectionEdgeSplit splitSelectionEdges(const QString& input)
{
    SelectionEdgeSplit split;
    if (input.isEmpty()) {
        return split;
    }

    int firstLineEnd = 0;
    while (firstLineEnd < input.size() && !isLineBreakChar(input.at(firstLineEnd))) {
        ++firstLineEnd;
    }
    const int protectedPrefixLength = protectedPrefixLengthForSelectionStart(input.left(firstLineEnd));

    int lastLineStart = input.size();
    while (lastLineStart > 0 && !isLineBreakChar(input.at(lastLineStart - 1))) {
        --lastLineStart;
    }
    const int protectedSuffixStart = lastLineStart + protectedSuffixStartForSelectionEnd(input.mid(lastLineStart));

    if (protectedPrefixLength >= protectedSuffixStart) {
        split.prefix = input;
        return split;
    }

    split.prefix = input.left(protectedPrefixLength);
    split.core = input.mid(protectedPrefixLength, protectedSuffixStart - protectedPrefixLength);
    split.suffix = input.mid(protectedSuffixStart);
    return split;
}

QString rewriteSelectionCore(const SelectionEdgeSplit& split, const std::function<QString(const QString&)>& rewriteCore)
{
    if (!rewriteCore) {
        return split.prefix + split.core + split.suffix;
    }

    QString rewritten;
    rewritten.reserve(split.prefix.size() + split.core.size() + split.suffix.size() + 32);
    rewritten.append(split.prefix);
    rewritten.append(rewriteCore(split.core));
    rewritten.append(split.suffix);
    return rewritten;
}

QString rewriteSelectionCore(const QString& input, const std::function<QString(const QString&)>& rewriteCore)
{
    return rewriteSelectionCore(splitSelectionEdges(input), rewriteCore);
}

bool parsePositiveIntegerText(const QString& text, int* value)
{
    if (text.isEmpty()) {
        return false;
    }
    int parsed = 0;
    for (QChar ch : text) {
        if (!ch.isDigit()) {
            return false;
        }
        const int digit = ch.digitValue();
        if (parsed > (std::numeric_limits<int>::max() - digit) / 10) {
            return false;
        }
        parsed = parsed * 10 + digit;
    }
    if (parsed <= 0) {
        return false;
    }
    if (value != nullptr) {
        *value = parsed;
    }
    return true;
}

bool isSubdivisionSignature(const QString& text, int* denominator)
{
    if (!text.startsWith(QLatin1Char('{')) || !text.endsWith(QLatin1Char('}'))) {
        return false;
    }
    return parsePositiveIntegerText(text.mid(1, text.size() - 2), denominator);
}

QString raiseSubdivisionCore(const QString& input, int* changedCount)
{
    int changed = 0;
    QString output;
    output.reserve(input.size() * 2);

    const QStringList lines = input.split('\n', Qt::KeepEmptyParts);
    for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex) {
        const QString& line = lines.at(lineIndex);
        for (int i = 0; i < line.size(); ++i) {
            const QChar ch = line.at(i);
            if (ch == QChar('|') && i + 1 < line.size() && line.at(i + 1) == QChar('|')) {
                output.append(line.mid(i));
                break;
            }
            if (ch == QChar('(')) {
                const int close = line.indexOf(')', i + 1);
                if (close < 0) {
                    output.append(line.mid(i));
                    break;
                }
                output.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            if (ch == QChar('[')) {
                const int close = line.indexOf(']', i + 1);
                if (close < 0) {
                    output.append(line.mid(i));
                    break;
                }
                output.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            if (ch == QChar('{')) {
                const int close = line.indexOf('}', i + 1);
                if (close < 0) {
                    output.append(line.mid(i));
                    break;
                }
                int denominator = 0;
                const QString signature = line.mid(i, close - i + 1);
                if (isSubdivisionSignature(signature, &denominator)
                    && denominator <= (std::numeric_limits<int>::max() / 2)) {
                    output.append(QStringLiteral("{%1}").arg(denominator * 2));
                    ++changed;
                } else {
                    output.append(signature);
                }
                i = close;
                continue;
            }
            if (ch == QChar('<') && line.mid(i, 4) == QStringLiteral("<HS*")) {
                const int close = line.indexOf('>', i + 4);
                if (close < 0) {
                    output.append(line.mid(i));
                    break;
                }
                output.append(line.mid(i, close - i + 1));
                i = close;
                continue;
            }
            output.append(ch);
            if (ch == QChar(',')) {
                output.append(ch);
                ++changed;
            }
        }
        if (lineIndex + 1 < lines.size()) {
            output.append('\n');
        }
    }

    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

bool validateLowerSubdivisionChunk(const QString& chunk)
{
    int slot = 0;
    bool slotHasContent = false;
    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar('|') && i + 1 < chunk.size() && chunk.at(i + 1) == QChar('|')) {
            const int lineEnd = chunk.indexOf(QChar('\n'), i + 2);
            if (lineEnd < 0) {
                break;
            }
            i = lineEnd;
            continue;
        }
        if (ch == QChar('(')) {
            const int close = chunk.indexOf(')', i + 1);
            if (close < 0) {
                if ((slot % 2) != 0) {
                    return false;
                }
                break;
            }
            i = close;
            continue;
        }
        if (ch == QChar('[')) {
            const int close = chunk.indexOf(']', i + 1);
            if (close < 0) {
                if ((slot % 2) != 0) {
                    return false;
                }
                break;
            }
            if (!slotHasContent && (slot % 2) != 0) {
                return false;
            }
            slotHasContent = true;
            i = close;
            continue;
        }
        if (ch == QChar('{')) {
            const int close = chunk.indexOf('}', i + 1);
            if (close < 0) {
                if ((slot % 2) != 0) {
                    return false;
                }
                break;
            }
            int denominator = 0;
            if (isSubdivisionSignature(chunk.mid(i, close - i + 1), &denominator)
                && ((denominator % 2) != 0 || denominator <= 1)) {
                return false;
            }
            i = close;
            continue;
        }
        if (ch == QChar('<') && chunk.mid(i, 4) == QStringLiteral("<HS*")) {
            const int close = chunk.indexOf('>', i + 4);
            if (close < 0) {
                if ((slot % 2) != 0) {
                    return false;
                }
                break;
            }
            if (!slotHasContent && (slot % 2) != 0) {
                return false;
            }
            slotHasContent = true;
            i = close;
            continue;
        }
        if (ch == QChar(',')) {
            if (slotHasContent && (slot % 2) != 0) {
                return false;
            }
            ++slot;
            slotHasContent = false;
            continue;
        }
        if (!ch.isSpace()) {
            if ((slot % 2) != 0) {
                return false;
            }
            slotHasContent = true;
        }
    }
    return !slotHasContent || ((slot % 2) == 0);
}

QString lowerSubdivisionChunk(const QString& chunk, int* changed)
{
    QString output;
    output.reserve(chunk.size());
    int slot = 0;
    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar('|') && i + 1 < chunk.size() && chunk.at(i + 1) == QChar('|')) {
            const int lineEnd = chunk.indexOf(QChar('\n'), i + 2);
            if (lineEnd < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, lineEnd - i + 1));
            i = lineEnd;
            continue;
        }
        if (ch == QChar('(')) {
            const int close = chunk.indexOf(')', i + 1);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, close - i + 1));
            i = close;
            continue;
        }
        if (ch == QChar('[')) {
            const int close = chunk.indexOf(']', i + 1);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, close - i + 1));
            i = close;
            continue;
        }
        if (ch == QChar('{')) {
            const int close = chunk.indexOf('}', i + 1);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            int denominator = 0;
            const QString signature = chunk.mid(i, close - i + 1);
            if (isSubdivisionSignature(signature, &denominator)) {
                output.append(QStringLiteral("{%1}").arg(denominator / 2));
                if (changed != nullptr) {
                    ++(*changed);
                }
            } else {
                output.append(signature);
            }
            i = close;
            continue;
        }
        if (ch == QChar('<') && chunk.mid(i, 4) == QStringLiteral("<HS*")) {
            const int close = chunk.indexOf('>', i + 4);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, close - i + 1));
            i = close;
            continue;
        }
        if (ch == QChar(',')) {
            ++slot;
            if ((slot % 2) == 0) {
                output.append(ch);
            } else if (changed != nullptr) {
                ++(*changed);
            }
            continue;
        }
        output.append(ch);
    }
    return output;
}

QStringList splitSubdivisionChunks(const QString& input)
{
    QStringList chunks;
    QString chunk;
    chunk.reserve(input.size());
    const auto flushChunk = [&]() {
        if (!chunk.isEmpty()) {
            chunks.append(chunk);
            chunk.clear();
        }
    };

    for (int i = 0; i < input.size(); ++i) {
        const QChar ch = input.at(i);
        if (ch == QChar('|') && i + 1 < input.size() && input.at(i + 1) == QChar('|')) {
            const int lineEnd = input.indexOf(QChar('\n'), i + 2);
            if (lineEnd < 0) {
                chunk.append(input.mid(i));
                break;
            }
            chunk.append(input.mid(i, lineEnd - i + 1));
            i = lineEnd;
            continue;
        }
        if (ch == QChar('(')) {
            const int close = input.indexOf(QChar(')'), i + 1);
            if (close < 0) {
                chunk.append(input.mid(i));
                break;
            }
            chunk.append(input.mid(i, close - i + 1));
            i = close;
            continue;
        }
        if (ch == QChar('[')) {
            const int close = input.indexOf(QChar(']'), i + 1);
            if (close < 0) {
                chunk.append(input.mid(i));
                break;
            }
            chunk.append(input.mid(i, close - i + 1));
            i = close;
            continue;
        }
        if (ch == QChar('{')) {
            const int close = input.indexOf(QChar('}'), i + 1);
            if (close < 0) {
                chunk.append(input.mid(i));
                break;
            }
            const QString signature = input.mid(i, close - i + 1);
            int denominator = 0;
            if (isSubdivisionSignature(signature, &denominator)) {
                flushChunk();
            }
            chunk.append(signature);
            i = close;
            continue;
        }
        if (ch == QChar('<') && input.mid(i, 4) == QStringLiteral("<HS*")) {
            const int close = input.indexOf(QChar('>'), i + 4);
            if (close < 0) {
                chunk.append(input.mid(i));
                break;
            }
            chunk.append(input.mid(i, close - i + 1));
            i = close;
            continue;
        }
        chunk.append(ch);
    }
    flushChunk();
    return chunks;
}

QString lowerSubdivisionCore(const QString& input, int* changedCount)
{
    int changed = 0;
    QString output;
    output.reserve(input.size());
    const QStringList chunks = splitSubdivisionChunks(input);
    for (const QString& chunk : chunks) {
        if (validateLowerSubdivisionChunk(chunk)) {
            output.append(lowerSubdivisionChunk(chunk, &changed));
        } else {
            output.append(chunk);
        }
    }
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

bool validateRaiseSubdivisionHalfStepChunk(const QString& chunk)
{
    int slot = 0;
    bool slotHasContent = false;
    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar('|') && i + 1 < chunk.size() && chunk.at(i + 1) == QChar('|')) {
            const int lineEnd = chunk.indexOf(QChar('\n'), i + 2);
            if (lineEnd < 0) {
                break;
            }
            i = lineEnd;
            continue;
        }
        if (ch == QChar('(')) {
            const int close = chunk.indexOf(')', i + 1);
            if (close < 0) {
                if ((slot % 2) != 0) {
                    return false;
                }
                break;
            }
            i = close;
            continue;
        }
        if (ch == QChar('[')) {
            const int close = chunk.indexOf(']', i + 1);
            if (close < 0) {
                if ((slot % 2) != 0) {
                    return false;
                }
                break;
            }
            if (!slotHasContent && (slot % 2) != 0) {
                return false;
            }
            slotHasContent = true;
            i = close;
            continue;
        }
        if (ch == QChar('{')) {
            const int close = chunk.indexOf('}', i + 1);
            if (close < 0) {
                if ((slot % 2) != 0) {
                    return false;
                }
                break;
            }
            int denominator = 0;
            if (isSubdivisionSignature(chunk.mid(i, close - i + 1), &denominator)
                && ((denominator % 2) != 0 || denominator > (std::numeric_limits<int>::max() / 3))) {
                return false;
            }
            i = close;
            continue;
        }
        if (ch == QChar('<') && chunk.mid(i, 4) == QStringLiteral("<HS*")) {
            const int close = chunk.indexOf('>', i + 4);
            if (close < 0) {
                if ((slot % 2) != 0) {
                    return false;
                }
                break;
            }
            if (!slotHasContent && (slot % 2) != 0) {
                return false;
            }
            slotHasContent = true;
            i = close;
            continue;
        }
        if (ch == QChar(',')) {
            if (slotHasContent && (slot % 2) != 0) {
                return false;
            }
            ++slot;
            slotHasContent = false;
            continue;
        }
        if (!ch.isSpace()) {
            if ((slot % 2) != 0) {
                return false;
            }
            slotHasContent = true;
        }
    }
    return !slotHasContent || ((slot % 2) == 0);
}

QString raiseSubdivisionHalfStepChunk(const QString& chunk, int* changed)
{
    QString output;
    output.reserve(chunk.size() * 3 / 2);
    int commaRun = 0;
    const auto flushCommas = [&]() {
        for (int i = 0; i < commaRun; ++i) {
            output.append(QChar(','));
        }
        for (int i = 0; i < commaRun / 2; ++i) {
            output.append(QChar(','));
            if (changed != nullptr) {
                ++(*changed);
            }
        }
        commaRun = 0;
    };

    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar(',')) {
            ++commaRun;
            continue;
        }
        flushCommas();
        if (ch == QChar('|') && i + 1 < chunk.size() && chunk.at(i + 1) == QChar('|')) {
            output.append(chunk.mid(i));
            break;
        }
        if (ch == QChar('(')) {
            const int close = chunk.indexOf(')', i + 1);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, close - i + 1));
            i = close;
            continue;
        }
        if (ch == QChar('[')) {
            const int close = chunk.indexOf(']', i + 1);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, close - i + 1));
            i = close;
            continue;
        }
        if (ch == QChar('{')) {
            const int close = chunk.indexOf('}', i + 1);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            int denominator = 0;
            const QString signature = chunk.mid(i, close - i + 1);
            if (isSubdivisionSignature(signature, &denominator)
                && (denominator % 2) == 0
                && denominator <= (std::numeric_limits<int>::max() / 3)) {
                output.append(QStringLiteral("{%1}").arg((denominator / 2) * 3));
                if (changed != nullptr) {
                    ++(*changed);
                }
            } else {
                output.append(signature);
            }
            i = close;
            continue;
        }
        if (ch == QChar('<') && chunk.mid(i, 4) == QStringLiteral("<HS*")) {
            const int close = chunk.indexOf('>', i + 4);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, close - i + 1));
            i = close;
            continue;
        }
        output.append(ch);
    }
    flushCommas();
    return output;
}

bool validateLowerSubdivisionHalfStepChunk(const QString& chunk)
{
    int slot = 0;
    bool slotHasContent = false;
    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar('|') && i + 1 < chunk.size() && chunk.at(i + 1) == QChar('|')) {
            const int lineEnd = chunk.indexOf(QChar('\n'), i + 2);
            if (lineEnd < 0) {
                break;
            }
            i = lineEnd;
            continue;
        }
        if (ch == QChar('(')) {
            const int close = chunk.indexOf(')', i + 1);
            if (close < 0) {
                if ((slot % 3) != 0) {
                    return false;
                }
                break;
            }
            i = close;
            continue;
        }
        if (ch == QChar('[')) {
            const int close = chunk.indexOf(']', i + 1);
            if (close < 0) {
                if ((slot % 3) != 0) {
                    return false;
                }
                break;
            }
            if (!slotHasContent && (slot % 3) != 0) {
                return false;
            }
            slotHasContent = true;
            i = close;
            continue;
        }
        if (ch == QChar('{')) {
            const int close = chunk.indexOf('}', i + 1);
            if (close < 0) {
                if ((slot % 3) != 0) {
                    return false;
                }
                break;
            }
            int denominator = 0;
            if (isSubdivisionSignature(chunk.mid(i, close - i + 1), &denominator)
                && ((denominator % 3) != 0 || denominator <= 1)) {
                return false;
            }
            i = close;
            continue;
        }
        if (ch == QChar('<') && chunk.mid(i, 4) == QStringLiteral("<HS*")) {
            const int close = chunk.indexOf('>', i + 4);
            if (close < 0) {
                if ((slot % 3) != 0) {
                    return false;
                }
                break;
            }
            if (!slotHasContent && (slot % 3) != 0) {
                return false;
            }
            slotHasContent = true;
            i = close;
            continue;
        }
        if (ch == QChar(',')) {
            if (slotHasContent && (slot % 3) != 0) {
                return false;
            }
            ++slot;
            slotHasContent = false;
            continue;
        }
        if (!ch.isSpace()) {
            if ((slot % 3) != 0) {
                return false;
            }
            slotHasContent = true;
        }
    }
    return !slotHasContent || ((slot % 3) == 0);
}

QString lowerSubdivisionHalfStepChunk(const QString& chunk, int* changed)
{
    QString output;
    output.reserve(chunk.size());
    int commaRun = 0;
    const auto flushCommas = [&]() {
        for (int i = 0; i < commaRun; ++i) {
            if ((i % 3) != 2) {
                output.append(QChar(','));
            } else if (changed != nullptr) {
                ++(*changed);
            }
        }
        commaRun = 0;
    };

    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar(',')) {
            ++commaRun;
            continue;
        }
        flushCommas();
        if (ch == QChar('|') && i + 1 < chunk.size() && chunk.at(i + 1) == QChar('|')) {
            output.append(chunk.mid(i));
            break;
        }
        if (ch == QChar('(')) {
            const int close = chunk.indexOf(')', i + 1);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, close - i + 1));
            i = close;
            continue;
        }
        if (ch == QChar('[')) {
            const int close = chunk.indexOf(']', i + 1);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, close - i + 1));
            i = close;
            continue;
        }
        if (ch == QChar('{')) {
            const int close = chunk.indexOf('}', i + 1);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            int denominator = 0;
            const QString signature = chunk.mid(i, close - i + 1);
            if (isSubdivisionSignature(signature, &denominator)) {
                output.append(QStringLiteral("{%1}").arg((denominator / 3) * 2));
                if (changed != nullptr) {
                    ++(*changed);
                }
            } else {
                output.append(signature);
            }
            i = close;
            continue;
        }
        if (ch == QChar('<') && chunk.mid(i, 4) == QStringLiteral("<HS*")) {
            const int close = chunk.indexOf('>', i + 4);
            if (close < 0) {
                output.append(chunk.mid(i));
                break;
            }
            output.append(chunk.mid(i, close - i + 1));
            i = close;
            continue;
        }
        output.append(ch);
    }
    flushCommas();
    return output;
}

QString raiseSubdivisionHalfStepCore(const QString& input, int* changedCount)
{
    int changed = 0;
    QString output;
    output.reserve(input.size() * 3 / 2);
    const QStringList chunks = splitSubdivisionChunks(input);
    for (const QString& chunk : chunks) {
        if (validateRaiseSubdivisionHalfStepChunk(chunk)) {
            output.append(raiseSubdivisionHalfStepChunk(chunk, &changed));
        } else {
            output.append(chunk);
        }
    }
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString lowerSubdivisionHalfStepCore(const QString& input, int* changedCount)
{
    int changed = 0;
    QString output;
    output.reserve(input.size());
    const QStringList chunks = splitSubdivisionChunks(input);
    for (const QString& chunk : chunks) {
        if (validateLowerSubdivisionHalfStepChunk(chunk)) {
            output.append(lowerSubdivisionHalfStepChunk(chunk, &changed));
        } else {
            output.append(chunk);
        }
    }
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
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

}  // namespace

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

QString raiseSubdivisionForSelection(const QString& input, int* changedCount)
{
    MC_OP("miacode::chart_transform::raiseSubdivisionForSelection");
    int changed = 0;
    const QString output = rewriteSelectionCore(input, [&](const QString& core) {
        return raiseSubdivisionCore(core, &changed);
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString lowerSubdivisionForSelection(const QString& input, int* changedCount)
{
    MC_OP("miacode::chart_transform::lowerSubdivisionForSelection");
    int changed = 0;
    const QString output = rewriteSelectionCore(input, [&](const QString& core) {
        return lowerSubdivisionCore(core, &changed);
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString raiseSubdivisionHalfStepForSelection(const QString& input, int* changedCount)
{
    MC_OP("miacode::chart_transform::raiseSubdivisionHalfStepForSelection");
    int changed = 0;
    const QString output = rewriteSelectionCore(input, [&](const QString& core) {
        return raiseSubdivisionHalfStepCore(core, &changed);
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

QString lowerSubdivisionHalfStepForSelection(const QString& input, int* changedCount)
{
    MC_OP("miacode::chart_transform::lowerSubdivisionHalfStepForSelection");
    int changed = 0;
    const QString output = rewriteSelectionCore(input, [&](const QString& core) {
        return lowerSubdivisionHalfStepCore(core, &changed);
    });
    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    return output;
}

}  // namespace miacode::chart_transform
