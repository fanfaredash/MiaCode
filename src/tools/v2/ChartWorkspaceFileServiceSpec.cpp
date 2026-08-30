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
    const auto saved = files.save(5);
    QFile savedOpened(openedPath);
    savedOpened.open(QIODevice::ReadOnly);
    ok &= expect(saved.accepted && !workspace.snapshot().dirty
                     && savedOpened.readAll().contains("(120){4}2,"),
                 QStringLiteral("save atomically writes the workspace and establishes its save point"), out);

    const auto savedAs = files.saveAs(savedPath, 0);
    ok &= expect(savedAs.accepted && workspace.snapshot().filePath == savedPath,
                 QStringLiteral("save as changes file identity only after a successful write"), out);

    const auto reopenedFromDirectory = files.open(directory.path());
    ok &= expect(reopenedFromDirectory.accepted
                     && workspace.snapshot().filePath == maidataPath,
                 QStringLiteral("opening a chart directory resolves its maidata.txt child"), out);

    // Saving one difficulty leaves the others on disk as they were. A single
    // question about "the document" cannot express that, which is why the
    // unsaved-changes flow walks the changed difficulties one at a time.
    const QString twoDifficulties =
        QStringLiteral("&lv_5=12\n&inote_5=(120){4}1,\n&lv_6=13\n&inote_6=(120){4}3,\n");
    const QString multiPath = directory.filePath(QStringLiteral("multi.txt"));
    ok &= expect(writeBytes(multiPath, twoDifficulties.toUtf8()),
                 QStringLiteral("a two-difficulty document is prepared"), out);
    ok &= expect(files.open(multiPath).accepted,
                 QStringLiteral("the two-difficulty document opens clean"), out);

    workspace.selectDifficulty(5);
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}5,"));
    workspace.selectDifficulty(6);
    workspace.replaceActiveDifficultyChart(QStringLiteral("(120){4}6,"));
    ok &= expect(workspace.snapshot().dirtyDifficultyIds.size() == 2,
                 QStringLiteral("both difficulties are reported changed"), out);

    ok &= expect(files.save(5).accepted, QStringLiteral("saving one difficulty succeeds"), out);
    QFile multiFile(multiPath);
    multiFile.open(QIODevice::ReadOnly);
    const QByteArray afterSectionSave = multiFile.readAll();
    ok &= expect(afterSectionSave.contains("(120){4}5,")
                     && afterSectionSave.contains("(120){4}3,")
                     && !afterSectionSave.contains("(120){4}6,"),
                 QStringLiteral("only the saved difficulty reaches the file"), out);
    ok &= expect(workspace.snapshot().dirty
                     && workspace.snapshot().dirtyDifficultyIds == QVector<int>{6},
                 QStringLiteral("the other difficulty stays unsaved and still says so"), out);
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
