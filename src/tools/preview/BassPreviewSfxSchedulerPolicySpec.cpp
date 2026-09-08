#include <QString>
#include <QTextStream>

#include "audio/BassPreviewMasterMixerPolicy.h"
#include "audio/BassPreviewSfxCallbackRing.h"
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
    using miacode::preview_audio::bass::masterMixerPolicyFromOverrides;
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

    const SfxSchedulerAnchor buffered {10.0, 120.0, 1.0, 0.030};
    ok &= require(
        qAbs(mixerSecondForChartSecond(buffered, 12.5) - 122.47) < 1e-9,
        QStringLiteral("an output buffer advances the mixer sync by its audible lead"), err);
    ok &= require(
        chartSecondForMixerSecond(buffered, 122.5) == 12.5,
        QStringLiteral("output buffering does not alter decode-cursor clock conversion"), err);

    const SfxSchedulerAnchor rebuildAnchor {5.0, 100.0, 1.0};
    ok &= require(
        chartSecondForMixerSecond(rebuildAnchor, 150.0) == 55.0,
        QStringLiteral("a settings rebuild reanchors at the current master position, not the last SFX"), err);

    const auto defaultPolicy = masterMixerPolicyFromOverrides(QString(), QString());
    ok &= require(
        defaultPolicy.bufferMs == 30.0 && defaultPolicy.threadCount == 4,
        QStringLiteral("the master mixer defaults to a small safety buffer and four mixing threads"), err);
    const auto legacyPolicy = masterMixerPolicyFromOverrides(
        QStringLiteral("0"), QStringLiteral("8"));
    ok &= require(
        legacyPolicy.bufferMs == 0.0 && legacyPolicy.threadCount == 8
            && legacyPolicy.bufferOverrideValid && legacyPolicy.threadOverrideValid,
        QStringLiteral("the former zero-buffer eight-thread setup remains available for A/B"), err);
    const auto invalidPolicy = masterMixerPolicyFromOverrides(
        QStringLiteral("nan"), QStringLiteral("17"));
    ok &= require(
        invalidPolicy.bufferMs == 30.0 && invalidPolicy.threadCount == 4
            && !invalidPolicy.bufferOverrideValid && !invalidPolicy.threadOverrideValid,
        QStringLiteral("invalid overrides fall back to safe defaults"), err);

    using miacode::preview_audio::bass::SfxCallbackEvent;
    using miacode::preview_audio::bass::SfxCallbackEventKind;
    using miacode::preview_audio::bass::SfxCallbackEventRing;
    using miacode::preview_audio::bass::PlayedSfxSnapshot;
    PlayedSfxSnapshot played;
    played.record(QStringLiteral("judge_break"), 0.75);
    ok &= require(
        played.mask != 0,
        QStringLiteral("callback diagnostics encode a played kind without storing QString"), err);
    SfxCallbackEventRing ring;
    for (std::size_t index = 0; index + 1 < SfxCallbackEventRing::kCapacity; ++index) {
        SfxCallbackEvent event;
        event.kind = SfxCallbackEventKind::Trigger;
        event.handle = static_cast<quint32>(index + 1);
        ok &= require(ring.tryPush(event), QStringLiteral("callback event ring accepts its usable capacity"), err);
    }
    SfxCallbackEvent overflow;
    ok &= require(
        !ring.tryPush(overflow) && ring.takeDroppedCount() == 1,
        QStringLiteral("callback event ring reports overflow without blocking"), err);
    for (std::size_t index = 0; index + 1 < SfxCallbackEventRing::kCapacity; ++index) {
        SfxCallbackEvent event;
        ok &= require(
            ring.tryPop(&event) && event.handle == static_cast<quint32>(index + 1),
            QStringLiteral("callback event ring preserves FIFO order"), err);
    }
    SfxCallbackEvent empty;
    ok &= require(
        !ring.tryPop(&empty),
        QStringLiteral("callback event ring is empty after a complete drain"), err);

    if (ok) {
        out << "BASS preview SFX scheduler policy spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
