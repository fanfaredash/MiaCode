#include "ChartNormalization.h"

#include <algorithm>
#include <limits>
#include <numeric>

#include <QtGlobal>

#include "common/OperationLog.h"
#include "core/chart/parser/SimaiNativeParser.h"

namespace miacode::chart_transform {
namespace {

constexpr double kNormalizationEpsilon = 1e-6;
constexpr int kMinimumSnapSubdivisionBeats = 16;
constexpr int kMaximumSnapSubdivisionBeats = 384;
constexpr double kSnapToleranceWhole = (1.0 / (2.0 * static_cast<double>(kMaximumSnapSubdivisionBeats))) + 1e-9;

struct Rational {
    qint64 numerator = 0;
    qint64 denominator = 1;

    Rational() = default;
    Rational(qint64 n, qint64 d)
        : numerator(n)
        , denominator(d)
    {
        normalize();
    }

    void normalize()
    {
        if (denominator == 0) {
            numerator = 0;
            denominator = 1;
            return;
        }
        if (denominator < 0) {
            numerator = -numerator;
            denominator = -denominator;
        }
        if (numerator == 0) {
            denominator = 1;
            return;
        }
        const qint64 gcd = std::gcd(qAbs(numerator), denominator);
        if (gcd > 1) {
            numerator /= gcd;
            denominator /= gcd;
        }
    }

    bool isZero() const
    {
        return numerator == 0;
    }
};

Rational operator+(const Rational& left, const Rational& right)
{
    return Rational(
        left.numerator * right.denominator + right.numerator * left.denominator,
        left.denominator * right.denominator);
}

Rational operator-(const Rational& left, const Rational& right)
{
    return Rational(
        left.numerator * right.denominator - right.numerator * left.denominator,
        left.denominator * right.denominator);
}

bool operator<(const Rational& left, const Rational& right)
{
    return left.numerator * right.denominator < right.numerator * left.denominator;
}

bool operator>(const Rational& left, const Rational& right)
{
    return right < left;
}

bool operator<=(const Rational& left, const Rational& right)
{
    return !(left > right);
}

bool operator>=(const Rational& left, const Rational& right)
{
    return !(left < right);
}

bool operator==(const Rational& left, const Rational& right)
{
    return left.numerator == right.numerator && left.denominator == right.denominator;
}

double toDouble(const Rational& value)
{
    return static_cast<double>(value.numerator) / static_cast<double>(value.denominator);
}

qint64 safeLcm(qint64 left, qint64 right)
{
    if (left <= 0) {
        return qMax<qint64>(1, right);
    }
    if (right <= 0) {
        return qMax<qint64>(1, left);
    }
    const qint64 gcd = std::gcd(left, right);
    const qint64 scaled = left / gcd;
    if (scaled > (std::numeric_limits<qint64>::max() / right)) {
        return 0;
    }
    return scaled * right;
}

bool scaleRationalExact(const Rational& value, int scale, qint64* scaledValue = nullptr)
{
    if (scale <= 0) {
        return false;
    }
    const qint64 scaledNumerator = value.numerator * static_cast<qint64>(scale);
    if ((scaledNumerator % value.denominator) != 0) {
        return false;
    }
    if (scaledValue != nullptr) {
        *scaledValue = scaledNumerator / value.denominator;
    }
    return true;
}

qint64 scaleRationalRounded(const Rational& value, int scale)
{
    return qRound64(toDouble(value) * static_cast<double>(scale));
}

double scaledSnapError(const Rational& value, qint64 scaledValue, int scale)
{
    return qAbs(toDouble(value) - (static_cast<double>(scaledValue) / static_cast<double>(scale)));
}

QString normalizeTimeSignatureText(int numerator, int denominator)
{
    return QStringLiteral("%1/%2").arg(qMax(1, numerator)).arg(qMax(1, denominator));
}

QVector<int> preferredSnapSubdivisionBeats()
{
    QVector<int> beatsValues;
    for (int beats = kMinimumSnapSubdivisionBeats; beats <= kMaximumSnapSubdivisionBeats; ++beats) {
        if ((kMaximumSnapSubdivisionBeats % beats) == 0) {
            beatsValues.append(beats);
        }
    }
    return beatsValues;
}

enum class BoundaryItemKind {
    StandaloneText,
    Bpm,
    TimeSignature,
};

struct BoundaryItem {
    BoundaryItemKind kind = BoundaryItemKind::StandaloneText;
    QString text;
};

using MomentGroups = QVector<QStringList>;

struct MeasureMoment {
    Rational positionWhole;
    MomentGroups groups;
};

struct MeasureBuilder {
    int meterNumerator = miacode::simai::kDefaultWholeTimeSignatureNumerator;
    int meterDenominator = miacode::simai::kDefaultWholeTimeSignatureDenominator;
    Rational startPhaseWhole;
    QVector<BoundaryItem> leadingItems;
    QVector<BoundaryItem> trailingItems;
    QVector<MeasureMoment> moments;
};

struct RenderMeasure {
    int meterNumerator = miacode::simai::kDefaultWholeTimeSignatureNumerator;
    int meterDenominator = miacode::simai::kDefaultWholeTimeSignatureDenominator;
    Rational startPhaseWhole;
    Rational lengthWhole;
    QVector<BoundaryItem> leadingItems;
    QVector<BoundaryItem> trailingItems;
    QVector<MeasureMoment> moments;
};

struct NormalizationSeed {
    int meterNumerator = miacode::simai::kDefaultWholeTimeSignatureNumerator;
    int meterDenominator = miacode::simai::kDefaultWholeTimeSignatureDenominator;
    Rational startPhaseWhole;
    int currentBeats = 4;
    double currentBpm = 120.0;
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

struct DurationNormalizationOptions {
    bool allowZeroDuration = false;
    bool omitZeroDurationBracket = false;
};

QString sortedModifierText(QString modifiers)
{
    std::sort(modifiers.begin(), modifiers.end(), [](QChar left, QChar right) {
        return left.unicode() < right.unicode();
    });
    return modifiers;
}

bool parsePlainDurationSignature(const QString& signature, Rational* durationWhole)
{
    if (durationWhole == nullptr || signature.contains(QLatin1Char('#'))) {
        return false;
    }
    const int colon = signature.indexOf(QLatin1Char(':'));
    if (colon < 0 || signature.indexOf(QLatin1Char(':'), colon + 1) >= 0) {
        return false;
    }
    bool beatsOk = false;
    bool numeratorOk = false;
    const int beats = signature.left(colon).trimmed().toInt(&beatsOk);
    const int numerator = signature.mid(colon + 1).trimmed().toInt(&numeratorOk);
    if (!beatsOk || !numeratorOk || beats <= 0 || numerator < 0) {
        return false;
    }
    *durationWhole = Rational(numerator, beats);
    return true;
}

QString normalizePlainDurationSignature(
    const QString& signature,
    int gridBeats,
    bool reduceTo384Grid,
    const DurationNormalizationOptions& options)
{
    Rational durationWhole;
    if (!parsePlainDurationSignature(signature, &durationWhole)) {
        return signature;
    }

    if (!reduceTo384Grid) {
        if (durationWhole.isZero()) {
            if (!options.allowZeroDuration) {
                return signature;
            }
            if (options.omitZeroDurationBracket) {
                return QString();
            }
            return QStringLiteral("1:0");
        }
        // Exact-mode formatting keeps the active rendered grid when it can
        // represent the duration exactly, so users do not see a 16-grid line
        // collapse its token text back to a shorter unrelated denominator.
        qint64 renderedUnits = 0;
        if (gridBeats > 0 && scaleRationalExact(durationWhole, gridBeats, &renderedUnits)) {
            return QStringLiteral("%1:%2").arg(gridBeats).arg(renderedUnits);
        }
        return QStringLiteral("%1:%2")
            .arg(durationWhole.denominator)
            .arg(durationWhole.numerator);
    }

    qint64 units = scaleRationalRounded(durationWhole, kMaximumSnapSubdivisionBeats);
    if (!options.allowZeroDuration && units <= 0) {
        units = 1;
    }
    if (options.allowZeroDuration && units <= 0) {
        if (options.omitZeroDurationBracket) {
            return QString();
        }
        return QStringLiteral("1:0");
    }
    static const QVector<int> kPreferredDurationBeats = preferredSnapSubdivisionBeats();
    int renderedBeats = kMaximumSnapSubdivisionBeats;
    for (int candidate : kPreferredDurationBeats) {
        const qint64 scaledUnits = units * static_cast<qint64>(candidate);
        if ((scaledUnits % kMaximumSnapSubdivisionBeats) == 0) {
            renderedBeats = candidate;
            break;
        }
    }
    const qint64 renderedUnits =
        (units * static_cast<qint64>(renderedBeats)) / static_cast<qint64>(kMaximumSnapSubdivisionBeats);
    return QStringLiteral("%1:%2").arg(renderedBeats).arg(renderedUnits);
}

QString normalizeSingleBracketSuffix(
    const QString& bracketSuffix,
    int gridBeats,
    bool reduceTo384Grid,
    const DurationNormalizationOptions& options)
{
    if (bracketSuffix.isEmpty()) {
        return QString();
    }
    const int openBracket = bracketSuffix.indexOf(QLatin1Char('['));
    const int closeBracket = bracketSuffix.lastIndexOf(QLatin1Char(']'));
    if (openBracket < 0 || closeBracket != bracketSuffix.size() - 1) {
        return bracketSuffix;
    }
    const QString normalizedSignature = normalizePlainDurationSignature(
        bracketSuffix.mid(openBracket + 1, closeBracket - openBracket - 1),
        gridBeats,
        reduceTo384Grid,
        options);
    if (normalizedSignature.isEmpty()) {
        return QString();
    }
    return QStringLiteral("[%1]").arg(normalizedSignature);
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
    for (int index = 1; index < core.size(); ++index) {
        const QChar ch = core.at(index);
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
    for (int index = 1; index < core.size(); ++index) {
        if (core.at(index).toLower() == QLatin1Char('b')) {
            continue;
        }
        parts->coreWithoutTrackBreak.append(core.at(index));
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
    token.append(parts.bracketSuffix);
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
    token.append(parts.bracketSuffix);
    return token;
}

QString buildSlideToken(const SlideTokenParts& parts)
{
    QString token;
    token.reserve(parts.coreWithoutTrackBreak.size() + parts.headExtraModifiers.size() + 3);
    token.append(parts.coreWithoutTrackBreak.at(0));
    token.append(sortedModifierText(parts.headExtraModifiers));
    if (parts.headBreak) {
        token.append(QLatin1Char('b'));
    }
    if (parts.headEx) {
        token.append(QLatin1Char('x'));
    }
    QString remainder = parts.coreWithoutTrackBreak.mid(1);
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

QString renderTokenForGrid(const QString& token, int gridBeats, bool reduceTo384Grid)
{
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    TouchTokenParts touchParts;
    if (parseTouchTokenParts(trimmed, &touchParts) && touchParts.valid) {
        touchParts.bracketSuffix = normalizeSingleBracketSuffix(
            touchParts.bracketSuffix,
            gridBeats,
            reduceTo384Grid,
            DurationNormalizationOptions{true, false});
        return buildTouchToken(touchParts);
    }

    NoteTokenParts noteParts;
    if (parseNoteTokenParts(trimmed, &noteParts) && noteParts.valid) {
        noteParts.bracketSuffix = normalizeSingleBracketSuffix(
            noteParts.bracketSuffix,
            gridBeats,
            reduceTo384Grid,
            DurationNormalizationOptions{true, true});
        return buildNoteToken(noteParts);
    }

    SlideTokenParts slideParts;
    if (parseSlideTokenParts(trimmed, &slideParts) && slideParts.valid) {
        const int openBracket = slideParts.coreWithoutTrackBreak.indexOf(QLatin1Char('['));
        if (openBracket >= 0) {
            const QString normalizedBracket = normalizeSingleBracketSuffix(
                slideParts.coreWithoutTrackBreak.mid(openBracket),
                gridBeats,
                reduceTo384Grid,
                DurationNormalizationOptions{false, false});
            slideParts.coreWithoutTrackBreak =
                slideParts.coreWithoutTrackBreak.left(openBracket) + normalizedBracket;
        }
        return buildSlideToken(slideParts);
    }

    return trimmed;
}

MomentGroups normalizedMomentGroups(const QVector<QStringList>& groups)
{
    MomentGroups normalized;
    normalized.reserve(groups.size());
    for (const QStringList& group : groups) {
        QStringList renderedTokens;
        renderedTokens.reserve(group.size());
        for (const QString& token : group) {
            if (!token.trimmed().isEmpty()) {
                renderedTokens.append(token.trimmed());
            }
        }
        if (!renderedTokens.isEmpty()) {
            normalized.append(renderedTokens);
        }
    }
    return normalized;
}

void appendMomentGroups(MomentGroups* target, const MomentGroups& extra)
{
    if (target == nullptr) {
        return;
    }
    for (const QStringList& group : extra) {
        if (!group.isEmpty()) {
            target->append(group);
        }
    }
}

QString buildMomentText(const MomentGroups& groups, int gridBeats, bool reduceTo384Grid)
{
    QStringList renderedGroups;
    renderedGroups.reserve(groups.size());
    for (const QStringList& group : groups) {
        QStringList renderedTokens;
        renderedTokens.reserve(group.size());
        for (const QString& token : group) {
            const QString renderedToken = renderTokenForGrid(token, gridBeats, reduceTo384Grid);
            if (!renderedToken.isEmpty()) {
                renderedTokens.append(renderedToken);
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

Rational measureLengthWhole(int meterNumerator, int meterDenominator)
{
    return Rational(qMax(1, meterNumerator), qMax(1, meterDenominator));
}

Rational normalizeMeasurePhaseWhole(const Rational& phaseWhole, int meterNumerator, int meterDenominator)
{
    const Rational measureLength = measureLengthWhole(meterNumerator, meterDenominator);
    Rational normalized = phaseWhole;
    while (normalized >= measureLength) {
        normalized = normalized - measureLength;
    }
    while (normalized < Rational(0, 1)) {
        normalized = normalized + measureLength;
    }
    return normalized;
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

bool snapRangeFitsBeats(const QVector<Rational>& momentPositions, int beats)
{
    for (const Rational& position : momentPositions) {
        const qint64 units = scaleRationalRounded(position, beats);
        if (scaledSnapError(position, units, beats) > kSnapToleranceWhole) {
            return false;
        }
    }
    return true;
}

QString renderMeasureLineApproximate(const RenderMeasure& measure)
{
    static const QVector<int> kPreferredBeats = preferredSnapSubdivisionBeats();

    const int measureGridLength = qMax(1, qRound(toDouble(measure.lengthWhole) * 384.0));
    const int startPhaseGrid = qMax(0, qRound(toDouble(measure.startPhaseWhole) * 384.0));
    const int endPhaseGrid = startPhaseGrid + measureGridLength;
    const int beatGrid = measure.meterDenominator > 0
        ? qMax(1, qRound(384.0 / static_cast<double>(measure.meterDenominator)))
        : 0;

    QVector<int> momentAbsoluteGrids;
    momentAbsoluteGrids.reserve(measure.moments.size());
    for (const MeasureMoment& moment : measure.moments) {
        momentAbsoluteGrids.append(
            startPhaseGrid + qBound(0, measureGridLength, qRound(toDouble(moment.positionWhole) * 384.0)));
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

        QVector<Rational> segmentMomentPositions;
        QVector<int> segmentMomentIndices;
        segmentMomentPositions.reserve(measure.moments.size());
        segmentMomentIndices.reserve(measure.moments.size());
        for (int index = 0; index < momentAbsoluteGrids.size(); ++index) {
            const int absoluteGrid = momentAbsoluteGrids.at(index);
            if (absoluteGrid < segmentStartGrid || absoluteGrid >= segmentEndGrid) {
                continue;
            }
            segmentMomentPositions.append(Rational(absoluteGrid - segmentStartGrid, 384));
            segmentMomentIndices.append(index);
        }

        int beats = 384;
        for (int candidate : kPreferredBeats) {
            const int stepGrid = 384 / candidate;
            if (stepGrid <= 0 || (segmentLengthGrid % stepGrid) != 0) {
                continue;
            }
            if (snapRangeFitsBeats(segmentMomentPositions, candidate)) {
                beats = candidate;
                break;
            }
        }

        const int slotCount = qMax(1, qRound(toDouble(Rational(segmentLengthGrid, 384)) * beats));
        QVector<MomentGroups> slotGroups(slotCount);
        for (int localIndex = 0; localIndex < segmentMomentIndices.size(); ++localIndex) {
            const int momentIndex = segmentMomentIndices.at(localIndex);
            const qint64 scaled = scaleRationalRounded(segmentMomentPositions.at(localIndex), beats);
            const int slotIndex = qBound(0, slotCount - 1, static_cast<int>(scaled));
            appendMomentGroups(&slotGroups[slotIndex], measure.moments.at(momentIndex).groups);
        }

        if (line.isEmpty() || beats != lastSegmentBeats) {
            line.append(QStringLiteral("{%1}").arg(beats));
            lastSegmentBeats = beats;
        }
        for (const MomentGroups& slotGroup : slotGroups) {
            line.append(buildMomentText(slotGroup, beats, true));
            line.append(QLatin1Char(','));
        }
        if (segmentEndGrid < endPhaseGrid && beatGrid > 0 && (segmentEndGrid % beatGrid) == 0) {
            line.append(QLatin1Char(' '));
        }
        segmentStartGrid = segmentEndGrid;
    }

    return line.trimmed();
}

QVector<Rational> beatBoundaryPositions(const RenderMeasure& measure)
{
    QVector<Rational> boundaries;
    const Rational beatLength(1, qMax(1, measure.meterDenominator));
    const Rational absoluteStart = measure.startPhaseWhole;
    const Rational absoluteEnd = measure.startPhaseWhole + measure.lengthWhole;

    qint64 beatIndex = (absoluteStart.numerator * beatLength.denominator)
        / (absoluteStart.denominator * beatLength.numerator);
    Rational candidate(static_cast<qint64>(beatIndex + 1) * beatLength.numerator, beatLength.denominator);
    while (candidate < absoluteEnd) {
        boundaries.append(candidate - absoluteStart);
        candidate = candidate + beatLength;
    }
    return boundaries;
}

int chooseExactBeatsForRange(
    const RenderMeasure& measure,
    const Rational& start,
    const Rational& end)
{
    qint64 beats = 1;
    const Rational chunkLength = end - start;
    beats = safeLcm(beats, chunkLength.denominator);
    if (beats <= 0) {
        return 0;
    }

    for (const MeasureMoment& moment : measure.moments) {
        if (moment.positionWhole < start || moment.positionWhole >= end) {
            continue;
        }
        const Rational relativePosition = moment.positionWhole - start;
        beats = safeLcm(beats, relativePosition.denominator);
        if (beats <= 0 || beats > std::numeric_limits<int>::max()) {
            return 0;
        }
    }

    return qMax<int>(1, static_cast<int>(beats));
}

QString renderExactChunk(
    const RenderMeasure& measure,
    const Rational& start,
    const Rational& end,
    int beats)
{
    qint64 slotCount64 = 0;
    const Rational chunkLength = end - start;
    if (!scaleRationalExact(chunkLength, beats, &slotCount64) || slotCount64 <= 0) {
        return QString();
    }

    const int slotCount = static_cast<int>(qMin<qint64>(slotCount64, std::numeric_limits<int>::max()));
    QVector<MomentGroups> slotGroups(slotCount);
    for (const MeasureMoment& moment : measure.moments) {
        if (moment.positionWhole < start || moment.positionWhole >= end) {
            continue;
        }
        qint64 scaled = 0;
        if (!scaleRationalExact(moment.positionWhole - start, beats, &scaled)) {
            continue;
        }
        const int slotIndex = qBound(0, slotCount - 1, static_cast<int>(scaled));
        appendMomentGroups(&slotGroups[slotIndex], moment.groups);
    }

    QString text = QStringLiteral("{%1}").arg(beats);
    for (const MomentGroups& slotGroup : slotGroups) {
        text.append(buildMomentText(slotGroup, beats, false));
        text.append(QLatin1Char(','));
    }
    return text;
}

QString renderMeasureLineExact(const RenderMeasure& measure)
{
    QString line;
    int lastBeats = 0;
    Rational cursor(0, 1);
    const QVector<Rational> boundaries = beatBoundaryPositions(measure);
    int boundaryIndex = 0;

    while (cursor < measure.lengthWhole) {
        Rational chosenEnd = measure.lengthWhole;
        bool chosenEndsAtBeatBoundary = false;
        int chosenBeats = 0;

        QVector<QPair<Rational, bool>> candidates;
        candidates.reserve(boundaries.size() - boundaryIndex + 1);
        for (int index = boundaryIndex; index < boundaries.size(); ++index) {
            if (boundaries.at(index) <= cursor) {
                continue;
            }
            candidates.append(qMakePair(boundaries.at(index), true));
        }
        candidates.append(qMakePair(measure.lengthWhole, false));

        int minimalBeats = std::numeric_limits<int>::max();
        for (const auto& candidate : candidates) {
            const int candidateBeats = chooseExactBeatsForRange(measure, cursor, candidate.first);
            if (candidateBeats <= 0) {
                continue;
            }
            if (candidateBeats < minimalBeats) {
                minimalBeats = candidateBeats;
                chosenEnd = candidate.first;
                chosenEndsAtBeatBoundary = candidate.second;
                chosenBeats = candidateBeats;
            }
        }

        if (chosenBeats <= 0) {
            break;
        }

        const QString chunkText = renderExactChunk(measure, cursor, chosenEnd, chosenBeats);
        if (chunkText.isEmpty()) {
            break;
        }
        if (line.isEmpty() || chosenBeats != lastBeats) {
            line.append(chunkText);
        } else {
            line.append(chunkText.mid(chunkText.indexOf(QLatin1Char('}')) + 1));
        }
        lastBeats = chosenBeats;

        if (chosenEndsAtBeatBoundary && chosenEnd < measure.lengthWhole) {
            line.append(QLatin1Char(' '));
        }

        cursor = chosenEnd;
        while (boundaryIndex < boundaries.size() && boundaries.at(boundaryIndex) <= cursor) {
            ++boundaryIndex;
        }
    }

    return line.trimmed();
}

QString renderMeasureLine(const RenderMeasure& measure, const ChartNormalizationOptions& options)
{
    return options.reduceTo384Grid
        ? renderMeasureLineApproximate(measure)
        : renderMeasureLineExact(measure);
}

QString summarizeValidationError(const SimaiNativeValidationReport& report)
{
    if (!report.issues.isEmpty()) {
        const SimaiNativeValidationIssue& issue = report.issues.constFirst();
        return issue.displayMessage.isEmpty() ? issue.rawMessage : issue.displayMessage;
    }
    return QStringLiteral("Fix syntax errors before normalizing this chart.");
}

NormalizationSeed seedFromTimingMetadata(const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    NormalizationSeed seed;
    seed.meterNumerator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureNumerator
        : miacode::simai::kDefaultWholeTimeSignatureNumerator;
    seed.meterDenominator = timingMetadata.wholeTimeSignatureValid
        ? timingMetadata.wholeTimeSignatureDenominator
        : miacode::simai::kDefaultWholeTimeSignatureDenominator;
    return seed;
}

NormalizationSeed scanNormalizationSeed(
    const QString& prefix,
    const miacode::simai::SimaiTimingMetadata& timingMetadata)
{
    NormalizationSeed seed = seedFromTimingMetadata(timingMetadata);
    Rational currentPhase;

    const auto advanceByComma = [&]() {
        currentPhase = currentPhase + Rational(1, qMax(1, seed.currentBeats));
        const Rational measureLength = measureLengthWhole(seed.meterNumerator, seed.meterDenominator);
        while (currentPhase >= measureLength) {
            currentPhase = currentPhase - measureLength;
        }
    };

    const QStringList lines = prefix.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (QString line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) {
            line.chop(1);
        }
        if (isTerminalMarkerText(line)) {
            break;
        }

        for (int index = 0; index < line.size(); ++index) {
            const QChar ch = line.at(index);
            if (ch == QLatin1Char('|') && index + 1 < line.size() && line.at(index + 1) == QLatin1Char('|')) {
                int numerator = 0;
                int denominator = 0;
                if (miacode::simai::parseInlineTimeSignatureComment(
                        line,
                        index,
                        &numerator,
                        &denominator,
                        nullptr)) {
                    seed.meterNumerator = qMax(1, numerator);
                    seed.meterDenominator = qMax(1, denominator);
                    currentPhase = Rational();
                }
                break;
            }
            if (ch.isSpace() || ch == QLatin1Char('/')) {
                continue;
            }
            if (ch == QLatin1Char('(')) {
                const int close = line.indexOf(QLatin1Char(')'), index + 1);
                if (close < 0) {
                    break;
                }
                bool bpmOk = false;
                const double parsedBpm = line.mid(index + 1, close - index - 1).trimmed().toDouble(&bpmOk);
                if (bpmOk) {
                    seed.currentBpm = parsedBpm;
                }
                index = close;
                continue;
            }
            if (ch == QLatin1Char('{')) {
                const int close = line.indexOf(QLatin1Char('}'), index + 1);
                if (close < 0) {
                    break;
                }
                bool beatsOk = false;
                const int parsedBeats = line.mid(index + 1, close - index - 1).trimmed().toInt(&beatsOk);
                if (beatsOk && parsedBeats > 0) {
                    seed.currentBeats = parsedBeats;
                }
                index = close;
                continue;
            }
            if (ch == QLatin1Char('<') && line.mid(index, 4) == QStringLiteral("<HS*")) {
                const int close = line.indexOf(QLatin1Char('>'), index + 4);
                if (close < 0) {
                    break;
                }
                index = close;
                continue;
            }
            if (ch == QLatin1Char('`')) {
                continue;
            }
            if (ch == QLatin1Char(',')) {
                advanceByComma();
                continue;
            }
            if ((ch == QLatin1Char('E') || ch == QLatin1Char('e'))
                && lineTailIsTerminalMarker(line, index)) {
                break;
            }
        }
    }

    seed.startPhaseWhole = normalizeMeasurePhaseWhole(
        currentPhase,
        seed.meterNumerator,
        seed.meterDenominator);
    return seed;
}

}  // namespace

ChartNormalizationResult normalizeChartFragment(
    const QString& input,
    const miacode::simai::SimaiTimingMetadata& timingMetadata,
    const NormalizationSeed& seed,
    const ChartNormalizationOptions& options,
    bool appendTerminalMarker,
    bool injectLeadingTimeSignature)
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
    currentMeasure.meterNumerator = seed.meterNumerator;
    currentMeasure.meterDenominator = seed.meterDenominator;
    currentMeasure.startPhaseWhole = options.startAtNewMeasure
        ? Rational()
        : seed.startPhaseWhole;
    if (injectLeadingTimeSignature) {
        currentMeasure.leadingItems.append(BoundaryItem{
            BoundaryItemKind::TimeSignature,
            QStringLiteral("|| %1").arg(normalizeTimeSignatureText(seed.meterNumerator, seed.meterDenominator))});
    }

    Rational currentPositionWhole;
    int currentBeats = qMax(1, seed.currentBeats);
    double currentBpm = seed.currentBpm;
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
        const MomentGroups groups = normalizedMomentGroups(currentGroups);
        if (!groups.isEmpty()) {
            MeasureMoment moment;
            moment.positionWhole = currentPositionWhole;
            moment.groups = groups;
            currentMeasure.moments.append(moment);
        }
        currentGroups.clear();
    };
    const auto appendRenderedMeasure = [&](const Rational& lengthWhole) {
        if (lengthWhole.isZero()
            && currentMeasure.leadingItems.isEmpty()
            && currentMeasure.trailingItems.isEmpty()
            && currentMeasure.moments.isEmpty()) {
            return;
        }
        RenderMeasure stored;
        stored.meterNumerator = currentMeasure.meterNumerator;
        stored.meterDenominator = currentMeasure.meterDenominator;
        stored.startPhaseWhole = currentMeasure.startPhaseWhole;
        stored.lengthWhole = lengthWhole;
        stored.leadingItems = currentMeasure.leadingItems;
        stored.trailingItems = currentMeasure.trailingItems;
        stored.moments = currentMeasure.moments;
        renderedMeasures.append(stored);
    };
    const auto beginFreshMeasure = [&](int meterNumerator, int meterDenominator, const Rational& startPhaseWhole = Rational()) {
        currentMeasure = MeasureBuilder();
        currentMeasure.meterNumerator = qMax(1, meterNumerator);
        currentMeasure.meterDenominator = qMax(1, meterDenominator);
        currentMeasure.startPhaseWhole = normalizeMeasurePhaseWhole(
            startPhaseWhole,
            currentMeasure.meterNumerator,
            currentMeasure.meterDenominator);
        currentPositionWhole = Rational();
    };
    const auto currentRemainingMeasureLength = [&]() {
        return measureLengthWhole(currentMeasure.meterNumerator, currentMeasure.meterDenominator)
            - currentMeasure.startPhaseWhole;
    };
    const auto appendBoundaryItem = [&](const BoundaryItem& item) {
        flushMoment();
        if (!currentMeasure.moments.isEmpty() || !currentPositionWhole.isZero()) {
            currentMeasure.trailingItems.append(item);
            return;
        }
        currentMeasure.leadingItems.append(item);
    };
    const auto restartMeasureAtCurrentPosition = [&](const BoundaryItem& item, int nextMeterNumerator, int nextMeterDenominator) {
        flushMoment();
        if (!currentMeasure.moments.isEmpty() || !currentPositionWhole.isZero()) {
            appendRenderedMeasure(currentPositionWhole);
            beginFreshMeasure(nextMeterNumerator, nextMeterDenominator);
        } else {
            currentMeasure.meterNumerator = qMax(1, nextMeterNumerator);
            currentMeasure.meterDenominator = qMax(1, nextMeterDenominator);
            currentMeasure.startPhaseWhole = Rational();
        }
        currentMeasure.leadingItems.append(item);
    };
    const auto splitMeasureAtCurrentPosition = [&](const BoundaryItem& item) {
        flushMoment();
        if (!currentMeasure.moments.isEmpty() || !currentPositionWhole.isZero()) {
            const int carryMeterNumerator = currentMeasure.meterNumerator;
            const int carryMeterDenominator = currentMeasure.meterDenominator;
            const Rational nextStartPhase = currentMeasure.startPhaseWhole + currentPositionWhole;
            appendRenderedMeasure(currentPositionWhole);
            beginFreshMeasure(carryMeterNumerator, carryMeterDenominator, nextStartPhase);
        }
        currentMeasure.leadingItems.append(item);
    };
    const auto advanceByComma = [&]() {
        flushMoment();
        currentPositionWhole = currentPositionWhole + Rational(1, qMax(1, currentBeats));
        while (currentPositionWhole >= currentRemainingMeasureLength()) {
            const int carryMeterNumerator = currentMeasure.meterNumerator;
            const int carryMeterDenominator = currentMeasure.meterDenominator;
            const Rational completedLength = currentRemainingMeasureLength();
            const Rational overflow = currentPositionWhole - completedLength;
            appendRenderedMeasure(completedLength);
            beginFreshMeasure(carryMeterNumerator, carryMeterDenominator);
            currentPositionWhole = overflow;
        }
    };

    const QStringList lines = input.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
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
                        appendBoundaryItem(
                            BoundaryItem{BoundaryItemKind::StandaloneText, QStringLiteral("(%1)").arg(bpmText)});
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

            if (ch == QLatin1Char('<') && line.mid(index, 4) == QStringLiteral("<HS*")) {
                flushToken();
                finalizeGroup();
                const int close = line.indexOf(QLatin1Char('>'), index + 4);
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
        || !currentPositionWhole.isZero()
        || !currentMeasure.leadingItems.isEmpty()
        || !currentMeasure.trailingItems.isEmpty()) {
        appendRenderedMeasure(currentPositionWhole);
    }

    QStringList outputLines;
    int emittedMeasureLines = 0;
    for (const RenderMeasure& measure : renderedMeasures) {
        appendBoundaryItems(&outputLines, measure.leadingItems);
        if (!measure.lengthWhole.isZero() || !measure.moments.isEmpty()) {
            outputLines.append(renderMeasureLine(measure, options));
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
    if (appendTerminalMarker) {
        outputLines.append(QStringLiteral("E"));
    }

    result.ok = true;
    result.text = outputLines.join(QLatin1Char('\n'));
    result.measureLineCount = emittedMeasureLines;
    result.changedCount = result.text == input ? 0 : qMax(1, emittedMeasureLines);
    return result;
}

ChartNormalizationOptions chartNormalizationOptionsFromPreferences(
    const QJsonObject& preview,
    const ChartNormalizationOptions& defaults)
{
    ChartNormalizationOptions options = defaults;
    if (preview.value(kChartNormalizeStartAtNewMeasurePreferenceKey).isBool()) {
        options.startAtNewMeasure =
            preview.value(kChartNormalizeStartAtNewMeasurePreferenceKey).toBool(options.startAtNewMeasure);
    }
    if (preview.value(kChartNormalizeReduceTo384GridPreferenceKey).isBool()) {
        options.reduceTo384Grid =
            preview.value(kChartNormalizeReduceTo384GridPreferenceKey).toBool(options.reduceTo384Grid);
    }
    return options;
}

void saveChartNormalizationOptionsToPreferences(
    QJsonObject* preview,
    const ChartNormalizationOptions& options)
{
    if (preview == nullptr) {
        return;
    }
    preview->insert(kChartNormalizeStartAtNewMeasurePreferenceKey, options.startAtNewMeasure);
    preview->insert(kChartNormalizeReduceTo384GridPreferenceKey, options.reduceTo384Grid);
}

ChartNormalizationResult normalizeChartText(
    const QString& input,
    const miacode::simai::SimaiTimingMetadata& timingMetadata,
    const ChartNormalizationOptions& options)
{
    MC_OP("miacode::chart_transform::normalizeChartText");
    _mc_op_.note(QStringLiteral("input_len=%1").arg(input.size()));
    return normalizeChartFragment(
        input,
        timingMetadata,
        seedFromTimingMetadata(timingMetadata),
        options,
        true,
        false);
}

ChartNormalizationResult normalizeChartSelectionText(
    const QString& fullText,
    int selectionStart,
    int selectionEnd,
    const miacode::simai::SimaiTimingMetadata& timingMetadata,
    const ChartNormalizationOptions& options)
{
    MC_OP("miacode::chart_transform::normalizeChartSelectionText");
    _mc_op_.note(QStringLiteral("range=%1..%2 input_len=%3")
                     .arg(selectionStart).arg(selectionEnd).arg(fullText.size()));
    if (selectionStart < 0 || selectionEnd < selectionStart || selectionEnd > fullText.size()) {
        ChartNormalizationResult result;
        result.errorMessage = QStringLiteral("Invalid selection range.");
        _mc_op_.fail(QStringLiteral("invalid selection range"));
        return result;
    }

    NormalizationSeed seed = scanNormalizationSeed(fullText.left(selectionStart), timingMetadata);
    const bool shouldInjectLeadingTimeSignature =
        options.startAtNewMeasure && !seed.startPhaseWhole.isZero();
    if (options.startAtNewMeasure) {
        seed.startPhaseWhole = Rational();
    }

    return normalizeChartFragment(
        fullText.mid(selectionStart, selectionEnd - selectionStart),
        timingMetadata,
        seed,
        options,
        false,
        shouldInjectLeadingTimeSignature);
}

}  // namespace miacode::chart_transform
