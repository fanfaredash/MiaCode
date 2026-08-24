#include <QString>
#include <QTextStream>

#include "audio/BassPreviewSfxSchedulerPolicy.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

}  // namespace

int main()
{
    using miacode::preview_audio::bass::SfxSchedulerAnchor;
    using miacode::preview_audio::bass::chartSecondForMixerSecond;
    using miacode::preview_audio::bass::mixerSecondForChartSecond;
    using miacode::preview_audio::bass::shouldLogDisarm;

    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    ok &= require(
        shouldLogDisarm(true, false, -1),
        QStringLiteral("an active scheduler disarm remains visible"), err);
    ok &= require(
        shouldLogDisarm(false, true, -1),
        QStringLiteral("a disarm with a sync remains visible"), err);
    ok &= require(
        shouldLogDisarm(false, false, 0),
        QStringLiteral("a disarm with a group remains visible"), err);
    ok &= require(
        !shouldLogDisarm(false, false, -1),
        QStringLiteral("only a proven no-op disarm is suppressed"), err);

    const SfxSchedulerAnchor oneX {10.0, 120.0, 1.0};
    ok &= require(
        mixerSecondForChartSecond(oneX, 12.5) == 122.5,
        QStringLiteral("one-times chart seconds advance the master mixer equally"), err);

    const SfxSchedulerAnchor halfX {10.0, 120.0, 0.5};
    ok &= require(
        mixerSecondForChartSecond(halfX, 12.5) == 125.0,
        QStringLiteral("half-speed chart seconds map through the active playback rate"), err);

    const SfxSchedulerAnchor invalidRate {10.0, 120.0, 0.0};
    ok &= require(
        mixerSecondForChartSecond(invalidRate, 12.5) == 122.5,
        QStringLiteral("an invalid rate falls back to one-times scheduling"), err);

    const SfxSchedulerAnchor rebuildAnchor {5.0, 100.0, 1.0};
    ok &= require(
        chartSecondForMixerSecond(rebuildAnchor, 150.0) == 55.0,
        QStringLiteral("a settings rebuild reanchors at the current master position, not the last SFX"), err);

    if (ok) {
        out << "BASS preview SFX scheduler policy spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
