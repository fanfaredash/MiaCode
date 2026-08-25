#include "ChartWorkspace.h"

#include "core/chart/document/SimaiTimingMetadata.h"

#include <QHash>
#include <QRegularExpression>

namespace miacode::v2 {
namespace {

struct SourceFieldSpan {
    int valueLine = 1;
    int valueColumn = 1;
};

struct EffectiveSourceSpans {
    QHash<int, SourceFieldSpan> inote;
    QHash<int, SourceFieldSpan> level;

    SourceFieldSpan forDifficulty(int difficultyId) const
    {
        return inote.contains(difficultyId)
            ? inote.value(difficultyId) : level.value(difficultyId);
    }
};

EffectiveSourceSpans effectiveSourceSpans(const QString& source)
{
    EffectiveSourceSpans spans;
    const QRegularExpression header(QStringLiteral(R"((?m)^[^\S\r\n]*&(inote|lv)_(\d+)=)"));
    QRegularExpressionMatchIterator fields = header.globalMatch(source);
    while (fields.hasNext()) {
        const QRegularExpressionMatch match = fields.next();
        bool idOk = false;
        const int id = match.captured(2).toInt(&idOk);
        if (!idOk) continue;
        const int valueStart = match.capturedEnd(0);
        const int line = source.left(valueStart).count(QLatin1Char('\n')) + 1;
        const int lastNewline = source.lastIndexOf(QLatin1Char('\n'), valueStart - 1);
        const SourceFieldSpan span{line, valueStart - lastNewline};
        if (match.captured(1) == QLatin1String("inote")) {
            spans.inote.insert(id, span);
        } else {
            spans.level.insert(id, span);
        }
    }
    return spans;
}

int firstDifficultyId(const SimaiDocument& document)
{
    const QVector<int> ids = document.difficultyIds();
    return ids.isEmpty() ? 0 : ids.constFirst();
}

}  // namespace

ChartWorkspace::ChartWorkspace(QObject* parent)
    : QObject(parent)
{
}

ChartWorkspacePreflightResult ChartWorkspace::preflightSource(
    const QString& source, SimaiNativeValidationLocale locale)
{
    ChartWorkspacePreflightResult result;
    result.candidate = SimaiDocument::fromText(source);
    const miacode::simai::SimaiTimingMetadata timing =
        miacode::simai::buildTimingMetadata(result.candidate);
    const EffectiveSourceSpans spans = effectiveSourceSpans(source);

    result.accepted = true;
    for (const int difficultyId : result.candidate.difficultyIds()) {
        const SimaiDifficultyData* difficulty = result.candidate.difficulty(difficultyId);
        if (difficulty == nullptr) continue;
        const SimaiNativeValidationReport report = SimaiNativeParser::buildValidationReport(
            difficulty->chart, locale, nullptr, timing);
        const SourceFieldSpan span = spans.forDifficulty(difficultyId);
        for (const SimaiNativeValidationIssue& issue : report.issues) {
            const int line = span.valueLine + issue.line - 1;
            const int column = issue.line == 1 ? span.valueColumn + issue.col - 1 : issue.col;
            const int endColumn = issue.line == 1
                ? span.valueColumn + issue.endCol - 1 : issue.endCol;
            result.issues.append({line, column, endColumn,
                issue.severity == SimaiNativeValidationSeverity::Warning
                    ? ChartWorkspaceIssueSeverity::Warning : ChartWorkspaceIssueSeverity::Error,
                issue.displayMessage});
        }
        result.accepted = result.accepted && report.errorCount == 0;
    }
    return result;
}

ChartWorkspaceResult ChartWorkspace::replaceSource(const QString& source, const QString& filePath)
{
    const ChartWorkspacePreflightResult preflight =
        preflightSource(source, SimaiNativeValidationLocale::English);
    if (!preflight.accepted) return reject(preflight.issues);

    const int previousDifficulty = activeDifficultyId_;
    document_ = preflight.candidate;
    activeDifficultyId_ = document_.difficulty(previousDifficulty) != nullptr
        ? previousDifficulty : firstDifficultyId(document_);
    filePath_ = filePath;
    hasDocument_ = true;
    sourceText_ = document_.toText();
    savedSourceText_ = sourceText_;
    dirty_ = false;
    return commit();
}

ChartWorkspaceResult ChartWorkspace::replaceActiveDifficultyChart(const QString& chartText)
{
    if (!hasDocument_) return reject();
    SimaiDifficultyData* difficulty = document_.difficulty(activeDifficultyId_);
    if (difficulty == nullptr) return reject();

    SimaiDocument candidate = document_;
    candidate.difficulty(activeDifficultyId_)->chart = chartText;
    const ChartWorkspacePreflightResult preflight =
        preflightSource(candidate.toText(), SimaiNativeValidationLocale::English);
    if (!preflight.accepted) return reject(preflight.issues);

    document_ = preflight.candidate;
    refreshSourceAndDirty();
    return commit();
}

bool ChartWorkspace::selectDifficulty(int difficultyId)
{
    if (!hasDocument_ || activeDifficultyId_ == difficultyId
        || document_.difficulty(difficultyId) == nullptr) {
        return false;
    }
    activeDifficultyId_ = difficultyId;
    commit();
    return true;
}

bool ChartWorkspace::markSaved(const QString& filePath)
{
    if (!hasDocument_) return false;
    const QString nextFilePath = filePath.isEmpty() ? filePath_ : filePath;
    if (!dirty_ && filePath_ == nextFilePath) return false;
    savedSourceText_ = sourceText_;
    dirty_ = false;
    filePath_ = nextFilePath;
    commit();
    return true;
}

ChartWorkspaceSnapshot ChartWorkspace::snapshot() const
{
    return {sourceText_, filePath_, activeDifficultyId_, revision_, dirty_, hasDocument_};
}

const SimaiDocument& ChartWorkspace::document() const
{
    return document_;
}

ChartWorkspaceResult ChartWorkspace::reject(const QVector<ChartWorkspaceIssue>& issues) const
{
    return {false, revision_, issues};
}

ChartWorkspaceResult ChartWorkspace::commit()
{
    ++revision_;
    emit changed(revision_);
    return {true, revision_, {}};
}

void ChartWorkspace::refreshSourceAndDirty()
{
    sourceText_ = document_.toText();
    dirty_ = sourceText_ != savedSourceText_;
}

}  // namespace miacode::v2
