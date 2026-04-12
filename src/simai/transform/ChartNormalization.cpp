#include "ChartNormalization.h"

#include <algorithm>
#include <numeric>

#include <QtGlobal>

#include "simai/parser/SimaiNativeParser.h"

namespace miacode::chart_transform {
namespace {

constexpr double kNormalizationEpsilon = 1e-6;

enum class BoundaryItemKind {
    StandaloneText,
    Bpm,
    TimeSignature,
};

struct BoundaryItem {
    BoundaryItemKind kind = BoundaryItemKind::StandaloneText;
    QString text;
};

struct MeasureMoment {
    double positionWhole = 0.0;
    QString text;
};

struct MeasureBuilder {
    int meterNumerator = miacode::simai::kDefaultWholeTimeSignatureNumerator;
    int meterDenominator = miacode::simai::kDefaultWholeTimeSignatureDenominator;
    double startPhaseWhole = 0.0;
    QVector<BoundaryItem> leadingItems;
    QVector<BoundaryItem> trailingItems;
    QVector<MeasureMoment> moments;
};

struct RenderMeasure {
    int meterNumerator = miacode::simai::kDefaultWholeTimeSignatureNumerator;
    int meterDenominator = miacode::simai::kDefaultWholeTimeSignatureDenominator;
    double startPhaseWhole = 0.0;
    double lengthWhole = 0.0;
    QVector<BoundaryItem> leadingItems;
    QVector<BoundaryItem> trailingItems;
    QVector<MeasureMoment> moments;
};

bool nearlyEqual(double a, double b, double epsilon = kNormalizationEpsilon)
{
    return qAbs(a - b) <= epsilon;
}

bool isDigitLane(QChar ch)
{
    return ch >= QLatin1Char('1') && ch <= QLatin1Char('8');
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
    if (head == QLatin1Char('C')) {
        if (token.size() >= 2 && (token.at(1) == QLatin1Char('1') || token.at(1) == QLatin1Char('2'))) {
            return 2;
        }
        return 1;
    }
    if (token.size() >= 2
        && (head == QLatin1Char('A')
            || head == QLatin1Char('B')
            || head == QLatin1Char('D')
            || head == QLatin1Char('E'))
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
    QString extraModifiers;
    bool hasHold = false;
    bool hasBreak = false;
    bool hasEx = false;
    bool valid = false;
};

struct SlideTokenParts {
    QString coreWithoutTrackBreak;
    QString headExtraModifiers;
    bool headBreak = false;
    bool headEx = false;
    bool trackBreak = false;
    bool valid = false;
};

QString sortedModifierText(QString modifiers)
{
    std::sort(modifiers.begin(), modifiers.end(), [](QChar left, QChar right) {
        return left.unicode() < right.unicode();
    });
    return modifiers;
}

struct DurationNormalizationOptions {
    bool allowZeroDuration = false;
    bool omitZeroDurationBracket = false;
};

QString normalizeFractionDurationSignature(QString signature, const DurationNormalizationOptions& options)
{
    if (signature.contains(QLatin1Char('#'))) {
        return signature;
    }

    const int colon = signature.indexOf(QLatin1Char(':'));
    if (colon < 0 || signature.indexOf(QLatin1Char(':'), colon + 1) >= 0) {
        return signature;
    }

    bool beatsOk = false;
    bool numeratorOk = false;
    const int beats = signature.left(colon).trimmed().toInt(&beatsOk);
    const int numerator = signature.mid(colon + 1).trimmed().toInt(&numeratorOk);
    if (!beatsOk || !numeratorOk || beats <= 0 || numerator < 0) {
        return signature;
    }
    if (!options.allowZeroDuration && numerator == 0) {
        return signature;
    }

    qint64 gridUnits = 0;
    if (numerator > 0) {
        const qint64 scaledGridUnits = 384ll * static_cast<qint64>(numerator);
        gridUnits = (scaledGridUnits + (beats / 2)) / beats;
    }

    if (gridUnits <= 0) {
        if (!options.allowZeroDuration) {
            gridUnits = 1;
        } else if (options.omitZeroDurationBracket) {
            return QString();
        } else {
            return QStringLiteral("1:0");
        }
    }

    const qint64 gcd = std::gcd<qint64>(384ll, gridUnits);
    if (gcd <= 0) {
        return signature;
    }

    return QStringLiteral("%1:%2").arg(384ll / gcd).arg(gridUnits / gcd);
}

QString normalizeBracketDurationSignatures(const QString& text, const DurationNormalizationOptions& options)
{
    QString normalized;
    normalized.reserve(text.size());

    int cursor = 0;
    while (cursor < text.size()) {
        const int openBracket = text.indexOf(QLatin1Char('['), cursor);
        if (openBracket < 0) {
            normalized.append(text.mid(cursor));
            break;
        }

        normalized.append(text.mid(cursor, openBracket - cursor));
        const int closeBracket = text.indexOf(QLatin1Char(']'), openBracket + 1);
        if (closeBracket < 0) {
            normalized.append(text.mid(openBracket));
            break;
        }

        const QString normalizedSignature = normalizeFractionDurationSignature(
            text.mid(openBracket + 1, closeBracket - openBracket - 1),
            options);
        if (!normalizedSignature.isEmpty()) {
            normalized.append(QLatin1Char('['));
            normalized.append(normalizedSignature);
            normalized.append(QLatin1Char(']'));
        }
        cursor = closeBracket + 1;
    }

    return normalized;
}

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
    const int openBracket = suffix.indexOf(QLatin1Char('['));
    const int closeBracket = suffix.lastIndexOf(QLatin1Char(']'));
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
        if (lower == QLatin1Char('b')) {
            parts->hasBreak = true;
        } else if (lower == QLatin1Char('x')) {
            parts->hasEx = true;
        } else if (lower == QLatin1Char('f')) {
            parts->hasFirework = true;
        } else if (lower == QLatin1Char('h')) {
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

    const int openBracket = token.indexOf(QLatin1Char('['));
    const int closeBracket = token.lastIndexOf(QLatin1Char(']'));
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
        const QChar ch = core.at(i);
        const QChar lower = ch.toLower();
        if (lower == QLatin1Char('b')) {
            parts->hasBreak = true;
        } else if (lower == QLatin1Char('x')) {
            parts->hasEx = true;
        } else if (lower == QLatin1Char('h')) {
            parts->hasHold = true;
        } else if (!ch.isSpace()) {
            parts->extraModifiers.append(ch);
        }
    }
    if (!parts->bracketSuffix.isEmpty() && !parts->hasHold) {
        return false;
    }
    if (parts->bracketSuffix.isEmpty() && parts->hasHold) {
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
        if (lower == QLatin1Char('b')) {
            parts->headBreak = true;
            ++prefixLength;
            continue;
        }
        if (lower == QLatin1Char('x')) {
            parts->headEx = true;
            ++prefixLength;
            continue;
        }
        if (lower == QLatin1Char('h')) {
            return false;
        }
        if (ch == QLatin1Char('@') || ch == QLatin1Char('?') || ch == QLatin1Char('!')) {
            parts->headExtraModifiers.append(ch);
            ++prefixLength;
            continue;
        }
        break;
    }

    const QString remainder = token.mid(1 + prefixLength);
    if (remainder.isEmpty()) {
        return false;
    }

    const QString core = QString(token.at(0)) + remainder;
    parts->trackBreak = core.mid(1).contains(QLatin1Char('b'), Qt::CaseInsensitive);
    parts->coreWithoutTrackBreak.reserve(core.size());
    parts->coreWithoutTrackBreak.append(core.at(0));
    for (int i = 1; i < core.size(); ++i) {
        if (core.at(i).toLower() == QLatin1Char('b')) {
            continue;
        }
        parts->coreWithoutTrackBreak.append(core.at(i));
    }
    parts->valid = true;
    return true;
}

QString buildTouchToken(const TouchTokenParts& parts)
{
    QString token = parts.prefix;
    if (parts.hasBreak) {
        token.append(QLatin1Char('b'));
    }
    if (parts.hasEx) {
        token.append(QLatin1Char('x'));
    }
    if (parts.hasHold) {
        token.append(QLatin1Char('h'));
    }
    if (parts.hasFirework) {
        token.append(QLatin1Char('f'));
    }
    token.append(normalizeBracketDurationSignatures(
        parts.bracketSuffix,
        DurationNormalizationOptions{true, false}));
    return token;
}

QString buildNoteToken(const NoteTokenParts& parts)
{
    QString token;
    token.reserve(1 + parts.extraModifiers.size() + parts.bracketSuffix.size() + 3);
    token.append(parts.lane);
    token.append(sortedModifierText(parts.extraModifiers));
    if (parts.hasBreak) {
        token.append(QLatin1Char('b'));
    }
    if (parts.hasEx) {
        token.append(QLatin1Char('x'));
    }
    if (parts.hasHold) {
        token.append(QLatin1Char('h'));
    }
    token.append(normalizeBracketDurationSignatures(
        parts.bracketSuffix,
        DurationNormalizationOptions{true, true}));
    return token;
}

QString buildSlideToken(const SlideTokenParts& parts)
{
    const QString normalizedCoreWithoutTrackBreak = normalizeBracketDurationSignatures(
        parts.coreWithoutTrackBreak,
        DurationNormalizationOptions{false, false});
    QString token;
    token.reserve(normalizedCoreWithoutTrackBreak.size() + parts.headExtraModifiers.size() + 3);
    token.append(normalizedCoreWithoutTrackBreak.at(0));
    token.append(sortedModifierText(parts.headExtraModifiers));
    if (parts.headBreak) {
        token.append(QLatin1Char('b'));
    }
    if (parts.headEx) {
        token.append(QLatin1Char('x'));
    }
    QString remainder = normalizedCoreWithoutTrackBreak.mid(1);
    if (parts.trackBreak) {
        const int firstBracket = remainder.indexOf(QLatin1Char('['));
        if (firstBracket >= 0) {
            remainder.insert(firstBracket, QLatin1Char('b'));
        } else {
            remainder.append(QLatin1Char('b'));
        }
    }
    token.append(remainder);
    return token;
}

QString canonicalizeToken(const QString& token)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    TouchTokenParts touchParts;
    if (parseTouchTokenParts(trimmed, &touchParts) && touchParts.valid) {
        return buildTouchToken(touchParts);
    }

    NoteTokenParts noteParts;
    if (parseNoteTokenParts(trimmed, &noteParts) && noteParts.valid) {
        return buildNoteToken(noteParts);
    }

    SlideTokenParts slideParts;
    if (parseSlideTokenParts(trimmed, &slideParts) && slideParts.valid) {
        return buildSlideToken(slideParts);
    }

    return trimmed;
}

QString buildMomentText(const QVector<QStringList>& groups)
{
    QStringList renderedGroups;
    renderedGroups.reserve(groups.size());
    for (const QStringList& group : groups) {
        QStringList renderedTokens;
        renderedTokens.reserve(group.size());
        for (const QString& token : group) {
            if (!token.isEmpty()) {
                renderedTokens.append(token);
            }
        }
        if (!renderedTokens.isEmpty()) {
            renderedGroups.append(renderedTokens.join(QLatin1Char('/')));
        }
    }
    return renderedGroups.join(QLatin1Char('`'));
}

bool isTerminalMarkerText(QString text)
{
    text = text.trimmed();
    return text.compare(QStringLiteral("E"), Qt::CaseInsensitive) == 0;
}

bool lineTailIsTerminalMarker(const QString& line, int startIndex)
{
    if (startIndex < 0 || startIndex >= line.size()) {
        return false;
    }
    QString tail = line.mid(startIndex);
    const int commentIndex = tail.indexOf(QStringLiteral("||"));
    if (commentIndex >= 0) {
        tail = tail.left(commentIndex);
    }
    return isTerminalMarkerText(tail);
}

double measureLengthWhole(int meterNumerator, int meterDenominator)
{
    return static_cast<double>(qMax(1, meterNumerator)) / static_cast<double>(qMax(1, meterDenominator));
}

double normalizeMeasurePhaseWhole(double phaseWhole, int meterNumerator, int meterDenominator)
{
    const double measureLength = measureLengthWhole(meterNumerator, meterDenominator);
    if (measureLength <= kNormalizationEpsilon) {
        return 0.0;
    }
    while (phaseWhole + kNormalizationEpsilon >= measureLength) {
        phaseWhole -= measureLength;
    }
    return nearlyEqual(phaseWhole, 0.0) ? 0.0 : qMax(0.0, phaseWhole);
}

void appendBoundaryItems(QStringList* lines, const QVector<BoundaryItem>& items)
{
    if (lines == nullptr) {
        return;
    }
    for (int index = 0; index < items.size();) {
        const BoundaryItem& item = items.at(index);
        if (item.kind == BoundaryItemKind::Bpm || item.kind == BoundaryItemKind::TimeSignature) {
            QString bpmText;
            QString timeSignatureText;
            int consumed = 1;
            if (item.kind == BoundaryItemKind::Bpm) {
                bpmText = item.text;
            } else {
                timeSignatureText = item.text;
            }
            if (index + 1 < items.size()) {
                const BoundaryItem& next = items.at(index + 1);
                if ((item.kind == BoundaryItemKind::Bpm && next.kind == BoundaryItemKind::TimeSignature)
                    || (item.kind == BoundaryItemKind::TimeSignature && next.kind == BoundaryItemKind::Bpm)) {
                    if (next.kind == BoundaryItemKind::Bpm) {
                        bpmText = next.text;
                    } else {
                        timeSignatureText = next.text;
                    }
                    consumed = 2;
                }
            }
            if (!bpmText.isEmpty() && !timeSignatureText.isEmpty()) {
                lines->append(QStringLiteral("%1 %2").arg(bpmText, timeSignatureText));
            } else if (!bpmText.isEmpty()) {
                lines->append(bpmText);
            } else if (!timeSignatureText.isEmpty()) {
                lines->append(timeSignatureText);
            }
            index += consumed;
            continue;
        }
        lines->append(item.text);
        ++index;
    }
}

QVector<int> preferredSubdivisionBeats()
{
    QVector<int> beatsValues;
    for (int beats = 16; beats <= 384; ++beats) {
        if ((384 % beats) == 0) {
            beatsValues.append(beats);
        }
    }
    return beatsValues;
}

int chooseSubdivisionBeats(int measureGridLength, const QVector<int>& momentGrids)
{
    static const QVector<int> kPreferredBeats = preferredSubdivisionBeats();
    for (int beats : kPreferredBeats) {
        const int stepGrid = 384 / beats;
        if (stepGrid <= 0 || (measureGridLength % stepGrid) != 0) {
            continue;
        }
        bool fits = true;
        for (int grid : momentGrids) {
            if ((grid % stepGrid) != 0) {
                fits = false;
                break;
            }
        }
        if (fits) {
            return beats;
        }
    }
    return 384;
}

QString renderMeasureLine(const RenderMeasure& measure)
{
    const int measureGridLength = qMax(1, qRound(measure.lengthWhole * 384.0));
    const int startPhaseGrid = qMax(0, qRound(measure.startPhaseWhole * 384.0));
    const int endPhaseGrid = startPhaseGrid + measureGridLength;
    const int beatGrid = measure.meterDenominator > 0
        ? qMax(1, qRound(384.0 / static_cast<double>(measure.meterDenominator)))
        : 0;
    QVector<int> momentAbsoluteGrids;
    momentAbsoluteGrids.reserve(measure.moments.size());
    for (const MeasureMoment& moment : measure.moments) {
        const int relativeGrid = qBound(0, measureGridLength, qRound(moment.positionWhole * 384.0));
        momentAbsoluteGrids.append(startPhaseGrid + relativeGrid);
    }

    QString line;
    int lastSegmentBeats = 0;
    int segmentStartGrid = startPhaseGrid;
    while (segmentStartGrid < endPhaseGrid) {
        int segmentEndGrid = endPhaseGrid;
        if (beatGrid > 0) {
            const int nextBeatIndex = (segmentStartGrid / beatGrid) + 1;
            segmentEndGrid = qMin(endPhaseGrid, nextBeatIndex * beatGrid);
        }
        const int segmentLengthGrid = qMax(1, segmentEndGrid - segmentStartGrid);

        QVector<int> segmentMomentGrids;
        QVector<int> segmentMomentIndices;
        segmentMomentGrids.reserve(measure.moments.size());
        segmentMomentIndices.reserve(measure.moments.size());
        for (int index = 0; index < momentAbsoluteGrids.size(); ++index) {
            const int absoluteGrid = momentAbsoluteGrids.at(index);
            if (absoluteGrid < segmentStartGrid || absoluteGrid >= segmentEndGrid) {
                continue;
            }
            segmentMomentGrids.append(absoluteGrid - segmentStartGrid);
            segmentMomentIndices.append(index);
        }

        const int beats = chooseSubdivisionBeats(segmentLengthGrid, segmentMomentGrids);
        const int stepGrid = qMax(1, 384 / beats);
        const int slotCount = qMax(1, segmentLengthGrid / stepGrid);
        QVector<QString> slotTexts(slotCount);
        for (int index = 0; index < segmentMomentIndices.size(); ++index) {
            const int momentIndex = segmentMomentIndices.at(index);
            const int grid = segmentMomentGrids.at(index);
            const int slotIndex = qBound(0, slotCount - 1, grid / stepGrid);
            if (slotTexts.at(slotIndex).isEmpty()) {
                slotTexts[slotIndex] = measure.moments.at(momentIndex).text;
            } else if (!measure.moments.at(momentIndex).text.isEmpty()) {
                slotTexts[slotIndex].append(QLatin1Char('/'));
                slotTexts[slotIndex].append(measure.moments.at(momentIndex).text);
            }
        }

        if (line.isEmpty() || beats != lastSegmentBeats) {
            line.append(QStringLiteral("{%1}").arg(beats));
            lastSegmentBeats = beats;
        }
        for (const QString& slotText : slotTexts) {
            line.append(slotText);
            line.append(QLatin1Char(','));
        }
        if (segmentEndGrid < endPhaseGrid && beatGrid > 0 && (segmentEndGrid % beatGrid) == 0) {
            line.append(QLatin1Char(' '));
        }
        segmentStartGrid = segmentEndGrid;
    }
    return line.trimmed();
}

QString summarizeValidationError(const SimaiNativeValidationReport& report)
{
    if (!report.issues.isEmpty()) {
        const SimaiNativeValidationIssue& issue = report.issues.constFirst();
        return issue.displayMessage.isEmpty() ? issue.rawMessage : issue.displayMessage;
    }
    return QStringLiteral("Fix syntax errors before normalizing this chart.");
}

}  // namespace

ChartNormalizationResult normalizeChartText(
    const QString& input,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    ChartNormalizationResult result;

    const SimaiNativeValidationReport report = SimaiNativeParser::buildValidationReport(
        input,
        SimaiNativeValidationLocale::English,
        nullptr,
        timingMetadata);
    if (report.errorCount > 0) {
        result.errorMessage = summarizeValidationError(report);
        return result;
    }

    QVector<RenderMeasure> renderedMeasures;
    MeasureBuilder currentMeasure;
    currentMeasure.meterNumerator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureNumerator
        : miacode::simai::kDefaultWholeTimeSignatureNumerator;
    currentMeasure.meterDenominator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureDenominator
        : miacode::simai::kDefaultWholeTimeSignatureDenominator;

    double currentPositionWhole = 0.0;
    int currentBeats = 4;
    double currentBpm = 120.0;
    QString token;
    QStringList currentGroupTokens;
    QVector<QStringList> currentGroups;

    const auto flushToken = [&]() {
        if (token.isEmpty()) {
            return;
        }
        const QString canonicalToken = canonicalizeToken(token);
        if (!canonicalToken.isEmpty()) {
            currentGroupTokens.append(canonicalToken);
        }
        token.clear();
    };
    const auto finalizeGroup = [&]() {
        if (!currentGroupTokens.isEmpty()) {
            currentGroups.append(currentGroupTokens);
            currentGroupTokens.clear();
        }
    };
    const auto flushMoment = [&]() {
        flushToken();
        finalizeGroup();
        const QString momentText = buildMomentText(currentGroups);
        if (!momentText.isEmpty()) {
            MeasureMoment moment;
            moment.positionWhole = currentPositionWhole;
            moment.text = momentText;
            currentMeasure.moments.append(moment);
        }
        currentGroups.clear();
    };
    const auto appendRenderedMeasure = [&](double lengthWhole) {
        if (lengthWhole <= kNormalizationEpsilon
            && currentMeasure.leadingItems.isEmpty()
            && currentMeasure.trailingItems.isEmpty()
            && currentMeasure.moments.isEmpty()) {
            return;
        }
        RenderMeasure stored;
        stored.meterNumerator = currentMeasure.meterNumerator;
        stored.meterDenominator = currentMeasure.meterDenominator;
        stored.startPhaseWhole = currentMeasure.startPhaseWhole;
        stored.lengthWhole = qMax(0.0, lengthWhole);
        stored.leadingItems = currentMeasure.leadingItems;
        stored.trailingItems = currentMeasure.trailingItems;
        stored.moments = currentMeasure.moments;
        renderedMeasures.append(stored);
    };
    const auto beginFreshMeasure = [&](int meterNumerator, int meterDenominator, double startPhaseWhole = 0.0) {
        currentMeasure = MeasureBuilder();
        currentMeasure.meterNumerator = qMax(1, meterNumerator);
        currentMeasure.meterDenominator = qMax(1, meterDenominator);
        currentMeasure.startPhaseWhole = normalizeMeasurePhaseWhole(
            startPhaseWhole,
            currentMeasure.meterNumerator,
            currentMeasure.meterDenominator);
        currentPositionWhole = 0.0;
    };
    const auto currentMeasureLengthWhole = [&]() {
        return measureLengthWhole(currentMeasure.meterNumerator, currentMeasure.meterDenominator);
    };
    const auto currentRemainingMeasureLengthWhole = [&]() {
        return qMax(
            0.0,
            currentMeasureLengthWhole() - normalizeMeasurePhaseWhole(
                currentMeasure.startPhaseWhole,
                currentMeasure.meterNumerator,
                currentMeasure.meterDenominator));
    };
    const auto appendBoundaryItem = [&](const BoundaryItem& item) {
        flushMoment();
        if (!currentMeasure.moments.isEmpty() || currentPositionWhole > kNormalizationEpsilon) {
            currentMeasure.trailingItems.append(item);
            return;
        }
        currentMeasure.leadingItems.append(item);
    };
    const auto restartMeasureAtCurrentPosition = [&](const BoundaryItem& item, int nextMeterNumerator, int nextMeterDenominator) {
        flushMoment();
        if (!currentMeasure.moments.isEmpty() || currentPositionWhole > kNormalizationEpsilon) {
            appendRenderedMeasure(currentPositionWhole);
            beginFreshMeasure(nextMeterNumerator, nextMeterDenominator);
        } else {
            currentMeasure.meterNumerator = qMax(1, nextMeterNumerator);
            currentMeasure.meterDenominator = qMax(1, nextMeterDenominator);
            currentMeasure.startPhaseWhole = 0.0;
        }
        currentMeasure.leadingItems.append(item);
    };
    const auto splitMeasureAtCurrentPosition = [&](const BoundaryItem& item) {
        flushMoment();
        if (!currentMeasure.moments.isEmpty() || currentPositionWhole > kNormalizationEpsilon) {
            const int carryMeterNumerator = currentMeasure.meterNumerator;
            const int carryMeterDenominator = currentMeasure.meterDenominator;
            const double nextStartPhase = currentMeasure.startPhaseWhole + currentPositionWhole;
            appendRenderedMeasure(currentPositionWhole);
            beginFreshMeasure(carryMeterNumerator, carryMeterDenominator, nextStartPhase);
        }
        currentMeasure.leadingItems.append(item);
    };
    const auto advanceByComma = [&]() {
        flushMoment();
        const double stepWhole = currentBeats > 0 ? (1.0 / static_cast<double>(currentBeats)) : 0.0;
        currentPositionWhole += stepWhole;
        while (currentPositionWhole + kNormalizationEpsilon >= currentRemainingMeasureLengthWhole()) {
            const int carryMeterNumerator = currentMeasure.meterNumerator;
            const int carryMeterDenominator = currentMeasure.meterDenominator;
            const double completedLength = currentRemainingMeasureLengthWhole();
            const double overflow = currentPositionWhole - completedLength;
            appendRenderedMeasure(completedLength);
            beginFreshMeasure(carryMeterNumerator, carryMeterDenominator);
            currentPositionWhole = nearlyEqual(overflow, 0.0) ? 0.0 : qMax(0.0, overflow);
        }
    };

    const QStringList lines = input.split(QLatin1Char('\n'));
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (isTerminalMarkerText(line)) {
            finalizeGroup();
            continue;
        }

        bool terminatedByComment = false;
        for (int index = 0; index < line.size(); ++index) {
            const QChar ch = line.at(index);

            if (ch == QLatin1Char('|') && index + 1 < line.size() && line.at(index + 1) == QLatin1Char('|')) {
                flushToken();
                finalizeGroup();
                int numerator = 0;
                int denominator = 0;
                QString normalizedText;
                if (miacode::simai::parseInlineTimeSignatureComment(
                        line,
                        index,
                        &numerator,
                        &denominator,
                        &normalizedText)) {
                    restartMeasureAtCurrentPosition(
                        BoundaryItem{BoundaryItemKind::TimeSignature, QStringLiteral("|| %1").arg(normalizedText)},
                        numerator,
                        denominator);
                } else {
                    splitMeasureAtCurrentPosition(
                        BoundaryItem{BoundaryItemKind::StandaloneText, line.mid(index).trimmed()});
                }
                terminatedByComment = true;
                break;
            }

            if (ch.isSpace()) {
                flushToken();
                continue;
            }

            if (ch == QLatin1Char('(')) {
                flushToken();
                finalizeGroup();
                const int close = line.indexOf(QLatin1Char(')'), index + 1);
                if (close < 0) {
                    break;
                }
                const QString bpmText = line.mid(index + 1, close - index - 1).trimmed();
                bool bpmOk = false;
                const double parsedBpm = bpmText.toDouble(&bpmOk);
                if (bpmOk) {
                    if (!nearlyEqual(parsedBpm, currentBpm)) {
                        restartMeasureAtCurrentPosition(
                            BoundaryItem{BoundaryItemKind::Bpm, QStringLiteral("(%1)").arg(bpmText)},
                            currentMeasure.meterNumerator,
                            currentMeasure.meterDenominator);
                    } else {
                        appendBoundaryItem(BoundaryItem{BoundaryItemKind::StandaloneText, QStringLiteral("(%1)").arg(bpmText)});
                    }
                    currentBpm = parsedBpm;
                }
                index = close;
                continue;
            }

            if (ch == QLatin1Char('{')) {
                flushToken();
                finalizeGroup();
                const int close = line.indexOf(QLatin1Char('}'), index + 1);
                if (close < 0) {
                    break;
                }
                bool beatsOk = false;
                const int parsedBeats = line.mid(index + 1, close - index - 1).trimmed().toInt(&beatsOk);
                if (beatsOk && parsedBeats > 0) {
                    currentBeats = parsedBeats;
                }
                index = close;
                continue;
            }

            if (ch == QLatin1Char('H') && line.mid(index, 3) == QStringLiteral("HS*")) {
                flushToken();
                finalizeGroup();
                const int close = line.indexOf(QLatin1Char('>'), index + 3);
                if (close < 0) {
                    break;
                }
                appendBoundaryItem(BoundaryItem{
                    BoundaryItemKind::StandaloneText,
                    line.mid(index, close - index + 1).trimmed()});
                index = close;
                continue;
            }

            if (ch == QLatin1Char('/')) {
                flushToken();
                continue;
            }

            if (ch == QLatin1Char('`')) {
                flushToken();
                finalizeGroup();
                continue;
            }

            if (ch == QLatin1Char(',')) {
                advanceByComma();
                continue;
            }

            if (token.isEmpty()
                && (ch == QLatin1Char('E') || ch == QLatin1Char('e'))
                && lineTailIsTerminalMarker(line, index)) {
                flushToken();
                finalizeGroup();
                break;
            }

            token.append(ch);
        }

        flushToken();
        if (!terminatedByComment) {
            finalizeGroup();
        }
    }

    flushMoment();
    if (!currentMeasure.moments.isEmpty()
        || currentPositionWhole > kNormalizationEpsilon
        || !currentMeasure.leadingItems.isEmpty()
        || !currentMeasure.trailingItems.isEmpty()) {
        appendRenderedMeasure(currentPositionWhole);
    }

    QStringList outputLines;
    int emittedMeasureLines = 0;
    for (const RenderMeasure& measure : renderedMeasures) {
        appendBoundaryItems(&outputLines, measure.leadingItems);
        if (measure.lengthWhole > kNormalizationEpsilon || !measure.moments.isEmpty()) {
            outputLines.append(renderMeasureLine(measure));
            ++emittedMeasureLines;
        }
        appendBoundaryItems(&outputLines, measure.trailingItems);
        if (emittedMeasureLines > 0 && (emittedMeasureLines % 4) == 0) {
            outputLines.append(QString());
        }
    }
    while (!outputLines.isEmpty() && outputLines.constLast().isEmpty()) {
        outputLines.removeLast();
    }
    outputLines.append(QStringLiteral("E"));

    result.ok = true;
    result.text = outputLines.join(QLatin1Char('\n'));
    result.measureLineCount = emittedMeasureLines;
    result.changedCount = result.text == input ? 0 : qMax(1, emittedMeasureLines);
    return result;
}

}  // namespace miacode::chart_transform
