#include "tools/muri/MuriPanelEntries.h"

#include <QHash>
#include <QSet>
#include <QtMath>

namespace {

using miacode::muri::MuriPanelEntry;

QString muriPanelEntryDedupeKey(MuriKind kind, int line, int col)
{
    return QStringLiteral("%1|%2|%3").arg(static_cast<int>(kind)).arg(qMax(1, line)).arg(qMax(1, col));
}

bool keepDenseOverlapPanelIssue(MuriKind kind, double second, QHash<qint64, int>* keptOverlapCountBySecond)
{
    if (kind != MuriKind::Overlap || keptOverlapCountBySecond == nullptr) {
        return true;
    }

    const qint64 secondKey = qRound64(qMax(0.0, second) * 1000000.0);
    const int keptCount = keptOverlapCountBySecond->value(secondKey, 0);
    if (keptCount >= 1) {
        return false;
    }
    keptOverlapCountBySecond->insert(secondKey, keptCount + 1);
    return true;
}

struct MuriPanelAnchor {
    bool valid = false;
    double second = 0.0;
    int line = 1;
    int col = 1;
};

bool muriPanelAnchorComesBefore(const MuriPanelAnchor& left, const MuriPanelAnchor& right)
{
    if (!left.valid) {
        return false;
    }
    if (!right.valid) {
        return true;
    }
    if (left.second + 1e-6 < right.second) {
        return true;
    }
    if (right.second + 1e-6 < left.second) {
        return false;
    }
    if (left.line != right.line) {
        return left.line < right.line;
    }
    return left.col < right.col;
}

MuriPanelAnchor earlierMuriPanelAnchor(const MuriPanelAnchor& first, const MuriPanelAnchor& second)
{
    return muriPanelAnchorComesBefore(second, first) ? second : first;
}

MuriPanelAnchor muriPanelAnchorFromReferenceNote(const MuriStaticReferenceNote& note)
{
    MuriPanelAnchor anchor;
    anchor.valid = true;
    anchor.second = qMax(0.0, note.second);
    anchor.line = note.line;
    anchor.col = note.col;
    return anchor;
}

QString simpleNoteConfigToken(int lane, bool hasProtection, bool isHold)
{
    QString token;
    if (lane >= 1 && lane <= 8) {
        token = QString::number(lane);
    }
    if (token.isEmpty()) {
        return token;
    }
    if (hasProtection) {
        token += QLatin1Char('x');
    }
    if (isHold) {
        token += QLatin1Char('h');
    }
    return token;
}

QString muriStaticReferenceConfigText(const MuriStaticReferenceNote& note)
{
    if (note.headStarTapLike) {
        const QString token = simpleNoteConfigToken(note.lane, note.hasProtection, false);
        const QString base = token.isEmpty()
            ? QStringLiteral("star")
            : QStringLiteral("star %1").arg(token);
        return note.hasProtection ? QStringLiteral("protected %1").arg(base) : base;
    }

    const QString markerType = note.markerType.trimmed().toLower();
    const QString pad = note.pad.trimmed().toUpper();
    if (markerType == QLatin1String("tap")) {
        const QString token = simpleNoteConfigToken(note.lane, note.hasProtection, false);
        const QString base = token.isEmpty()
            ? QStringLiteral("tap")
            : QStringLiteral("tap %1").arg(token);
        return note.hasProtection ? QStringLiteral("protected %1").arg(base) : base;
    }
    if (markerType == QLatin1String("hold")) {
        const QString token = simpleNoteConfigToken(note.lane, note.hasProtection, true);
        const QString base = token.isEmpty()
            ? QStringLiteral("hold")
            : QStringLiteral("hold %1").arg(token);
        return note.hasProtection ? QStringLiteral("protected %1").arg(base) : base;
    }
    if (markerType == QLatin1String("touch")) {
        return pad.isEmpty() ? QStringLiteral("touch") : QStringLiteral("touch %1").arg(pad);
    }
    if (markerType == QLatin1String("touch_hold")) {
        return pad.isEmpty() ? QStringLiteral("touch-hold") : QStringLiteral("touch-hold %1").arg(pad);
    }
    if (markerType == QLatin1String("slide")) {
        const QString trackKey = !note.slideDisplayKey.trimmed().isEmpty()
            ? note.slideDisplayKey.trimmed()
            : (note.slideTrackKey.trimmed().isEmpty()
                  ? QStringLiteral("%1->%2").arg(note.lane).arg(note.endLane)
                  : note.slideTrackKey.trimmed());
        return QStringLiteral("slide %1").arg(trackKey);
    }
    if (markerType == QLatin1String("wifi")) {
        const QString trackKey = !note.slideDisplayKey.trimmed().isEmpty()
            ? note.slideDisplayKey.trimmed()
            : (note.slideTrackKey.trimmed().isEmpty()
                  ? QStringLiteral("%1w%2").arg(note.lane).arg(note.endLane)
                  : note.slideTrackKey.trimmed());
        return QStringLiteral("wifi %1").arg(trackKey);
    }
    return markerType;
}

QString buildMuriStaticReferenceDetail(const MuriStaticReference& reference)
{
    const QString affectedText = muriStaticReferenceConfigText(reference.affected);
    const QString causeText = muriStaticReferenceConfigText(reference.cause);
    const double gapMs = qAbs(reference.deltaSecond) * 1000.0;
    if (reference.kind == MuriKind::SlideHeadTap) {
        return reference.slideHeadHasTapOnSlide
            ? (reference.alertLevel == MuriAlertLevel::Warning
                   ? QStringLiteral("%1 start may early-judge %2, gap %3 ms.")
                         .arg(causeText, affectedText, QString::number(gapMs, 'f', 1))
                   : QStringLiteral("%1 start will early-judge %2, gap %3 ms.")
                         .arg(causeText, affectedText, QString::number(gapMs, 'f', 1)))
            : (reference.alertLevel == MuriAlertLevel::Warning
                   ? QStringLiteral("%1 jump-start may early-judge %2, gap %3 ms.")
                         .arg(causeText, affectedText, QString::number(gapMs, 'f', 1))
                   : QStringLiteral("%1 jump-start will early-judge %2, gap %3 ms.")
                         .arg(causeText, affectedText, QString::number(gapMs, 'f', 1)));
    }
    if (reference.kind == MuriKind::TapOnSlide) {
        return reference.alertLevel == MuriAlertLevel::Warning
            ? QStringLiteral("%1 trajectory may collide with %2, gap %3 ms.")
                  .arg(causeText, affectedText, QString::number(gapMs, 'f', 1))
            : QStringLiteral("%1 trajectory will collide with %2, gap %3 ms.")
                  .arg(causeText, affectedText, QString::number(gapMs, 'f', 1));
    }
    if (reference.kind == MuriKind::Overlap) {
        return QStringLiteral("%1 and same-position %2 formed overlap.")
            .arg(affectedText, causeText);
    }

    return reference.hasDelta
        ? QStringLiteral("Static reference from %1, Δ %2 ms")
              .arg(causeText, QString::number(reference.deltaSecond * 1000.0, 'f', 1))
        : QStringLiteral("Static reference from %1").arg(causeText);
}

MuriPanelEntry makeMuriPanelEntry(const MuriDiagnostic& diagnostic)
{
    MuriPanelEntry entry;
    entry.kind = diagnostic.kind;
    entry.alertLevel = diagnostic.alertLevel;
    entry.second = diagnostic.anchorSecond >= 0.0 ? diagnostic.anchorSecond : diagnostic.second;
    entry.occurrenceSecond = diagnostic.second;
    entry.line = diagnostic.line;
    entry.col = diagnostic.col;
    entry.rawDetail = diagnostic.detail;
    return entry;
}

MuriPanelEntry makeMuriPanelEntry(const MuriStaticReference& reference)
{
    MuriPanelEntry entry;
    entry.isStatic = true;
    entry.kind = reference.kind;
    entry.alertLevel = reference.alertLevel;
    const MuriPanelAnchor anchor = earlierMuriPanelAnchor(
        muriPanelAnchorFromReferenceNote(reference.affected),
        muriPanelAnchorFromReferenceNote(reference.cause));
    entry.second = anchor.valid ? anchor.second : qMax(0.0, reference.affected.second);
    entry.occurrenceSecond = qMax(0.0, reference.affected.second);
    entry.line = anchor.valid ? anchor.line : reference.affected.line;
    entry.col = anchor.valid ? anchor.col : reference.affected.col;
    entry.rawDetail = buildMuriStaticReferenceDetail(reference);
    return entry;
}

}  // namespace

namespace miacode::muri {

QVector<MuriPanelEntry> buildVisibleMuriPanelEntries(
    const MuriAnalysisReport& analysisReport,
    const QVector<MuriStaticReference>& staticReferences)
{
    QVector<MuriPanelEntry> entries;
    entries.reserve(analysisReport.diagnostics.size() + staticReferences.size());

    QHash<qint64, int> keptOverlapCountBySecond;
    QSet<QString> runtimeIssueKeys;
    runtimeIssueKeys.reserve(analysisReport.diagnostics.size());

    for (const MuriDiagnostic& diagnostic : analysisReport.diagnostics) {
        const MuriPanelEntry entry = makeMuriPanelEntry(diagnostic);
        if (!keepDenseOverlapPanelIssue(entry.kind, entry.second, &keptOverlapCountBySecond)) {
            continue;
        }
        runtimeIssueKeys.insert(muriPanelEntryDedupeKey(entry.kind, entry.line, entry.col));
        entries.append(entry);
    }

    for (const MuriStaticReference& reference : staticReferences) {
        const MuriPanelEntry entry = makeMuriPanelEntry(reference);
        if (!keepDenseOverlapPanelIssue(entry.kind, entry.second, &keptOverlapCountBySecond)) {
            continue;
        }
        if (runtimeIssueKeys.contains(muriPanelEntryDedupeKey(entry.kind, entry.line, entry.col))) {
            continue;
        }
        entries.append(entry);
    }

    return entries;
}

}  // namespace miacode::muri
