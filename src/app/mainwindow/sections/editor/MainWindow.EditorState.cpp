#include "MainWindow.EditorSection.h"
#include "../../MainWindowShared.h"
#include "../document/MainWindow.DocumentSection.h"

#include "UiText.h"

#include <QtCore>
#include <QStatusBar>

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
    // Legacy `editor_bookmarks` entries are only STAGED here; the simai file's
    // own &miacode_bookmarks= payload is authoritative and is not parsed until
    // state_.document_ is assigned. adoptBookmarksForLoadedDocument() (called
    // from DocumentSection::loadDocument) picks the winner — this ordering
    // matters because the open flow runs setCurrentFilePath (→ here) BEFORE
    // loadDocument, while onNewFile runs them in the opposite order.
    state_.legacyJsonEditorBookmarks_.clear();

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
                    state_.legacyJsonEditorBookmarks_.append(MainWindow::EditorBookmark{
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
                        bookmarkObject.value("name_locked").toBool(false),
                    });
                }
            }
        }
    }
}

namespace {
void sortEditorBookmarks(QVector<MainWindow::EditorBookmark>* bookmarks)
{
    std::sort(bookmarks->begin(), bookmarks->end(), [](const MainWindow::EditorBookmark& left, const MainWindow::EditorBookmark& right) {
        if (left.difficultyId != right.difficultyId) {
            return left.difficultyId < right.difficultyId;
        }
        if (left.line != right.line) {
            return left.line < right.line;
        }
        return left.title.localeAwareCompare(right.title) < 0;
    });
}
}  // namespace

void MainWindow::EditorSection::adoptBookmarksForLoadedDocument()
{
    state_.editorBookmarks_.clear();
    state_.activeBookmarkDifficultyId_ = 0;
    state_.activeBookmarkLine_ = -1;
    bool migratedFromLegacyJson = false;
    if (!state_.document_.bookmarks.isEmpty()) {
        for (const SimaiBookmarkData& bookmark : std::as_const(state_.document_.bookmarks)) {
            state_.editorBookmarks_.append(MainWindow::EditorBookmark{
                bookmark.name,
                QString(),
                qMax(1, bookmark.line),
                bookmark.source,
                QString(),
                bookmark.commentFingerprint,
                bookmark.contextBefore,
                bookmark.contextAfter,
                bookmark.second,
                bookmark.difficultyId,
                bookmark.nameLocked,
            });
        }
        state_.editorBookmarksInSimai_ = true;
    } else {
        state_.editorBookmarks_ = state_.legacyJsonEditorBookmarks_;
        state_.editorBookmarksInSimai_ = false;
        migratedFromLegacyJson = !state_.editorBookmarks_.isEmpty();
    }
    state_.legacyJsonEditorBookmarks_.clear();
    sortEditorBookmarks(&state_.editorBookmarks_);
    refreshEditorBookmarkLines();

    // Non-fatal load diagnostics — shown after the "Opened: …" status message
    // (this runs inside loadDocument, before it), hence the queued dispatch.
    const bool parseError = state_.document_.bookmarksParseError;
    if (parseError || migratedFromLegacyJson) {
        QTimer::singleShot(0, &owner_, [this, parseError, migratedFromLegacyJson]() {
            if (parseError) {
                owner_.statusBar()->showMessage(
                    UiText::isChineseUi()
                        ? QStringLiteral("谱面内嵌书签数据无法解析，已忽略。")
                        : QStringLiteral("Embedded bookmark data could not be parsed and was ignored."),
                    8000);
                return;
            }
            owner_.statusBar()->showMessage(
                UiText::isChineseUi()
                    ? QStringLiteral("已从旧项目状态载入书签，将在下次保存时写入谱面文件。")
                    : QStringLiteral("Bookmarks loaded from the legacy project state; they will be written into the chart file on the next save."),
                8000);
        });
    }
}

void MainWindow::EditorSection::syncBookmarksIntoDocument(SimaiDocument* document) const
{
    if (document == nullptr) {
        return;
    }
    document->bookmarks.clear();
    document->bookmarks.reserve(state_.editorBookmarks_.size());
    for (const MainWindow::EditorBookmark& bookmark : std::as_const(state_.editorBookmarks_)) {
        if (!SimaiDocument::isDifficultyId(bookmark.difficultyId)) {
            continue;
        }
        SimaiBookmarkData data;
        data.difficultyId = bookmark.difficultyId;
        data.line = qMax(1, bookmark.line);
        data.name = bookmark.title;
        data.second = bookmark.second;
        data.source = bookmark.source;
        data.commentFingerprint = bookmark.commentFingerprint;
        data.contextBefore = bookmark.contextBefore;
        data.contextAfter = bookmark.contextAfter;
        data.nameLocked = bookmark.nameLocked;
        document->bookmarks.append(data);
    }
}

void MainWindow::EditorSection::markBookmarksMutatedByUser()
{
    state_.documentDirty_ = true;
    if (owner_.documentSection_ != nullptr) {
        owner_.documentSection_->updateDirtyState();
    }
    owner_.updateWindowTitle();
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
    // Bookmarks now live in the simai file (&miacode_bookmarks=). The legacy
    // `editor_bookmarks` key is mirrored here only until the simai file has
    // become the authoritative store for this chart (first load-with-payload
    // or first save) — after that the key is dropped, which also cleans it
    // out of existing project JSON files. Older MiaCode builds keep working:
    // they simply read the key while it still exists.
    if (!state_.editorBookmarksInSimai_) {
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
            if (bookmark.nameLocked) {
                bookmarkObject.insert("name_locked", true);
            }
            bookmarks.append(bookmarkObject);
        }
        root.insert("editor_bookmarks", bookmarks);
    }
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
