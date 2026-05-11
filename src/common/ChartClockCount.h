#pragma once

#include "core/chart/document/SimaiDocument.h"

#include <QRegularExpression>
#include <QtMath>

namespace miacode::chart_clock {

constexpr double kFallbackClockBpm = 120.0;

inline int clockCountFromFields(const QVector<SimaiRawField>& fields)
{
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("clock_count"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool ok = false;
        const int value = field.value.trimmed().toInt(&ok);
        return ok ? qMax(0, value) : 0;
    }
    return 0;
}

inline int clockCountFromDocument(const SimaiDocument& document)
{
    return clockCountFromFields(document.extraFields);
}

inline double wholeBpmFromFields(const QVector<SimaiRawField>& fields)
{
    for (const SimaiRawField& field : fields) {
        if (field.key.compare(QStringLiteral("wholebpm"), Qt::CaseInsensitive) != 0) {
            continue;
        }
        bool ok = false;
        const double value = field.value.trimmed().toDouble(&ok);
        return (ok && qIsFinite(value) && value > 0.0) ? value : 0.0;
    }
    return 0.0;
}

inline double firstBpmFromChart(const QString& chart)
{
    static const QRegularExpression bpmPattern(QStringLiteral(R"(\(\s*([0-9]+(?:\.[0-9]+)?)\s*\))"));
    QRegularExpressionMatchIterator it = bpmPattern.globalMatch(chart);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        bool ok = false;
        const double value = match.captured(1).toDouble(&ok);
        if (ok && qIsFinite(value) && value > 0.0) {
            return value;
        }
    }
    return 0.0;
}

inline double clockBpmForChart(const SimaiDocument& document, const QString& chart)
{
    const double wholeBpm = wholeBpmFromFields(document.extraFields);
    if (wholeBpm > 0.0) {
        return wholeBpm;
    }
    const double firstBpm = firstBpmFromChart(chart);
    return firstBpm > 0.0 ? firstBpm : kFallbackClockBpm;
}

}  // namespace miacode::chart_clock
