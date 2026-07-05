#include "MainWindow.EditorSection.h"
#include "../../MainWindowShared.h"

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
    state_.editorBookmarks_.clear();

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
                int legacyBookmarkDifficultyId = state_.projectLastOpenedDifficultyId_;
                if (!SimaiDocument::isDifficultyId(legacyBookmarkDifficultyId) && SimaiDocument::isDifficultyId(state_.activeDifficultyId_)) {
                    legacyBookmarkDifficultyId = state_.activeDifficultyId_;
                }
                const QVector<int> documentDifficultyIds = state_.document_.difficultyIds();
                if (!SimaiDocument::isDifficultyId(legacyBookmarkDifficultyId) && documentDifficultyIds.size() == 1) {
                    legacyBookmarkDifficultyId = documentDifficultyIds.first();
                }
                const QJsonArray bookmarks = root.value("editor_bookmarks").toArray();
                for (const QJsonValue& value : bookmarks) {
                    const QJsonObject bookmarkObject = value.toObject();
                    const int line = qMax(1, bookmarkObject.value("line").toInt(1));
                    int difficultyId = bookmarkObject.value("difficulty_id").toInt(0);
                    if (!SimaiDocument::isDifficultyId(difficultyId)) {
                        difficultyId = legacyBookmarkDifficultyId;
                    }
                    state_.editorBookmarks_.append(MainWindow::EditorBookmark{
                        bookmarkObject.value("title").toString(),
                        bookmarkObject.value("text").toString(),
                        line,
                        bookmarkObject.value("source").toString(),
                        bookmarkObject.value("comment_text").toString(),
                        bookmarkObject.value("comment_fingerprint").toString(),
                        bookmarkObject.value("context_before").toString(),
                        bookmarkObject.value("context_after").toString(),
                        bookmarkObject.value("second").toDouble(-1.0),
                        difficultyId,
                    });
                }
                std::sort(state_.editorBookmarks_.begin(), state_.editorBookmarks_.end(), [](const MainWindow::EditorBookmark& left, const MainWindow::EditorBookmark& right) {
                    if (left.difficultyId != right.difficultyId) {
                        return left.difficultyId < right.difficultyId;
                    }
                    if (left.line != right.line) {
                        return left.line < right.line;
                    }
                    return left.title.localeAwareCompare(right.title) < 0;
                });
            }
        }
    }
    refreshEditorBookmarkLines();
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
    QJsonArray bookmarks;
    for (const MainWindow::EditorBookmark& bookmark : state_.editorBookmarks_) {
        QJsonObject bookmarkObject;
        if (SimaiDocument::isDifficultyId(bookmark.difficultyId)) {
            bookmarkObject.insert("difficulty_id", bookmark.difficultyId);
        }
        bookmarkObject.insert("line", qMax(1, bookmark.line));
        bookmarkObject.insert("title", bookmark.title);
        bookmarkObject.insert("text", bookmark.text);
        if (!bookmark.source.isEmpty()) {
            bookmarkObject.insert("source", bookmark.source);
        }
        if (!bookmark.commentText.isEmpty()) {
            bookmarkObject.insert("comment_text", bookmark.commentText);
        }
        if (!bookmark.commentFingerprint.isEmpty()) {
            bookmarkObject.insert("comment_fingerprint", bookmark.commentFingerprint);
        }
        if (!bookmark.contextBefore.isEmpty()) {
            bookmarkObject.insert("context_before", bookmark.contextBefore);
        }
        if (!bookmark.contextAfter.isEmpty()) {
            bookmarkObject.insert("context_after", bookmark.contextAfter);
        }
        if (bookmark.second >= 0.0) {
            bookmarkObject.insert("second", bookmark.second);
        }
        bookmarks.append(bookmarkObject);
    }
    root.insert("editor_bookmarks", bookmarks);
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
