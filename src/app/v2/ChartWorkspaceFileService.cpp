#include "ChartWorkspaceFileService.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringConverter>

namespace miacode::v2 {

ChartWorkspaceFileService::ChartWorkspaceFileService(ChartWorkspace& workspace)
    : workspace_(&workspace)
{
}

ChartWorkspaceFileResult ChartWorkspaceFileService::open(const QString& path) const
{
    if (workspace_ == nullptr) return {false, 0, QStringLiteral("workspace_unavailable"), {}};
    QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return {false, workspace_->snapshot().revision, QStringLiteral("path_empty"), {}};
    }
    const QFileInfo inputInfo(normalizedPath);
    if (inputInfo.isDir()) {
        normalizedPath = QDir(inputInfo.absoluteFilePath()).filePath(QStringLiteral("maidata.txt"));
    }
    QFile file(normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {false, workspace_->snapshot().revision, QStringLiteral("open_failed"), {}};
    }
    bool usedSystemEncoding = false;
    const QString text = decodeDocumentText(file.readAll(), &usedSystemEncoding);
    const ChartWorkspaceResult result = workspace_->openSource(text, normalizedPath);
    return {result.accepted, result.revision,
            result.accepted ? QString() : QStringLiteral("validation_failed"), result.issues,
            usedSystemEncoding};
}

ChartWorkspaceFileResult ChartWorkspaceFileService::createEmptyDocument(const QString& path) const
{
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) return {false, 0, QStringLiteral("path_empty"), {}};
    const QDir parent = QFileInfo(normalizedPath).absoluteDir();
    if (!parent.exists() && !QDir().mkpath(parent.absolutePath())) {
        return {false, 0, QStringLiteral("mkpath_failed"), {}};
    }
    const QByteArray payload =
        QStringEncoder(QStringConverter::Utf8).encode(SimaiDocument::createEmpty().toText());
    QSaveFile file(normalizedPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {false, 0, QStringLiteral("open_failed"), {}};
    }
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return {false, 0, QStringLiteral("write_failed"), {}};
    }
    if (!file.commit()) return {false, 0, QStringLiteral("commit_failed"), {}};
    return {true, 0, QString(), {}};
}

ChartWorkspaceFileResult ChartWorkspaceFileService::save(int difficultyId) const
{
    if (workspace_ == nullptr) return {false, 0, QStringLiteral("workspace_unavailable"), {}};
    return writeToPath(workspace_->snapshot().filePath, difficultyId);
}

ChartWorkspaceFileResult ChartWorkspaceFileService::saveAs(const QString& path, int difficultyId) const
{
    return writeToPath(path, difficultyId);
}

QString ChartWorkspaceFileService::decodeDocumentText(
    const QByteArray& bytes, bool* usedSystemEncoding)
{
    if (usedSystemEncoding != nullptr) *usedSystemEncoding = false;
    if (bytes.startsWith("\xEF\xBB\xBF")) return QString::fromUtf8(bytes.mid(3));

    QStringDecoder utf8Decoder(QStringConverter::Utf8);
    const QString text = utf8Decoder.decode(bytes);
    if (!utf8Decoder.hasError()) return text;

    if (usedSystemEncoding != nullptr) *usedSystemEncoding = true;
    QStringDecoder systemDecoder(QStringConverter::System);
    return systemDecoder.decode(bytes);
}

ChartWorkspaceFileResult ChartWorkspaceFileService::writeToPath(
    const QString& path, int difficultyId) const
{
    if (workspace_ == nullptr) return {false, 0, QStringLiteral("workspace_unavailable"), {}};
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const ChartWorkspaceSnapshot before = workspace_->snapshot();
    if (!before.hasDocument) return {false, before.revision, QStringLiteral("document_unavailable"), {}};
    if (normalizedPath.isEmpty()) return {false, before.revision, QStringLiteral("path_empty"), {}};

    // Not document().toText(): that is everything open, and a section save is
    // the last save point with this one difficulty brought up to date.
    const QByteArray payload = QStringEncoder(QStringConverter::Utf8)
                                   .encode(workspace_->textForSectionSave(difficultyId));
    QSaveFile file(normalizedPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {false, before.revision, QStringLiteral("open_failed"), {}};
    }
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return {false, before.revision, QStringLiteral("write_failed"), {}};
    }
    if (!file.commit()) return {false, before.revision, QStringLiteral("commit_failed"), {}};

    workspace_->markSectionSaved(difficultyId, normalizedPath);
    return {true, workspace_->snapshot().revision, QString(), {}};
}

}  // namespace miacode::v2
