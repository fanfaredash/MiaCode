#pragma once

#include <QString>
#include <QStringList>

namespace miacode::chart_drop {

enum class Action {
    None,
    OpenChart,
    CreateChartsFromAudio,
    Ambiguous,
};

struct Classification {
    Action action = Action::None;
    QString chartPath;
    QStringList audioPaths;
};

Classification classifyLocalPaths(const QStringList& localPaths,
                                  const QStringList& supportedAudioExtensions);

}  // namespace miacode::chart_drop
