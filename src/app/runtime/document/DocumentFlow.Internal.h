#pragma once

// Internal helpers shared by more than one of the miacode::runtime::DocumentSessionHost
// document-flow translation units (DocumentFlow core + DocumentFileFlow +
// DocumentDesignerFlow + DocumentAutosaveFlow). Split out of the former
// MainWindow.DocumentFlow.cpp god-file. These were file-local
// (anonymous-namespace) helpers; the ones now referenced from more than one
// TU live here in a named detail namespace, marked 'inline' so they keep one
// definition across TUs. Helpers used by exactly one resulting TU stayed in
// that TU's own anonymous namespace.

#include "runtime/Shared.h"

#include <algorithm>

#include <QtCore>

namespace miacode::runtime::document_detail {

// resolveProjectDataDirectoryPath and the other filesystem helpers live in the
// shared namespace; the original file resolved them via a file-level
// using-directive, so mirror that here for the moved bodies.
using namespace miacode::runtime::shared;

struct BackupRestoreEntry {
    QString filePath;
    QDateTime modifiedAt;
    int priority = 0;
};


inline QString autosaveEntryDirectoryPathForFile(const QString& filePath)
{
    const QString projectDataDirectoryPath = resolveProjectDataDirectoryPath(filePath);
    if (projectDataDirectoryPath.isEmpty()) {
        return QString();
    }

    QString fileContainerName = QFileInfo(filePath).fileName().trimmed();
    if (fileContainerName.isEmpty()) {
        fileContainerName = QStringLiteral("maidata.txt");
    }
    return QDir(projectDataDirectoryPath).filePath(QStringLiteral(".autosave/%1").arg(fileContainerName));
}

inline QString autosaveLatestFilePath(const QString& autosaveDirectoryPath)
{
    QString baseName = QFileInfo(autosaveDirectoryPath).fileName().trimmed();
    if (baseName.isEmpty()) {
        baseName = QStringLiteral("latest");
    }
    return QDir(autosaveDirectoryPath).filePath(baseName + QStringLiteral(".bak"));
}

inline QString autosaveHistoryDirectoryPath(const QString& autosaveDirectoryPath)
{
    return QDir(autosaveDirectoryPath).filePath(QStringLiteral("history"));
}

inline QList<BackupRestoreEntry> backupRestoreEntriesForAutosaveDirectory(const QString& autosaveDirectoryPath)
{
    QList<BackupRestoreEntry> entries;
    if (autosaveDirectoryPath.isEmpty()) {
        return entries;
    }

    const QString latestPath = autosaveLatestFilePath(autosaveDirectoryPath);
    const QFileInfo latestInfo(latestPath);
    if (latestInfo.exists() && latestInfo.isFile()) {
        entries.append(BackupRestoreEntry{latestInfo.absoluteFilePath(), latestInfo.lastModified(), 1});
    }

    const QString crashRecoveryPath = QDir(autosaveDirectoryPath).filePath(
        QFileInfo(autosaveDirectoryPath).fileName() + QStringLiteral(".crash_recovery")
    );
    const QFileInfo crashRecoveryInfo(crashRecoveryPath);
    if (crashRecoveryInfo.exists() && crashRecoveryInfo.isFile()) {
        entries.append(BackupRestoreEntry{crashRecoveryInfo.absoluteFilePath(), crashRecoveryInfo.lastModified(), 2});
    }

    QDir historyDir(autosaveHistoryDirectoryPath(autosaveDirectoryPath));
    const QFileInfoList historyFiles = historyDir.entryInfoList(
        QStringList{QStringLiteral("*.bak")},
        QDir::Files | QDir::NoDotAndDotDot,
        QDir::Name
    );
    for (const QFileInfo& historyInfo : historyFiles) {
        entries.append(BackupRestoreEntry{historyInfo.absoluteFilePath(), historyInfo.lastModified(), 0});
    }

    std::sort(entries.begin(), entries.end(), [](const BackupRestoreEntry& lhs, const BackupRestoreEntry& rhs) {
        if (lhs.modifiedAt != rhs.modifiedAt) {
            return lhs.modifiedAt > rhs.modifiedAt;
        }
        return lhs.priority > rhs.priority;
    });
    return entries;
}

inline QString latestBackupRestoreFilePathForChart(const QString& chartFilePath)
{
    const QList<BackupRestoreEntry> entries =
        backupRestoreEntriesForAutosaveDirectory(autosaveEntryDirectoryPathForFile(chartFilePath));
    return entries.isEmpty() ? QString() : entries.constFirst().filePath;
}

}  // namespace miacode::runtime::document_detail
