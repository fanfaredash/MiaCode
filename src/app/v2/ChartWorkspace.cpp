#include "ChartWorkspace.h"

#include "core/chart/document/SimaiTimingMetadata.h"

#include <QHash>
#include <QRegularExpression>

#include <utility>

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

// Same fallback order as MainWindow::activateInitialField: last-opened if it
// still exists, otherwise Master → Re:Master → Expert → Utage → Advanced →
// Basic → Easy. Empty or invalid inote slots are still difficulties; picking
// the smallest id would land the editor on a blank Basic in a Master chart.
int resolveOpenDifficultyId(const SimaiDocument& document, int requested)
{
    if (document.difficulty(requested) != nullptr) {
        return requested;
    }
    const QVector<int> ids = document.difficultyIds();
    if (ids.isEmpty()) {
        return 0;
    }
    static const int kPreferredOrder[] = {5, 6, 4, 7, 3, 2, 1};
    for (int id : kPreferredOrder) {
        if (ids.contains(id)) {
            return id;
        }
    }
    return ids.constFirst();
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

ChartWorkspaceResult ChartWorkspace::openSource(
    const QString& source, const QString& filePath, int preferredDifficultyId)
{
    const ChartWorkspacePreflightResult preflight =
        preflightSource(source, SimaiNativeValidationLocale::English);
    // Maidata field parse is the open gate. Chart-body diagnostics (empty
    // inote, unmatched brackets, unknown tokens) stay on the result for the
    // validation panel; they must not refuse the file. v1 loadDocument uses
    // SimaiDocument::fromText the same way.
    document_ = preflight.candidate;
    const int requestedDifficulty = preferredDifficultyId > 0
        ? preferredDifficultyId : activeDifficultyId_;
    activeDifficultyId_ = resolveOpenDifficultyId(document_, requestedDifficulty);
    filePath_ = filePath;
    hasDocument_ = true;
    sourceText_ = document_.toText();
    savedSourceText_ = sourceText_;
    savedDocument_ = document_;
    dirty_ = false;
    ChartWorkspaceResult result = commit();
    result.issues = preflight.issues;
    return result;
}

ChartWorkspaceResult ChartWorkspace::replaceSource(const QString& source)
{
    if (!hasDocument_) return reject();
    const ChartWorkspacePreflightResult preflight =
        preflightSource(source, SimaiNativeValidationLocale::English);
    if (!preflight.accepted) return reject(preflight.issues);

    const QString canonicalSource = preflight.candidate.toText();
    if (canonicalSource == sourceText_) return acceptWithoutChange();

    const int previousDifficulty = activeDifficultyId_;
    document_ = preflight.candidate;
    activeDifficultyId_ = document_.difficulty(previousDifficulty) != nullptr
        ? previousDifficulty : resolveOpenDifficultyId(document_, 0);
    refreshSourceAndDirty();
    return commit();
}

ChartWorkspaceResult ChartWorkspace::replaceActiveDifficultyChart(const QString& chartText)
{
    if (!hasDocument_) return reject();
    SimaiDifficultyData* difficulty = document_.difficulty(activeDifficultyId_);
    if (difficulty == nullptr) return reject();
    if (difficulty->chart == chartText) return acceptWithoutChange();

    // A visible source editor must be able to publish incomplete tokens while
    // the user is typing. Strict validation belongs to the complete-source
    // replacement transaction above, not this incremental text transaction.
    difficulty->chart = chartText;
    refreshSourceAndDirty();
    return commit();
}

bool ChartWorkspace::updateDocumentField(
    ChartWorkspaceDocumentField field, const QString& value)
{
    if (!hasDocument_) return false;
    bool changed = false;
    switch (field) {
    case ChartWorkspaceDocumentField::Title:
        changed = document_.title != value;
        document_.title = value;
        break;
    case ChartWorkspaceDocumentField::Artist:
        changed = document_.artist != value;
        document_.artist = value;
        break;
    case ChartWorkspaceDocumentField::First:
        changed = document_.first != value;
        document_.first = value;
        break;
    case ChartWorkspaceDocumentField::Designer:
        changed = document_.designer != value;
        document_.designer = value;
        break;
    case ChartWorkspaceDocumentField::VideoPath:
        changed = document_.videoPath != value;
        document_.videoPath = value;
        break;
    case ChartWorkspaceDocumentField::ExtraText: {
        QVector<SimaiRawField> fields = SimaiDocument::parseUnmanagedFields(value, true);
        SimaiDocument::ensureDefaultClockCount(&fields);
        changed = document_.extraFields != fields;
        document_.extraFields = std::move(fields);
        break;
    }
    }
    if (!changed) return false;
    refreshSourceAndDirty();
    commit();
    return true;
}

bool ChartWorkspace::updateDifficultyField(
    int difficultyId, ChartWorkspaceDifficultyField field, const QString& value)
{
    if (!hasDocument_) return false;
    SimaiDifficultyData* difficulty = document_.difficulty(difficultyId);
    if (difficulty == nullptr) return false;
    switch (field) {
    case ChartWorkspaceDifficultyField::Level:
        if (difficulty->level == value) return false;
        difficulty->level = value;
        break;
    case ChartWorkspaceDifficultyField::Designer:
        if (difficulty->designer == value) return false;
        difficulty->designer = value;
        break;
    }
    refreshSourceAndDirty();
    commit();
    return true;
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

bool ChartWorkspace::addDifficulty(int difficultyId)
{
    if (!hasDocument_ || !SimaiDocument::isDifficultyId(difficultyId)
        || document_.difficulty(difficultyId) != nullptr) {
        return false;
    }
    document_.ensureDifficulty(difficultyId);
    activeDifficultyId_ = difficultyId;
    refreshSourceAndDirty();
    commit();
    return true;
}

bool ChartWorkspace::removeDifficulty(int difficultyId)
{
    if (!hasDocument_ || document_.difficulty(difficultyId) == nullptr) return false;
    document_.removeDifficulty(difficultyId);
    if (activeDifficultyId_ == difficultyId) {
        activeDifficultyId_ = resolveOpenDifficultyId(document_, 0);
    }
    refreshSourceAndDirty();
    commit();
    return true;
}

bool ChartWorkspace::unifyDesigners(const QString& canonicalName)
{
    if (!hasDocument_) return false;
    bool changed = document_.designer != canonicalName;
    document_.designer = canonicalName;
    const QVector<QPair<int, QString>> designerSlots = document_.perDifficultyDesigners();
    for (const QPair<int, QString>& slot : designerSlots) {
        if (slot.second == canonicalName) continue;
        document_.setDesignerForSlot(slot.first, canonicalName);
        changed = true;
    }
    if (!changed) return false;
    refreshSourceAndDirty();
    commit();
    return true;
}

bool ChartWorkspace::markSaved(const QString& filePath)
{
    if (!hasDocument_) return false;
    const QString nextFilePath = filePath.isEmpty() ? filePath_ : filePath;
    if (!dirty_ && filePath_ == nextFilePath) return false;
    savedSourceText_ = sourceText_;
    savedDocument_ = document_;
    dirty_ = false;
    filePath_ = nextFilePath;
    commit();
    return true;
}

ChartWorkspaceResult ChartWorkspace::closeDocument()
{
    if (!hasDocument_) return acceptWithoutChange();
    document_ = SimaiDocument();
    savedDocument_ = SimaiDocument();
    sourceText_.clear();
    savedSourceText_.clear();
    filePath_.clear();
    activeDifficultyId_ = 0;
    hasDocument_ = false;
    dirty_ = false;
    return commit();
}

ChartWorkspaceResult ChartWorkspace::revertDifficultyChart(int difficultyId)
{
    if (!hasDocument_) return reject();
    SimaiDifficultyData* difficulty = document_.difficulty(difficultyId);
    const SimaiDifficultyData* saved = savedDocument_.difficulty(difficultyId);
    if (difficulty == nullptr || saved == nullptr) return reject();
    if (difficulty->chart == saved->chart) return acceptWithoutChange();
    difficulty->chart = saved->chart;
    refreshSourceAndDirty();
    return commit();
}

QVector<int> ChartWorkspace::computeDirtyDifficultyIds() const
{
    QVector<int> ids;
    if (!hasDocument_) return ids;
    for (const int id : document_.difficultyIds()) {
        const SimaiDifficultyData* current = document_.difficulty(id);
        const SimaiDifficultyData* saved = savedDocument_.difficulty(id);
        if (current == nullptr) continue;
        // A difficulty added since the save point has no earlier record;
        // chart, level, and designer are all new, so it is changed.
        if (saved == nullptr
            || saved->chart != current->chart
            || saved->level != current->level
            || saved->designer != current->designer) {
            ids.append(id);
        }
    }
    return ids;
}

QString ChartWorkspace::textForSectionSave(int difficultyId) const
{
    if (!hasDocument_) return QString();
    if (difficultyId <= 0) return sourceText_;
    const SimaiDifficultyData* current = document_.difficulty(difficultyId);
    if (current == nullptr) return savedSourceText_;
    SimaiDocument merged = savedDocument_;
    // ensureDifficulty, not difficulty(): a difficulty created since the save
    // point has nothing on disk yet, and saving it must add it rather than
    // silently drop the work.
    merged.ensureDifficulty(difficultyId) = *current;
    return merged.toText();
}

bool ChartWorkspace::markSectionSaved(int difficultyId, const QString& filePath)
{
    if (!hasDocument_) return false;
    const QString nextFilePath = filePath.isEmpty() ? filePath_ : filePath;
    if (difficultyId <= 0) return markSaved(nextFilePath);

    const SimaiDifficultyData* current = document_.difficulty(difficultyId);
    if (current == nullptr) return false;
    savedDocument_.ensureDifficulty(difficultyId) = *current;
    savedSourceText_ = savedDocument_.toText();
    filePath_ = nextFilePath;
    // The document as a whole can still differ: other sections keep whatever
    // they had, saved or not.
    dirty_ = sourceText_ != savedSourceText_;
    commit();
    return true;
}

ChartWorkspaceSnapshot ChartWorkspace::snapshot() const
{
    return {sourceText_, filePath_, activeDifficultyId_, revision_, dirty_, hasDocument_,
            computeDirtyDifficultyIds()};
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

ChartWorkspaceResult ChartWorkspace::acceptWithoutChange() const
{
    return {true, revision_, {}};
}

void ChartWorkspace::refreshSourceAndDirty()
{
    sourceText_ = document_.toText();
    dirty_ = sourceText_ != savedSourceText_;
}

}  // namespace miacode::v2
