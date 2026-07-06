#include "MainWindow.EditorSection.h"
#include "../../MainWindowShared.h"
#include "../document/MainWindow.DocumentSection.h"

#include <QtCore>

namespace {
QString legacyProjectRenderStateFilePath(const QString& currentFilePath)
{
    if (currentFilePath.isEmpty()) {
        return QString();
    }
    const QDir projectDir(QFileInfo(currentFilePath).absolutePath());
    return projectDir.filePath(QStringLiteral(".miacode_render_settings.json"));
}

}  // namespace

QString MainWindow::EditorSection::resolveProjectRenderStateFilePath() const
{
    const QString projectDataDirectoryPath =
        miacode::mainwindow::shared::resolveProjectDataDirectoryPath(state_.currentFilePath_);
    if (projectDataDirectoryPath.isEmpty()) {
        return QString();
    }
    return QDir(projectDataDirectoryPath).filePath(QStringLiteral("miacode_settings.json"));
}

void MainWindow::EditorSection::loadProjectRenderState()
{
    state_.projectLastOpenedDifficultyId_ = 0;

    const QString path = resolveProjectRenderStateFilePath();
    const QString legacyPath = legacyProjectRenderStateFilePath(state_.currentFilePath_);
    const QString loadPath = QFileInfo::exists(path) ? path : legacyPath;
    if (!loadPath.isEmpty()) {
        QFile file(loadPath);
        if (file.exists() && file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QJsonParseError parseError;
            const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
            if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
                const QJsonObject root = doc.object();
                const int savedDifficultyId = root.value("last_opened_difficulty").toInt(0);
                if (SimaiDocument::isDifficultyId(savedDifficultyId)) {
                    state_.projectLastOpenedDifficultyId_ = savedDifficultyId;
                }
            }
        }
    }
}

void MainWindow::EditorSection::adoptBookmarksForLoadedDocument()
{
    state_.editorBookmarks_.clear();
    state_.activeBookmarkDifficultyId_ = 0;
    state_.activeBookmarkLine_ = -1;
    syncBookmarksFromEditorText();
}

void MainWindow::EditorSection::saveProjectRenderState() const
{
    const QString path = resolveProjectRenderStateFilePath();
    if (path.isEmpty()) {
        return;
    }

    const QFileInfo pathInfo(path);
    if (!QDir().mkpath(pathInfo.absolutePath())) {
        return;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return;
    }

    QJsonObject root;
    root.insert("last_opened_difficulty", state_.projectLastOpenedDifficultyId_);
    root.insert("schema", "miacode_settings_v1");
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        return;
    }
    if (!file.commit()) {
        return;
    }

    const QString legacyPath = legacyProjectRenderStateFilePath(state_.currentFilePath_);
    if (!legacyPath.isEmpty() && legacyPath != path) {
        QFile::remove(legacyPath);
    }
}

void MainWindow::EditorSection::removeProjectRenderState() const
{
    const QString path = resolveProjectRenderStateFilePath();
    if (path.isEmpty()) {
        return;
    }
    QFile::remove(path);
    const QString legacyPath = legacyProjectRenderStateFilePath(state_.currentFilePath_);
    if (!legacyPath.isEmpty() && legacyPath != path) {
        QFile::remove(legacyPath);
    }
}

QString MainWindow::resolveProjectRenderStateFilePath() const
{
    return editorSection_->resolveProjectRenderStateFilePath();
}

void MainWindow::loadProjectRenderState()
{
    editorSection_->loadProjectRenderState();
}

void MainWindow::saveProjectRenderState() const
{
    editorSection_->saveProjectRenderState();
}

void MainWindow::removeProjectRenderState() const
{
    editorSection_->removeProjectRenderState();
}
