#include "app/v2/ChartWorkspaceFileService.h"

#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

bool expect(bool condition, const QString& message, QTextStream& out)
{
    if (!condition) out << "FAIL: " << message << Qt::endl;
    return condition;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

bool verifyOpenSaveAndSaveAs(QTextStream& out)
{
    QTemporaryDir directory;
    const QString openedPath = directory.filePath(QStringLiteral("opened.txt"));
    const QString savedPath = directory.filePath(QStringLiteral("saved.txt"));
    const QString maidataPath = directory.filePath(QStringLiteral("maidata.txt"));
    const QByteArray bomSource("\xEF\xBB\xBF&lv_5=12\n&inote_5=(120){4}1,\n");
    if (!expect(directory.isValid() && writeBytes(openedPath, bomSource)
                    && writeBytes(maidataPath, bomSource),
                QStringLiteral("temporary document is prepared"), out)) return false;

    miacode::v2::ChartWorkspace workspace;
    miacode::v2::ChartWorkspaceFileService files(workspace);
    const auto opened = files.open(openedPath);
    bool ok = expect(opened.accepted && !workspace.snapshot().dirty
                         && workspace.snapshot().filePath == openedPath,
                     QStringLiteral("open decodes a BOM document and commits one clean workspace"), out);

    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}2,"));
    const auto saved = files.save();
    QFile savedOpened(openedPath);
    savedOpened.open(QIODevice::ReadOnly);
    ok &= expect(saved.accepted && !workspace.snapshot().dirty
                     && savedOpened.readAll().contains("(120){4}2,"),
                 QStringLiteral("save atomically writes the workspace and establishes its save point"), out);

    const auto savedAs = files.saveAs(savedPath);
    ok &= expect(savedAs.accepted && workspace.snapshot().filePath == savedPath,
                 QStringLiteral("save as changes file identity only after a successful write"), out);

    const auto reopenedFromDirectory = files.open(directory.path());
    ok &= expect(reopenedFromDirectory.accepted
                     && workspace.snapshot().filePath == maidataPath,
                 QStringLiteral("opening a chart directory resolves its maidata.txt child"), out);
    return ok;
}

bool verifyFailedOpenRetainsWorkspace(QTextStream& out)
{
    miacode::v2::ChartWorkspace workspace;
    workspace.openSource(QStringLiteral("&lv_5=12\n&inote_5=(120){4}1,\n"));
    miacode::v2::ChartWorkspaceFileService files(workspace);
    const auto before = workspace.snapshot();
    const auto result = files.open(QStringLiteral("Z:/does-not-exist/maidata.txt"));
    const auto after = workspace.snapshot();
    return expect(!result.accepted && result.error == QLatin1String("open_failed")
                      && before.revision == after.revision && before.sourceText == after.sourceText,
                  QStringLiteral("failed open leaves the current workspace untouched"), out);
}

}  // namespace

int main()
{
    QTextStream out(stderr);
    if (!verifyOpenSaveAndSaveAs(out) || !verifyFailedOpenRetainsWorkspace(out)) return 1;
    QTextStream result(stdout);
    result << "Chart workspace file-service checks passed." << Qt::endl;
    return 0;
}
