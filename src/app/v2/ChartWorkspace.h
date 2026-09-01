#pragma once

#include <QObject>
#include <QString>
#include <QVector>

#include "core/chart/document/SimaiDocument.h"
#include "core/chart/parser/SimaiNativeParser.h"

namespace miacode::v2 {

enum class ChartWorkspaceIssueSeverity {
    Error,
    Warning,
};

struct ChartWorkspaceIssue {
    int line = 1;
    int column = 1;
    int endColumn = 1;
    ChartWorkspaceIssueSeverity severity = ChartWorkspaceIssueSeverity::Error;
    QString message;
};

struct ChartWorkspacePreflightResult {
    SimaiDocument candidate;
    QVector<ChartWorkspaceIssue> issues;
    bool accepted = false;
};

struct ChartWorkspaceSnapshot {
    QString sourceText;
    QString filePath;
    int activeDifficultyId = 0;
    quint64 revision = 0;
    bool dirty = false;
    bool hasDocument = false;
    // Which difficulties differ from the last save point in chart, level, or
    // designer. `dirty` is the file; this is the tab, whose save writes that
    // whole difficulty record.
    QVector<int> dirtyDifficultyIds;
};

struct ChartWorkspaceResult {
    bool accepted = false;
    quint64 revision = 0;
    QVector<ChartWorkspaceIssue> issues;
};

enum class ChartWorkspaceDocumentField {
    Title,
    Artist,
    First,
    Designer,
    VideoPath,
    ExtraText,
};

enum class ChartWorkspaceDifficultyField {
    Level,
    Designer,
};

// The sole document owner for the staged Qt Quick application layer.  It is
// deliberately Qt Widgets-free: consumers read snapshots and submit one
// transaction at a time, never retain a writable document copy.
class ChartWorkspace final : public QObject
{
    Q_OBJECT

public:
    explicit ChartWorkspace(QObject* parent = nullptr);

    static ChartWorkspacePreflightResult preflightSource(
        const QString& source, SimaiNativeValidationLocale locale);

    // Opening establishes a new complete-document save point from the maidata
    // field parse. Chart-syntax diagnostics are recorded, not used as a gate:
    // empty inote slots and invalid tokens are the validation panel's job, as
    // in v1 loadDocument. Full-source editing is deliberately separate so it
    // can never reset dirty state.
    ChartWorkspaceResult openSource(
        const QString& source, const QString& filePath = QString(),
        int preferredDifficultyId = 0);
    ChartWorkspaceResult replaceSource(const QString& source);
    ChartWorkspaceResult replaceActiveDifficultyChart(const QString& chartText);
    bool updateDocumentField(ChartWorkspaceDocumentField field, const QString& value);
    bool updateDifficultyField(
        int difficultyId, ChartWorkspaceDifficultyField field, const QString& value);
    bool selectDifficulty(int difficultyId);
    bool addDifficulty(int difficultyId);
    bool removeDifficulty(int difficultyId);
    bool unifyDesigners(const QString& canonicalName);
    bool markSaved(const QString& filePath = QString());
    // What a save of one section should put on disk: the last save point with
    // that one difficulty's chart brought up to date, and nothing else. Saving
    // is per section because a tab is what a person works in — leaving the
    // other difficulties at their on-disk text is the difference between
    // "save what I am doing" and "save everything I have open".
    //
    // difficultyId 0 means the whole document (the metadata tab is a view of
    // the entire file, so its section is the file).
    QString textForSectionSave(int difficultyId) const;
    // Advance the save point for that section only. Callers write
    // textForSectionSave() first; this records that it landed.
    bool markSectionSaved(int difficultyId, const QString& filePath = QString());
    // 关闭文档: no document at all, rather than an empty one. A chart with no
    // difficulties is still a chart; this is the state before any chart.
    ChartWorkspaceResult closeDocument();
    // Put one difficulty's chart back to the last save point, leaving every
    // other difficulty alone. Refused for a difficulty that did not exist at
    // that save point: there is no earlier text for it, and dropping it
    // entirely would be a structural edit, not a discard.
    ChartWorkspaceResult revertDifficultyChart(int difficultyId);

    ChartWorkspaceSnapshot snapshot() const;
    const SimaiDocument& document() const;

signals:
    // Every accepted state transaction emits exactly once with its resulting
    // monotonic identity. Consumers use that identity to reject stale work.
    void changed(quint64 revision);

private:
    ChartWorkspaceResult reject(const QVector<ChartWorkspaceIssue>& issues = {}) const;
    ChartWorkspaceResult commit();
    ChartWorkspaceResult acceptWithoutChange() const;
    void refreshSourceAndDirty();

    SimaiDocument document_;
    QVector<int> computeDirtyDifficultyIds() const;

    QString sourceText_;
    QString savedSourceText_;
    // The save point as a document, not just as text, so a per-difficulty
    // comparison is a field lookup rather than a re-parse.
    SimaiDocument savedDocument_;
    QString filePath_;
    int activeDifficultyId_ = 0;
    quint64 revision_ = 0;
    bool hasDocument_ = false;
    bool dirty_ = false;
};

}  // namespace miacode::v2
