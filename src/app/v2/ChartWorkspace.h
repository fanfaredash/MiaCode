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
};

struct ChartWorkspaceResult {
    bool accepted = false;
    quint64 revision = 0;
    QVector<ChartWorkspaceIssue> issues;
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

    ChartWorkspaceResult replaceSource(
        const QString& source, const QString& filePath = QString());
    ChartWorkspaceResult replaceActiveDifficultyChart(const QString& chartText);
    bool selectDifficulty(int difficultyId);
    bool markSaved(const QString& filePath = QString());

    ChartWorkspaceSnapshot snapshot() const;
    const SimaiDocument& document() const;

signals:
    // Every accepted state transaction emits exactly once with its resulting
    // monotonic identity. Consumers use that identity to reject stale work.
    void changed(quint64 revision);

private:
    ChartWorkspaceResult reject(const QVector<ChartWorkspaceIssue>& issues = {}) const;
    ChartWorkspaceResult commit();
    void refreshSourceAndDirty();

    SimaiDocument document_;
    QString sourceText_;
    QString savedSourceText_;
    QString filePath_;
    int activeDifficultyId_ = 0;
    quint64 revision_ = 0;
    bool hasDocument_ = false;
    bool dirty_ = false;
};

}  // namespace miacode::v2
