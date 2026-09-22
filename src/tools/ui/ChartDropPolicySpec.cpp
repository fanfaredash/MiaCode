#include "app/ui/ChartDropPolicy.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
    }
    return condition;
}

bool touch(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("test") == 4;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTemporaryDir temp;
    if (!require(temp.isValid(), QStringLiteral("temporary directory must be available"), err)) {
        return 1;
    }

    const QString chartA = QDir(temp.path()).filePath(QStringLiteral("chart-a"));
    const QString chartB = QDir(temp.path()).filePath(QStringLiteral("chart-b"));
    QDir().mkpath(chartA);
    QDir().mkpath(chartB);
    const QString maidataA = QDir(chartA).filePath(QStringLiteral("maidata.txt"));
    const QString maidataB = QDir(chartB).filePath(QStringLiteral("maidata.txt"));
    const QString audio = QDir(temp.path()).filePath(QStringLiteral("song.mp3"));
    if (!touch(maidataA) || !touch(maidataB) || !touch(audio)) {
        err << "fixture creation failed" << Qt::endl;
        return 1;
    }
    const QStringList audioExtensions{QStringLiteral("mp3"), QStringLiteral("wav")};

    const auto folder = miacode::chart_drop::classifyLocalPaths({chartA}, audioExtensions);
    const auto file = miacode::chart_drop::classifyLocalPaths({maidataA}, audioExtensions);
    const auto duplicate = miacode::chart_drop::classifyLocalPaths({chartA, maidataA}, audioExtensions);
    const auto audioOnly = miacode::chart_drop::classifyLocalPaths({audio}, audioExtensions);
    const auto multiple = miacode::chart_drop::classifyLocalPaths({maidataA, maidataB}, audioExtensions);
    const auto mixed = miacode::chart_drop::classifyLocalPaths({maidataA, audio}, audioExtensions);

    const bool ok = require(folder.action == miacode::chart_drop::Action::OpenChart
                                && folder.chartPath == QFileInfo(maidataA).absoluteFilePath(),
                            QStringLiteral("chart folder must resolve its root maidata.txt"), err)
        && require(file.action == miacode::chart_drop::Action::OpenChart,
                   QStringLiteral("maidata.txt must be an open target"), err)
        && require(duplicate.action == miacode::chart_drop::Action::OpenChart,
                   QStringLiteral("a folder and its maidata.txt must deduplicate"), err)
        && require(audioOnly.action == miacode::chart_drop::Action::CreateChartsFromAudio
                       && audioOnly.audioPaths.size() == 1,
                   QStringLiteral("supported audio must preserve chart-creation routing"), err)
        && require(multiple.action == miacode::chart_drop::Action::Ambiguous,
                   QStringLiteral("multiple charts must be rejected as ambiguous"), err)
        && require(mixed.action == miacode::chart_drop::Action::Ambiguous,
                   QStringLiteral("mixed chart and audio payloads must be rejected"), err);
    return ok ? 0 : 1;
}
