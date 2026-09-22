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

int terminalMarkerStartForSelectionEnd(const QString& line)
{
    const int commentStart = commentStartIndexInLine(line);
    const int scanEnd = commentStart >= 0 ? commentStart : line.size();
    int tailStart = scanEnd;
    while (tailStart > 0 && line.at(tailStart - 1).isSpace()) {
        --tailStart;
    }
    int tokenStart = tailStart;
    while (tokenStart > 0 && !line.at(tokenStart - 1).isSpace()) {
        --tokenStart;
    }
    if (tokenStart < tailStart
        && line.mid(tokenStart, tailStart - tokenStart).compare(QStringLiteral("E"), Qt::CaseInsensitive) == 0) {
        return tokenStart;
    }
    return line.size();
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
    const QString lastLine = input.mid(lastLineStart);
    const int syntaxSuffixStart = protectedSuffixStartForSelectionEnd(lastLine);
    const int terminalSuffixStart = terminalMarkerStartForSelectionEnd(lastLine);
    const int protectedSuffixStart = lastLineStart + qMin(syntaxSuffixStart, terminalSuffixStart);

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

bool remainingChunkHasChartContent(const QString& chunk, int start)
{
    for (int i = start; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar('|') && i + 1 < chunk.size() && chunk.at(i + 1) == QChar('|')) {
            const int lineEnd = chunk.indexOf(QChar('\n'), i + 2);
            if (lineEnd < 0) {
                return false;
            }
            i = lineEnd;
            continue;
        }
        if (ch.isSpace()) {
            continue;
        }
        return true;
    }
    return false;
}

bool hasCompressibleCommaRun(const QString& chunk, int factor)
{
    int commaRun = 0;
    const auto flush = [&]() {
        const bool result = commaRun >= factor;
        commaRun = 0;
        return result;
    };
    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar(',')) {
            ++commaRun;
            continue;
        }
        if (flush()) {
            return true;
        }
        if (ch == QChar('|') && i + 1 < chunk.size() && chunk.at(i + 1) == QChar('|')) {
            const int lineEnd = chunk.indexOf(QChar('\n'), i + 2);
            if (lineEnd < 0) {
                break;
            }
            i = lineEnd;
            continue;
        }
        if (ch == QChar('(')) {
            const int close = chunk.indexOf(QChar(')'), i + 1);
            if (close < 0) {
                break;
            }
            i = close;
            continue;
        }
        if (ch == QChar('[')) {
            const int close = chunk.indexOf(QChar(']'), i + 1);
            if (close < 0) {
                break;
            }
            i = close;
            continue;
        }
        if (ch == QChar('{')) {
            const int close = chunk.indexOf(QChar('}'), i + 1);
            if (close < 0) {
                break;
            }
            i = close;
            continue;
        }
        if (ch == QChar('<') && chunk.mid(i, 4) == QStringLiteral("<HS*")) {
            const int close = chunk.indexOf('>', i + 4);
            if (close < 0) {
                break;
            }
            i = close;
            continue;
        }
    }
    return flush();
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
    if (!hasCompressibleCommaRun(chunk, 2)) {
        return chunk;
    }

    QString output;
    output.reserve(chunk.size());
    int commaRun = 0;
    int currentOriginalDenominator = 0;
    const auto appendChanged = [&](int count) {
        if (changed != nullptr) {
            *changed += count;
        }
    };
    const auto flushCommas = [&](bool preserveRemainder) {
        if (commaRun <= 0) {
            return;
        }
        const int compressed = commaRun / 2;
        const int remainder = commaRun % 2;
        for (int i = 0; i < compressed; ++i) {
            output.append(QChar(','));
        }
        if (preserveRemainder && remainder > 0 && currentOriginalDenominator > 0) {
            output.append(QStringLiteral("{%1}").arg(currentOriginalDenominator));
            for (int i = 0; i < remainder; ++i) {
                output.append(QChar(','));
            }
            appendChanged(commaRun - compressed - remainder);
        } else {
            appendChanged(commaRun - compressed);
        }
        commaRun = 0;
    };

    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar(',')) {
            ++commaRun;
            continue;
        }
        flushCommas(!remainingChunkHasChartContent(chunk, i));
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
                currentOriginalDenominator = denominator;
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
        output.append(ch);
    }
    flushCommas(true);
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
    if (!tripleFallback && !hasCompressibleCommaRun(chunk, 2)) {
        return chunk;
    }

    QString output;
    output.reserve(chunk.size() * 3);
    int commaRun = 0;
    int currentOriginalDenominator = 0;
    const auto flushCommas = [&](bool preserveRemainder) {
        if (commaRun <= 0) {
            return;
        }
        const int remainder = tripleFallback ? 0 : (commaRun % 2);
        const int fullSlots = commaRun - remainder;
        const int transformedSlots = tripleFallback ? (commaRun * 3) : (fullSlots + (fullSlots / 2));
        for (int i = 0; i < transformedSlots; ++i) {
            output.append(QChar(','));
        }
        if (!tripleFallback && remainder > 0) {
            if (preserveRemainder && currentOriginalDenominator > 0) {
                output.append(QStringLiteral("{%1}").arg(currentOriginalDenominator));
            }
            for (int i = 0; i < remainder; ++i) {
                output.append(QChar(','));
            }
        }
        const int insertedCommas = tripleFallback ? (commaRun * 2) : (fullSlots / 2);
        if (changed != nullptr) {
            *changed += insertedCommas;
        }
        commaRun = 0;
    };

    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar(',')) {
            ++commaRun;
            continue;
        }
        flushCommas(!remainingChunkHasChartContent(chunk, i));
        // A comment ends its line, not the chunk: the lines after it still
        // sit under the signature this chunk is rewriting.
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
            if (isSubdivisionSignature(signature, &denominator)
                && denominator <= (std::numeric_limits<int>::max() / 3)
                && (tripleFallback || (denominator % 2) == 0)) {
                currentOriginalDenominator = denominator;
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
    flushCommas(true);
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
    if (!hasCompressibleCommaRun(chunk, 3)) {
        return chunk;
    }

    QString output;
    output.reserve(chunk.size());
    int commaRun = 0;
    int currentOriginalDenominator = 0;
    const auto appendChanged = [&](int count) {
        if (changed != nullptr) {
            *changed += count;
        }
    };
    const auto flushCommas = [&](bool preserveRemainder) {
        if (commaRun <= 0) {
            return;
        }
        const int fullGroups = commaRun / 3;
        const int transformed = fullGroups * 2;
        const int remainder = commaRun % 3;
        for (int i = 0; i < transformed; ++i) {
            output.append(QChar(','));
        }
        if (preserveRemainder && remainder > 0 && currentOriginalDenominator > 0) {
            output.append(QStringLiteral("{%1}").arg(currentOriginalDenominator));
            for (int i = 0; i < remainder; ++i) {
                output.append(QChar(','));
            }
            appendChanged(commaRun - transformed - remainder);
        } else {
            appendChanged(commaRun - transformed);
        }
        commaRun = 0;
    };

    for (int i = 0; i < chunk.size(); ++i) {
        const QChar ch = chunk.at(i);
        if (ch == QChar(',')) {
            ++commaRun;
            continue;
        }
        flushCommas(!remainingChunkHasChartContent(chunk, i));
        // A comment ends its line, not the chunk: the lines after it still
        // sit under the signature this chunk is rewriting.
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
                currentOriginalDenominator = denominator;
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
    flushCommas(true);
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

bool lastSubdivisionDenominator(const QString& text, int* denominator)
{
    bool found = false;
    int last = 0;
    for (int i = 0; i < text.size(); ++i) {
        const QChar ch = text.at(i);
        if (ch == QChar('|') && i + 1 < text.size() && text.at(i + 1) == QChar('|')) {
            const int lineEnd = text.indexOf(QChar('\n'), i + 2);
            if (lineEnd < 0) {
                break;
            }
            i = lineEnd;
            continue;
        }
        if (ch == QChar('(')) {
            const int close = text.indexOf(QChar(')'), i + 1);
            if (close < 0) {
                break;
            }
            i = close;
            continue;
        }
        if (ch == QChar('[')) {
            const int close = text.indexOf(QChar(']'), i + 1);
            if (close < 0) {
                break;
            }
            i = close;
            continue;
        }
        if (ch != QChar('{')) {
            continue;
        }
        const int close = text.indexOf(QChar('}'), i + 1);
        if (close < 0) {
            break;
        }
        int current = 0;
        if (isSubdivisionSignature(text.mid(i, close - i + 1), &current)) {
            last = current;
            found = true;
        }
        i = close;
    }
    if (found && denominator != nullptr) {
        *denominator = last;
    }
    return found;
}

bool suffixStartsWithSubdivision(const QString& suffix)
{
    int i = 0;
    while (i < suffix.size() && suffix.at(i).isSpace()) {
        ++i;
    }
    if (i >= suffix.size() || suffix.at(i) != QChar('{')) {
        return false;
    }
    const int close = suffix.indexOf(QChar('}'), i + 1);
    if (close < 0) {
        return false;
    }
    int denominator = 0;
    return isSubdivisionSignature(suffix.mid(i, close - i + 1), &denominator);
}

bool suffixIsTerminalOnly(const QString& suffix)
{
    QString text;
    text.reserve(suffix.size());
    const QStringList lines = suffix.split(QChar('\n'), Qt::KeepEmptyParts);
    for (const QString& line : lines) {
        const int commentStart = commentStartIndexInLine(line);
        text += commentStart >= 0 ? line.left(commentStart) : line;
        text += QChar('\n');
    }
    const QString trimmed = text.trimmed();
    return trimmed.isEmpty() || trimmed.compare(QStringLiteral("E"), Qt::CaseInsensitive) == 0;
}

// `governingDenominator` is the {N} borrowed from before the selection, or 0
// when the selection brought its own; it is what a selection without any {N}
// of its own has to hand back to the text after it.
QString appendRestoreSubdivisionIfNeeded(
    const QString& originalSelection,
    const QString& transformedSelection,
    const QString& suffixContext,
    int changed,
    int governingDenominator)
{
    if (changed <= 0 || transformedSelection == originalSelection) {
        return transformedSelection;
    }
    int originalDenominator = 0;
    if (!lastSubdivisionDenominator(originalSelection, &originalDenominator)) {
        if (governingDenominator <= 0) {
            return transformedSelection;
        }
        originalDenominator = governingDenominator;
    }
    if (suffixIsTerminalOnly(suffixContext) || suffixStartsWithSubdivision(suffixContext)) {
        return transformedSelection;
    }
    return transformedSelection + QStringLiteral("{%1}").arg(originalDenominator);
}

// SimaiNativeParser starts every chart on {4}; a chart that never writes one is
// timed on it.
constexpr int kDefaultSubdivision = 4;

int governingSubdivisionDenominator(const QString& lead)
{
    int denominator = 0;
    return lastSubdivisionDenominator(lead, &denominator) ? denominator : kDefaultSubdivision;
}

// Where the {N} in force at the selection's start must be written so the step
// rewrites the selection's leading commas under it, or -1 when no comma comes
// before the selection's own first {N}. That is the core's start when the
// selection opens between slots, and otherwise the first comma: a signature
// cannot split the note the selection starts inside. A selection that opens
// inside a comment starts on the line after it, since a {N} written into the
// comment would be text.
int borrowedSubdivisionIndex(const QString& lead, const QString& core)
{
    const int leadLineStart = lead.lastIndexOf(QChar('\n')) + 1;
    const int leadComment = commentStartIndexInLine(lead.mid(leadLineStart));
    int scanStart = 0;
    QString leadCode = lead;
    if (leadComment >= 0) {
        const int commentEnd = core.indexOf(QChar('\n'));
        if (commentEnd < 0) {
            return -1;
        }
        scanStart = commentEnd + 1;
        leadCode = lead.left(leadLineStart + leadComment);
    }

    int firstComma = -1;
    for (int i = scanStart; i < core.size() && firstComma < 0; ++i) {
        const QChar ch = core.at(i);
        if (ch == QChar(',')) {
            firstComma = i;
            continue;
        }
        if (ch == QChar('|') && i + 1 < core.size() && core.at(i + 1) == QChar('|')) {
            const int lineEnd = core.indexOf(QChar('\n'), i + 2);
            if (lineEnd < 0) {
                return -1;
            }
            i = lineEnd;
            continue;
        }
        if (ch == QChar('(') || ch == QChar('[')) {
            const int close = core.indexOf(ch == QChar('(') ? QChar(')') : QChar(']'), i + 1);
            if (close < 0) {
                return -1;
            }
            i = close;
            continue;
        }
        if (ch == QChar('{')) {
            const int close = core.indexOf(QChar('}'), i + 1);
            int denominator = 0;
            if (close < 0 || isSubdivisionSignature(core.mid(i, close - i + 1), &denominator)) {
                return -1;
            }
            i = close;
            continue;
        }
        if (ch == QChar('<') && core.mid(i, 4) == QStringLiteral("<HS*")) {
            const int close = core.indexOf(QChar('>'), i + 4);
            if (close < 0) {
                return -1;
            }
            i = close;
        }
    }
    if (firstComma < 0) {
        return -1;
    }
    const QChar previous = leadCode.isEmpty() ? QChar(' ') : leadCode.back();
    const bool betweenSlots = previous.isSpace() || previous == QChar(',')
        || previous == QChar(')') || previous == QChar('}');
    return betweenSlots ? scanStart : firstComma;
}

// A subdivision step over a selection inside a chart. When the selection's
// leading commas are governed by a {N} written before it, that {N} is borrowed
// into the core so the step re-grids those commas instead of re-timing them,
// and handed back to the text after the selection like one of its own.
QString rewriteSubdivisionSelection(
    const QString& input,
    const SelectionContext& context,
    QString (*rewriteCore)(const QString&, int*),
    int* changedCount)
{
    const SelectionEdgeSplit split = splitSelectionEdges(input);
    const QString lead = context.before + split.prefix;
    const int borrowAt = borrowedSubdivisionIndex(lead, split.core);
    const int governing = borrowAt >= 0 ? governingSubdivisionDenominator(lead) : 0;
    const QString borrowed = borrowAt >= 0 ? QStringLiteral("{%1}").arg(governing) : QString();

    QString core = split.core;
    if (!borrowed.isEmpty()) {
        core.insert(borrowAt, borrowed);
    }
    int changed = 0;
    core = rewriteCore(core, &changed);
    // Every rewrite changes the number in a signature it steps, so a borrowed
    // {N} still in place means its run could not be stepped losslessly. That
    // run keeps its grid, and the signature was never needed.
    const bool borrowedKept = !borrowed.isEmpty() && core.mid(borrowAt, borrowed.size()) != borrowed;
    if (!borrowed.isEmpty() && !borrowedKept) {
        core.remove(borrowAt, borrowed.size());
    }

    if (changedCount != nullptr) {
        *changedCount = changed;
    }
    const QString output = split.prefix + core + split.suffix;
    return appendRestoreSubdivisionIfNeeded(
        input, output, context.after, changed, borrowedKept ? governing : 0);
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

QString raiseSubdivisionForSelection(const QString& input, const SelectionContext& context, int* changedCount)
{
    MC_OP("miacode::chart_transform::raiseSubdivisionForSelection");
    return rewriteSubdivisionSelection(input, context, &raiseSubdivisionCore, changedCount);
}

QString lowerSubdivisionForSelection(const QString& input, const SelectionContext& context, int* changedCount)
{
    MC_OP("miacode::chart_transform::lowerSubdivisionForSelection");
    return rewriteSubdivisionSelection(input, context, &lowerSubdivisionCore, changedCount);
}

QString raiseSubdivisionHalfStepForSelection(const QString& input, const SelectionContext& context, int* changedCount)
{
    MC_OP("miacode::chart_transform::raiseSubdivisionHalfStepForSelection");
    return rewriteSubdivisionSelection(input, context, &raiseSubdivisionHalfStepCore, changedCount);
}

QString lowerSubdivisionHalfStepForSelection(const QString& input, const SelectionContext& context, int* changedCount)
{
    MC_OP("miacode::chart_transform::lowerSubdivisionHalfStepForSelection");
    return rewriteSubdivisionSelection(input, context, &lowerSubdivisionHalfStepCore, changedCount);
}

}  // namespace miacode::chart_transform
