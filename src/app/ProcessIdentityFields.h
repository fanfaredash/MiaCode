#pragma once

#include <QString>

namespace miacode::app::entry {

inline QString formatProcessIdentityBuildFields(
    const QString& displayVersion,
    const QString& gitRevision,
    const QString& gitDirty)
{
    return QStringLiteral("version=%1 git_revision=%2 git_dirty=%3")
        .arg(displayVersion.trimmed().isEmpty() ? QStringLiteral("unknown")
                                                : displayVersion.trimmed())
        .arg(gitRevision.trimmed().isEmpty() ? QStringLiteral("unknown")
                                             : gitRevision.trimmed())
        .arg(gitDirty.trimmed().isEmpty() ? QStringLiteral("unknown")
                                          : gitDirty.trimmed());
}

}  // namespace miacode::app::entry
