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
    bool usedSystemEncoding = false;
};

// File-system boundary for ChartWorkspace.  This deliberately reports failures
// as values so the QML/application layer decides how to present them.
class ChartWorkspaceFileService final
{
public:
    explicit ChartWorkspaceFileService(ChartWorkspace& workspace);

    ChartWorkspaceFileResult open(const QString& path) const;
    // Saves one section — the difficulty being worked in — leaving every other
    // difficulty on disk exactly as it was. difficultyId 0 saves the whole
    // document, which is what the whole-source view means by its section.
    ChartWorkspaceFileResult save(int difficultyId) const;
    ChartWorkspaceFileResult saveAs(const QString& path, int difficultyId) const;

private:
    static QString decodeDocumentText(const QByteArray& bytes, bool* usedSystemEncoding);
    ChartWorkspaceFileResult writeToPath(const QString& path, int difficultyId) const;

    ChartWorkspace* workspace_ = nullptr;
};

}  // namespace miacode::v2
