#include <QString>
#include <QTextStream>

#include "audio/PreviewAudioRecoveryPolicy.h"

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
    using miacode::preview_audio::recovery::Reason;
    using miacode::preview_audio::recovery::decidePreviewAudioRecovery;

    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    ok &= require(
        decidePreviewAudioRecovery(/*playbackActive=*/false, /*defaultOutputChanged=*/true,
                                   /*audioClockAvailable=*/true, /*absoluteDeltaSeconds=*/1.0)
            == Reason::None,
        QStringLiteral("inactive preview never reanchors"), err);
    ok &= require(
        decidePreviewAudioRecovery(/*playbackActive=*/true, /*defaultOutputChanged=*/true,
                                   /*audioClockAvailable=*/false, /*absoluteDeltaSeconds=*/0.0)
            == Reason::DefaultOutputChanged,
        QStringLiteral("default-output changes reanchor active playback"), err);
    ok &= require(
        decidePreviewAudioRecovery(/*playbackActive=*/true, /*defaultOutputChanged=*/true,
                                   /*audioClockAvailable=*/true, /*absoluteDeltaSeconds=*/1.0)
            == Reason::DefaultOutputChanged,
        QStringLiteral("default-output changes take precedence over drift"), err);
    ok &= require(
        decidePreviewAudioRecovery(/*playbackActive=*/true, /*defaultOutputChanged=*/false,
                                   /*audioClockAvailable=*/true, /*absoluteDeltaSeconds=*/0.049)
            == Reason::None,
        QStringLiteral("49 ms audio-clock divergence remains below the guard"), err);
    ok &= require(
        decidePreviewAudioRecovery(/*playbackActive=*/true, /*defaultOutputChanged=*/false,
                                   /*audioClockAvailable=*/true, /*absoluteDeltaSeconds=*/0.050)
            == Reason::DriftExceeded,
        QStringLiteral("50 ms audio-clock divergence queues recovery"), err);
    ok &= require(
        decidePreviewAudioRecovery(/*playbackActive=*/true, /*defaultOutputChanged=*/false,
                                   /*audioClockAvailable=*/false, /*absoluteDeltaSeconds=*/1.0)
            == Reason::None,
        QStringLiteral("no audio clock cannot produce a drift recovery"), err);

    if (ok) {
        out << "Preview audio recovery policy spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
