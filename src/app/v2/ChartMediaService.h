#pragma once

#include "common/ChartMediaImport.h"

#include <QString>
#include <QStringList>

namespace miacode::v2 {

// Widgets-free chart-folder media transaction boundary. It owns no document
// state: callers commit `&video=` only after import/remove returns successfully.
class ChartMediaService final
{
public:
    using Kind = miacode::chart_media_import::Kind;
    using Result = miacode::chart_media_import::Result;

    Result importMedia(const QString& chartPath, const QString& sourcePath, Kind kind) const;
    Result removePv(const QString& chartPath, const QString& chartVideoFieldValue) const;

    static QStringList existingCandidates(const QString& chartPath, Kind kind);
    static bool sourceIsSupported(const QString& sourcePath, Kind kind);
    static QString targetPath(const QString& chartPath, const QString& sourcePath, Kind kind);
    static bool isConflictingCandidate(const QString& candidatePath,
                                       const QString& sourcePath,
                                       const QString& targetPath);
};

}  // namespace miacode::v2
