#pragma once

// Internal helpers shared by more than one of the MainWindow::DocumentSection
// document-flow translation units (DocumentFlow core + DocumentFileFlow +
// DocumentDesignerFlow + DocumentAutosaveFlow). Split out of the former
// MainWindow.DocumentFlow.cpp god-file. These were file-local
// (anonymous-namespace) helpers; the ones now referenced from more than one
// TU live here in a named detail namespace, marked 'inline' so they keep one
// definition across TUs. Helpers used by exactly one resulting TU stayed in
// that TU's own anonymous namespace.

#include "../../MainWindowShared.h"

#include "DialogLocalization.h"
#include "UiText.h"

#include <algorithm>

#include <QtCore>
#include <QtGui>
#include <QtWidgets>

namespace miacode::mainwindow::documentflow_detail {

// centerDialogOnAnchor / resolveProjectDataDirectoryPath etc. live in the
// shared namespace; the original file resolved them via a file-level
// using-directive, so mirror that here for the moved bodies.
using namespace miacode::mainwindow::shared;

enum class UnsavedChangesChoice {
    Save,
    Discard,
    Cancel,
};

inline QString unsavedChangesChoiceName(UnsavedChangesChoice choice)
{
    switch (choice) {
    case UnsavedChangesChoice::Save:
        return QStringLiteral("save");
    case UnsavedChangesChoice::Discard:
        return QStringLiteral("discard");
    case UnsavedChangesChoice::Cancel:
    default:
        return QStringLiteral("cancel");
    }
}

struct BackupRestoreEntry {
    QString filePath;
    QDateTime modifiedAt;
    int priority = 0;
};

inline UnsavedChangesChoice showUnsavedChangesDialog(QWidget* parent, const QString& title, const QString& text)
{
    QMessageBox dialog(
        QMessageBox::Warning,
        title,
        text,
        QMessageBox::NoButton,
        UiDialogs::effectiveParentWidget(parent)
    );
    dialog.setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    UiDialogs::configureDialogPreviewShortcuts(&dialog);
    UiDialogs::applyDetachedParentBehavior(&dialog, parent);
    QPushButton* saveButton = dialog.addButton(UiText::text(QStringLiteral("action.save")), QMessageBox::AcceptRole);
    QPushButton* discardButton = dialog.addButton(UiText::text(QStringLiteral("action.discard")), QMessageBox::DestructiveRole);
    QPushButton* cancelButton = dialog.addButton(UiText::text(QStringLiteral("action.cancel")), QMessageBox::RejectRole);
    dialog.setDefaultButton(saveButton);
    dialog.setEscapeButton(cancelButton);
    UiDialogs::localizeMessageBox(&dialog);
    centerDialogOnAnchor(&dialog, parent);
    dialog.exec();
    if (dialog.clickedButton() == saveButton) {
        return UnsavedChangesChoice::Save;
    }
    if (dialog.clickedButton() == discardButton) {
        return UnsavedChangesChoice::Discard;
    }
    return UnsavedChangesChoice::Cancel;
}

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

}  // namespace miacode::mainwindow::documentflow_detail
