#include "ChartBatchTransform.h"
#include "ChartBatchTransform.Internal.h"

namespace miacode::chart_transform::detail {

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
        } else if (lower == QChar('m')) {
            parts->hasMine = true;
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
    if (parts.hasMine) {
        token.append(QChar('m'));
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

}  // namespace miacode::chart_transform::detail
