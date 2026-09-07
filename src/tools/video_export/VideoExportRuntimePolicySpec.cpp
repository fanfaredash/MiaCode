#include <QCoreApplication>
#include <QTextStream>

#include <optional>

#include "tools/video_export/VideoExportRuntimePolicy.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyPolicy(QTextStream& err)
{
    using miacode::video_export::effectiveVideoExportAudioBitrateKbps;
    using miacode::video_export::effectiveVideoExportX264Crf;
    using miacode::video_export::VideoExportSizePreset;
    using miacode::video_export::shouldRequestOffscreenPboReadback;
    using miacode::video_export::shouldRetryVideoExportWorkerAfterCrash;
    using miacode::video_export::shouldUsePremultipliedExportPipe;
    using miacode::video_export::videoExportSizePolicy;
    using miacode::video_export::videoExportSizePresetToken;

    const auto standard = videoExportSizePolicy(VideoExportSizePreset::Standard);
    const auto compact = videoExportSizePolicy(VideoExportSizePreset::Compact);
    const auto ultraWithPv = videoExportSizePolicy(VideoExportSizePreset::UltraCompactWithPv);
    const auto ultra = videoExportSizePolicy(VideoExportSizePreset::UltraCompact);
    if (!require(standard.x264CrfAdjustment == 0 && standard.gopSeconds == 2,
                 QStringLiteral("standard size mode must retain legacy encoder tuning"), err)
        || !require(compact.minBitrateKbps == 1800 && compact.maxBitrateKbps == 8000
                        && compact.gopSeconds == 4 && compact.x264CrfAdjustment == 1
                        && !compact.disableVideoBackground,
                    QStringLiteral("compact size policy mismatch"), err)
        || !require(ultra.minBitrateKbps == 4000 && ultra.maxBitrateKbps == 4000
                        && ultra.gopSeconds == 6 && ultra.x264CrfAdjustment == 3
                        && ultra.maxRateMultiplier == 1.0 && ultra.disableVideoBackground,
                    QStringLiteral("ultra-compact size policy mismatch"), err)
        || !require(ultraWithPv.minBitrateKbps == ultra.minBitrateKbps
                        && ultraWithPv.maxBitrateKbps == ultra.maxBitrateKbps
                        && ultraWithPv.x264CrfAdjustment == ultra.x264CrfAdjustment
                        && !ultraWithPv.disableVideoBackground,
                    QStringLiteral("ultra-compact-with-PV size policy mismatch"), err)
        || !require(videoExportSizePresetToken(VideoExportSizePreset::UltraCompactWithPv)
                        == QStringLiteral("ultra_compact_with_pv")
                        && videoExportSizePresetToken(VideoExportSizePreset::UltraCompact)
                        == QStringLiteral("ultra_compact"),
                    QStringLiteral("ultra-compact size tokens must remain distinct"), err)
        || !require(effectiveVideoExportAudioBitrateKbps(VideoExportSizePreset::Standard, 320) == 320
                        && effectiveVideoExportAudioBitrateKbps(VideoExportSizePreset::Compact, 320) == 160
                        && effectiveVideoExportAudioBitrateKbps(VideoExportSizePreset::UltraCompactWithPv, 320) == 128
                        && effectiveVideoExportAudioBitrateKbps(VideoExportSizePreset::UltraCompact, 320) == 128,
                    QStringLiteral("size-mode audio bitrate caps mismatch"), err)) {
        return false;
    }

    if (!require(effectiveVideoExportX264Crf(VideoExportSizePreset::Standard, 22) == 22
                     && effectiveVideoExportX264Crf(VideoExportSizePreset::Compact, 22) == 23
                     && effectiveVideoExportX264Crf(VideoExportSizePreset::UltraCompact, 22) == 25
                     && effectiveVideoExportX264Crf(VideoExportSizePreset::Compact, 20) == 21,
                 QStringLiteral("size-mode CRF adjustments must preserve the quality tier"), err)) {
        return false;
    }

    if (!require(
            shouldRequestOffscreenPboReadback(std::nullopt, std::nullopt),
            QStringLiteral("default export PBO policy should be enabled when no env override is set"),
            err)) {
        return false;
    }
    if (!require(
            shouldRequestOffscreenPboReadback(true, std::nullopt),
            QStringLiteral("ENABLE=1 should keep PBO requested"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRequestOffscreenPboReadback(false, std::nullopt),
            QStringLiteral("ENABLE=0 should disable PBO"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRequestOffscreenPboReadback(true, true),
            QStringLiteral("DISABLE=1 should override ENABLE=1"),
            err)) {
        return false;
    }
    if (!require(
            shouldRetryVideoExportWorkerAfterCrash(true, true, false, 1),
            QStringLiteral("CrashExit with PBO enabled should retry once"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRetryVideoExportWorkerAfterCrash(true, false, false, 1),
            QStringLiteral("CrashExit without PBO should not retry"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRetryVideoExportWorkerAfterCrash(false, true, false, 1),
            QStringLiteral("NormalExit failure should not retry"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRetryVideoExportWorkerAfterCrash(true, true, true, 1),
            QStringLiteral("Canceled crash should not retry"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRetryVideoExportWorkerAfterCrash(true, true, false, 2),
            QStringLiteral("Crash retry should happen at most once"),
            err)) {
        return false;
    }
    if (!require(
            shouldUsePremultipliedExportPipe(true, true, std::nullopt),
            QStringLiteral("fast D3D11 export should default to premultiplied transport"),
            err)) {
        return false;
    }
    if (!require(
            !shouldUsePremultipliedExportPipe(true, false, std::nullopt),
            QStringLiteral("high-quality export should keep straight RGBA transport"),
            err)) {
        return false;
    }
    if (!require(
            !shouldUsePremultipliedExportPipe(false, true, std::nullopt),
            QStringLiteral("OpenGL export should keep straight RGBA transport"),
            err)) {
        return false;
    }
    if (!require(
            !shouldUsePremultipliedExportPipe(true, true, false),
            QStringLiteral("explicit premultiplied transport opt-out should win"),
            err)) {
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyPolicy(err)) {
        return 1;
    }

    out << "video_export_runtime_policy_spec ok" << Qt::endl;
    return 0;
}
