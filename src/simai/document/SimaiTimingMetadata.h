#pragma once

#include <QString>
#include <QVector>

#include "SimaiDocument.h"

namespace miacode::simai {

constexpr int kDefaultWholeTimeSignatureNumerator = 4;
constexpr int kDefaultWholeTimeSignatureDenominator = 4;

struct SimaiTimingMetadata {
    QString wholeTimeSignatureText;
    int wholeTimeSignatureNumerator = kDefaultWholeTimeSignatureNumerator;
    int wholeTimeSignatureDenominator = kDefaultWholeTimeSignatureDenominator;
    bool wholeTimeSignatureValid = false;
};

bool operator==(const SimaiTimingMetadata& lhs, const SimaiTimingMetadata& rhs);
bool operator!=(const SimaiTimingMetadata& lhs, const SimaiTimingMetadata& rhs);

bool parseTimeSignatureText(
    const QString& text,
    int* numerator,
    int* denominator,
    QString* normalizedText = nullptr);

bool parseInlineTimeSignatureComment(
    const QString& line,
    int commentStartIndex,
    int* numerator,
    int* denominator,
    QString* normalizedText = nullptr);

SimaiTimingMetadata buildTimingMetadata(const QVector<SimaiRawField>& fields);
SimaiTimingMetadata buildTimingMetadata(const SimaiDocument& document);
SimaiTimingMetadata buildTimingMetadataFromRawText(const QString& text, bool prefixDummyIfNeeded = true);
QString latencyMeterIdForTimingMetadata(const SimaiTimingMetadata& metadata);

}  // namespace miacode::simai
