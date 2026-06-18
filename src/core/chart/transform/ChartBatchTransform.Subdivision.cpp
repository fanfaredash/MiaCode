#include "ChartBatchTransform.h"
#include "ChartBatchTransform.Internal.h"

#include <limits>

#include <QStringList>

namespace miacode::chart_transform::detail {

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

QString raiseSubdivisionHalfStepChunk(const QString& chunk, bool tripleFallback, int* changed)
{
    QString output;
    output.reserve(chunk.size() * 3);
    int commaRun = 0;
    const auto flushCommas = [&]() {
        for (int i = 0; i < commaRun; ++i) {
            output.append(QChar(','));
        }
        // A lossless x1.5 stretches every two comma slots into three (+commaRun/2). When the grid
        // cannot be halved we instead stretch every slot into three (+commaRun*2) -- a plain x3 --
        // so each surviving note still keeps its position in time.
        const int insertedCommas = tripleFallback ? (commaRun * 2) : (commaRun / 2);
        for (int i = 0; i < insertedCommas; ++i) {
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
                && denominator <= (std::numeric_limits<int>::max() / 3)
                && (tripleFallback || (denominator % 2) == 0)) {
                const int raised = tripleFallback ? (denominator * 3) : ((denominator / 2) * 3);
                output.append(QStringLiteral("{%1}").arg(raised));
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

bool leadingSubdivisionDenominator(const QString& chunk, int* denominator)
{
    if (chunk.isEmpty() || chunk.at(0) != QChar('{')) {
        return false;
    }
    const int close = chunk.indexOf(QChar('}'), 1);
    if (close < 0) {
        return false;
    }
    return isSubdivisionSignature(chunk.mid(0, close + 1), denominator);
}

QString raiseSubdivisionHalfStepCore(const QString& input, int* changedCount)
{
    int changed = 0;
    QString output;
    output.reserve(input.size() * 3);
    const QStringList chunks = splitSubdivisionChunks(input);
    for (const QString& chunk : chunks) {
        if (validateRaiseSubdivisionHalfStepChunk(chunk)) {
            // Lossless x1.5: an even subdivision whose occupied slots are all even-aligned.
            output.append(raiseSubdivisionHalfStepChunk(chunk, /*tripleFallback=*/false, &changed));
            continue;
        }
        // x1.5 is not representable losslessly (odd subdivision, or notes on odd slots). Triple
        // instead -- always lossless -- so the grid still refines. Realizes "first x3; halve when
        // possible, otherwise keep the x3 state".
        int denominator = 0;
        if (leadingSubdivisionDenominator(chunk, &denominator)
            && denominator <= (std::numeric_limits<int>::max() / 3)) {
            output.append(raiseSubdivisionHalfStepChunk(chunk, /*tripleFallback=*/true, &changed));
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

}  // namespace miacode::chart_transform::detail

namespace miacode::chart_transform {

using namespace detail;

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
