#pragma once

#include <QString>
#include <QVector>

#include "ChartWorkspace.h"

namespace miacode::v2 {

struct ChartWorkspaceFileResult {
    bool accepted = false;
    quint64 revision = 0;
    QString error;
    QVector<ChartWorkspaceIssue> issues;
};

// File-system boundary for ChartWorkspace.  This deliberately reports failures
// as values so the QML/application layer decides how to present them.
class ChartWorkspaceFileService final
{
public:
    explicit ChartWorkspaceFileService(ChartWorkspace& workspace);

    ChartWorkspaceFileResult open(const QString& path) const;
    ChartWorkspaceFileResult save() const;
    ChartWorkspaceFileResult saveAs(const QString& path) const;

private:
    static QString decodeDocumentText(const QByteArray& bytes, bool* usedSystemEncoding);
    ChartWorkspaceFileResult writeToPath(const QString& path) const;

    ChartWorkspace* workspace_ = nullptr;
};

}  // namespace miacode::v2
