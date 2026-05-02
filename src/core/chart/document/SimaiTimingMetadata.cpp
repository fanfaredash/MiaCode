#include "SimaiTimingMetadata.h"

#include <QRegularExpression>

namespace miacode::simai {
namespace {

QString normalizedTimeSignatureText(int numerator, int denominator)
{
    return QStringLiteral("%1/%2").arg(numerator).arg(denominator);
}

void skipInlineCommentSpaces(const QString& text, int* index)
{
    if (index == nullptr) {
        return;
    }
    while (*index < text.size() && text.at(*index).isSpace()) {
        ++(*index);
    }
}

}  // namespace

bool operator==(const SimaiTimingMetadata& lhs, const SimaiTimingMetadata& rhs)
{
    return lhs.wholeTimeSignatureText == rhs.wholeTimeSignatureText
        && lhs.wholeTimeSignatureNumerator == rhs.wholeTimeSignatureNumerator
        && lhs.wholeTimeSignatureDenominator == rhs.wholeTimeSignatureDenominator
        && lhs.wholeTimeSignatureValid == rhs.wholeTimeSignatureValid;
}

bool operator!=(const SimaiTimingMetadata& lhs, const SimaiTimingMetadata& rhs)
{
    return !(lhs == rhs);
}

bool parseTimeSignatureText(
    const QString& text,
    int* numerator,
    int* denominator,
    QString* normalizedText)
{
    static const QRegularExpression kPattern(QStringLiteral(R"(^\s*(\d+)\s*/\s*(\d+)\s*$)"));
    const QRegularExpressionMatch match = kPattern.match(text);
    if (!match.hasMatch()) {
        return false;
    }

    bool numeratorOk = false;
    bool denominatorOk = false;
    const int parsedNumerator = match.captured(1).toInt(&numeratorOk);
    const int parsedDenominator = match.captured(2).toInt(&denominatorOk);
    if (!numeratorOk || !denominatorOk || parsedNumerator <= 0 || parsedDenominator <= 0) {
        return false;
    }

    if (numerator != nullptr) {
        *numerator = parsedNumerator;
    }
    if (denominator != nullptr) {
        *denominator = parsedDenominator;
    }
    if (normalizedText != nullptr) {
        *normalizedText = normalizedTimeSignatureText(parsedNumerator, parsedDenominator);
    }
    return true;
}

bool parseInlineTimeSignatureComment(
    const QString& line,
    int commentStartIndex,
    int* numerator,
    int* denominator,
    QString* normalizedText)
{
    if (commentStartIndex < 0
        || commentStartIndex + 1 >= line.size()
        || line.at(commentStartIndex) != QLatin1Char('|')
        || line.at(commentStartIndex + 1) != QLatin1Char('|')) {
        return false;
    }

    int index = commentStartIndex;
    while (index + 1 < line.size()
           && line.at(index) == QLatin1Char('|')
           && line.at(index + 1) == QLatin1Char('|')) {
        index += 2;
        skipInlineCommentSpaces(line, &index);
        if (index + 1 >= line.size()
            || line.at(index) != QLatin1Char('|')
            || line.at(index + 1) != QLatin1Char('|')) {
            break;
        }
    }

    QString parsedText;
    if (!parseTimeSignatureText(line.mid(index), numerator, denominator, &parsedText)) {
        return false;
    }
    if (normalizedText != nullptr) {
        *normalizedText = parsedText;
    }
    return true;
}

SimaiTimingMetadata buildTimingMetadata(const QVector<SimaiRawField>& fields)
{
    SimaiTimingMetadata metadata;
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("whole_time_signature"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        metadata.wholeTimeSignatureText = field.value.trimmed();
        QString normalizedText;
        if (parseTimeSignatureText(
                field.value,
                &metadata.wholeTimeSignatureNumerator,
                &metadata.wholeTimeSignatureDenominator,
                &normalizedText)) {
            metadata.wholeTimeSignatureText = normalizedText;
            metadata.wholeTimeSignatureValid = true;
        }
        return metadata;
    }
    return metadata;
}

SimaiTimingMetadata buildTimingMetadata(const SimaiDocument& document)
{
    return buildTimingMetadata(document.extraFields);
}

SimaiTimingMetadata buildTimingMetadataFromRawText(const QString& text, bool prefixDummyIfNeeded)
{
    return buildTimingMetadata(SimaiDocument::parseRawFields(text, prefixDummyIfNeeded));
}

QString latencyMeterIdForTimingMetadata(const SimaiTimingMetadata& metadata)
{
    const QString effectiveText = metadata.wholeTimeSignatureValid
        ? metadata.wholeTimeSignatureText
        : QStringLiteral("4/4");
    if (effectiveText == QLatin1String("4/4")
        || effectiveText == QLatin1String("3/4")
        || effectiveText == QLatin1String("6/8")
        || effectiveText == QLatin1String("7/4")) {
        return effectiveText;
    }
    return QStringLiteral("auto");
}

}  // namespace miacode::simai
