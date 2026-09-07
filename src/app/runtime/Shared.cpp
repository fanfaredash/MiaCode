#include "runtime/Shared.h"

#include "app/v2/ApplicationServices.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/MiniaudioFileAccess.h"
#include "common/WaveformCache.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QFontDatabase>
#include <QStringList>
#include <QtMath>

namespace miacode::runtime::shared {

const QList<double> kEditorLineSpacingFactorOptions{
    1.0, 1.5, 2.0, 3.0, 5.0,
};

namespace {

const QList<double> kPreviewPlaybackRateOptions{
    0.25, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0,
};

QString normalizeLanguageToken(QString token)
{
    token = token.trimmed().toLower();
    token.replace('-', '_');
    return token;
}

}  // namespace

double normalizeEditorLineSpacingFactor(double factor)
{
    if (kEditorLineSpacingFactorOptions.isEmpty()) {
        return kEditorLineSpacingFactorDefault;
    }
    double best = kEditorLineSpacingFactorOptions.first();
    double bestDiff = qAbs(best - factor);
    for (double candidate : kEditorLineSpacingFactorOptions) {
        const double diff = qAbs(candidate - factor);
        if (diff < bestDiff) {
            best = candidate;
            bestDiff = diff;
        }
    }
    return best;
}

QString editorLineSpacingFactorLabel(double factor)
{
    if (qFuzzyCompare(factor + 1.0, 1.0)) {
        return QStringLiteral("0x");
    }
    const QString text = QString::number(factor, 'f', qFuzzyCompare(factor, qRound(factor)) ? 0 : 1);
    return text + QStringLiteral("x");
}

int nearestPreviewPlaybackRateIndex(double rate)
{
    if (kPreviewPlaybackRateOptions.isEmpty()) {
        return -1;
    }
    int bestIndex = 0;
    double bestDiff = qAbs(kPreviewPlaybackRateOptions.first() - rate);
    for (int index = 1; index < kPreviewPlaybackRateOptions.size(); ++index) {
        const double diff = qAbs(kPreviewPlaybackRateOptions[index] - rate);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIndex = index;
        }
    }
    return bestIndex;
}

double steppedPreviewPlaybackRate(double rate, int direction)
{
    const int currentIndex = nearestPreviewPlaybackRateIndex(rate);
    if (currentIndex < 0) {
        return qMax(0.25, rate);
    }
    const int targetIndex = qBound(0, currentIndex + direction, kPreviewPlaybackRateOptions.size() - 1);
    return kPreviewPlaybackRateOptions[targetIndex];
}

SimaiNativeValidationLocale uiValidationLocale()
{
    // One implementation, owned by the non-Widget application layer. The name
    // remains as the shared runtime entry point for the parser/UI locale map.
    return miacode::v2::uiValidationLocale();
}

QByteArray autosaveContentSignature(const QString& text)
{
    return QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256);
}

QString resolveProjectDataDirectoryPath(const QString& filePath)
{
    return miacode::waveform::projectDataDirectoryPathForFile(filePath);
}

void appendStartupTimingStage(const QString& stage, qint64 elapsedMs, qint64 deltaMs)
{
    miacode::debug_log::appendStartupTimingStage(stage, elapsedMs, deltaMs);
}

QFont editorFont(int pointSize)
{
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    static const QString bundledEditorFontFamily = []() -> QString {
        const int fontId = QFontDatabase::addApplicationFont(QStringLiteral(":/fonts/maple_mono_cn.ttf"));
        const QStringList families = QFontDatabase::applicationFontFamilies(fontId);
        return families.isEmpty() ? QString() : families.first();
    }();
    if (!bundledEditorFontFamily.isEmpty()) {
        font.setFamily(bundledEditorFontFamily);
    }
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    if (pointSize > 0) {
        font.setPointSize(pointSize);
    }
    font.setStyleStrategy(QFont::PreferAntialias);
    font.setHintingPreference(QFont::PreferNoHinting);
    return font;
}

int blockSpacingPixelsForPointSize(int pointSize, double spacingFactor)
{
    const int baseSpacing = qBound(1, qRound(static_cast<double>(pointSize) * 0.18), 6);
    return qMax(0, qRound(static_cast<double>(baseSpacing) * qMax(0.0, spacingFactor)));
}

qint64 fileLastModifiedMs(const QFileInfo& fileInfo)
{
    return fileInfo.exists() ? fileInfo.lastModified().toMSecsSinceEpoch() : -1;
}

double probeAudioDurationSeconds(const QString& trackPath)
{
    if (trackPath.isEmpty() || !QFileInfo::exists(trackPath)) {
        return 0.0;
    }

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 1, 48000);
    ma_decoder decoder;
    if (miacode::audio_io::decoderInitFile(trackPath, &config, &decoder) != MA_SUCCESS) {
        return 0.0;
    }

    ma_uint64 totalFrames = 0;
    const bool ok = ma_decoder_get_length_in_pcm_frames(&decoder, &totalFrames) == MA_SUCCESS
        && totalFrames > 0;
    ma_decoder_uninit(&decoder);
    if (!ok) {
        return 0.0;
    }

    return static_cast<double>(totalFrames) / 48000.0;
}

// See the declaration in Shared.h for why this must stay the only writer of
// `playing_`. Moved verbatim from ShellHost.cpp's `Session::setPreviewPlayingFlag`
// (Stage 4.9d-4b-2); the broadcast target changed from `Session::presentationChanged`
// (which only forwarded it, see SessionBootstrap.cpp) to `notifications` directly,
// the same retargeting PlaybackState.cpp's `updatePauseButtonAppearance` already did
// for the sibling presentation announcement. Stage 4.9e-4 retargeted the
// parameter from RuntimeContext::State to RuntimeContext::PlaybackState (see
// Shared.h) — kept the parameter NAME `state` so the `state_?\.playing_`
// single-writer grep/spec pattern still matches this exact line.
void writePreviewPlayingFlag(
    RuntimeContext::PlaybackState& state,
    miacode::v2::ShellNotifications& notifications,
    bool playing)
{
    if (state.playing_ == playing) {
        return;
    }
    state.playing_ = playing;
    state.previewTransportState_ = playing
        ? miacode::v2::PlaybackTransportState::Playing
        : (state.previewTransportState_ == miacode::v2::PlaybackTransportState::Stopped
               ? miacode::v2::PlaybackTransportState::Stopped
               : miacode::v2::PlaybackTransportState::Paused);
    QMetaObject::invokeMethod(
        &notifications,
        [&notifications]() { emit notifications.presentationChanged(); },
        Qt::QueuedConnection);
}

}  // namespace miacode::runtime::shared
