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

    // The workspace this service writes for. There is exactly one per
    // application assembly; a second workspace behind the file boundary is how
    // "save" and "what is on screen" drift apart.
    ChartWorkspace& workspace() const { return *workspace_; }

    ChartWorkspaceFileResult open(const QString& path) const;
    // Write an empty chart at `path`, creating its folder if needed. The
    // workspace is untouched — the caller opens the file afterwards, so a new
    // document arrives through the same door as any other and starts on a real
    // save point rather than dirty-on-arrival.
    ChartWorkspaceFileResult createEmptyDocument(const QString& path) const;
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
