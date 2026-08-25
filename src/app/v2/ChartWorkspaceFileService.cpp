#include "ChartWorkspaceFileService.h"

#include <QDir>
#include <QFile>
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
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (normalizedPath.isEmpty()) {
        return {false, workspace_->snapshot().revision, QStringLiteral("path_empty"), {}};
    }
    QFile file(normalizedPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {false, workspace_->snapshot().revision, QStringLiteral("open_failed"), {}};
    }
    bool usedSystemEncoding = false;
    const QString text = decodeDocumentText(file.readAll(), &usedSystemEncoding);
    Q_UNUSED(usedSystemEncoding);
    const ChartWorkspaceResult result = workspace_->replaceSource(text, normalizedPath);
    return {result.accepted, result.revision,
            result.accepted ? QString() : QStringLiteral("validation_failed"), result.issues};
}

ChartWorkspaceFileResult ChartWorkspaceFileService::save() const
{
    if (workspace_ == nullptr) return {false, 0, QStringLiteral("workspace_unavailable"), {}};
    return writeToPath(workspace_->snapshot().filePath);
}

ChartWorkspaceFileResult ChartWorkspaceFileService::saveAs(const QString& path) const
{
    return writeToPath(path);
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

ChartWorkspaceFileResult ChartWorkspaceFileService::writeToPath(const QString& path) const
{
    if (workspace_ == nullptr) return {false, 0, QStringLiteral("workspace_unavailable"), {}};
    const QString normalizedPath = path.isEmpty() ? QString() : QDir::cleanPath(path);
    const ChartWorkspaceSnapshot before = workspace_->snapshot();
    if (!before.hasDocument) return {false, before.revision, QStringLiteral("document_unavailable"), {}};
    if (normalizedPath.isEmpty()) return {false, before.revision, QStringLiteral("path_empty"), {}};

    const QByteArray payload = QStringEncoder(QStringConverter::Utf8).encode(workspace_->document().toText());
    QSaveFile file(normalizedPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return {false, before.revision, QStringLiteral("open_failed"), {}};
    }
    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return {false, before.revision, QStringLiteral("write_failed"), {}};
    }
    if (!file.commit()) return {false, before.revision, QStringLiteral("commit_failed"), {}};

    workspace_->markSaved(normalizedPath);
    return {true, workspace_->snapshot().revision, QString(), {}};
}

}  // namespace miacode::v2
