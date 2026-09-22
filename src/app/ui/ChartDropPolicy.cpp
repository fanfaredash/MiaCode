#include "ChartDropPolicy.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>

namespace miacode::chart_drop {
namespace {

void appendUniquePath(QStringList& paths, QSet<QString>& seen, const QString& path)
{
    const QString normalized = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    const QString key = normalized.toCaseFolded();
    if (!normalized.isEmpty() && !seen.contains(key)) {
        seen.insert(key);
        paths.append(normalized);
    }
}

}  // namespace

Classification classifyLocalPaths(const QStringList& localPaths,
                                  const QStringList& supportedAudioExtensions)
{
    QStringList chartPaths;
    QStringList audioPaths;
    QSet<QString> seenChartPaths;
    QSet<QString> seenAudioPaths;
    for (const QString& path : localPaths) {
        const QFileInfo info(path);
        if (info.isDir()) {
            const QString maidataPath = QDir(info.absoluteFilePath()).filePath(QStringLiteral("maidata.txt"));
            const QFileInfo maidataInfo(maidataPath);
            if (maidataInfo.exists() && maidataInfo.isFile()) {
                appendUniquePath(chartPaths, seenChartPaths, maidataInfo.absoluteFilePath());
            }
            continue;
        }
        if (!info.exists() || !info.isFile()) {
            continue;
        }
        if (info.fileName().compare(QStringLiteral("maidata.txt"), Qt::CaseInsensitive) == 0) {
            appendUniquePath(chartPaths, seenChartPaths, info.absoluteFilePath());
            continue;
        }
        if (supportedAudioExtensions.contains(info.suffix(), Qt::CaseInsensitive)) {
            appendUniquePath(audioPaths, seenAudioPaths, info.absoluteFilePath());
        }
    }

    Classification result;
    if (chartPaths.size() > 1 || (!chartPaths.isEmpty() && !audioPaths.isEmpty())) {
        result.action = Action::Ambiguous;
        return result;
    }
    if (chartPaths.size() == 1) {
        result.action = Action::OpenChart;
        result.chartPath = chartPaths.constFirst();
        return result;
    }
    if (!audioPaths.isEmpty()) {
        result.action = Action::CreateChartsFromAudio;
        result.audioPaths = audioPaths;
    }
    return result;
}

}  // namespace miacode::chart_drop
