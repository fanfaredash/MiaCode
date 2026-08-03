#include "PreviewAudioSettings.h"

#include <QtGlobal>

namespace {

constexpr double kDefaultGlobalVolume = 0.30;
constexpr double kDefaultTrackVolume = 1.0;
constexpr double kDefaultAnswerVolume = 0.80;
constexpr double kDefaultTapVolume = 0.30;
constexpr double kDefaultExVolume = 0.30;
constexpr double kDefaultBreakVolume = 0.30;
constexpr double kDefaultBreakSlideVolume = 0.30;
constexpr double kDefaultSlideVolume = 0.30;
constexpr double kDefaultTouchVolume = 0.30;
constexpr double kDefaultFireworkVolume = 0.30;
constexpr double kMaxGlobalVolume = 1.0;
constexpr double kMuteThreshold = 0.0001;

bool isMutedVolume(double value)
{
    return PreviewAudioSettings::clamp(value) <= kMuteThreshold;
}

void normalizeVolumePair(double& currentVolume, double& restoreVolume, double fallbackVolume)
{
    currentVolume = PreviewAudioSettings::clamp(currentVolume);
    restoreVolume = PreviewAudioSettings::clamp(restoreVolume);
    if (currentVolume > kMuteThreshold) {
        restoreVolume = currentVolume;
    } else if (restoreVolume <= kMuteThreshold) {
        restoreVolume = PreviewAudioSettings::clamp(fallbackVolume);
    }
}

void setVolumePercent(double& currentVolume, double& restoreVolume, int value, double fallbackVolume)
{
    currentVolume = PreviewAudioSettings::clamp(static_cast<double>(qBound(0, value, 100)) / 100.0);
    if (currentVolume > kMuteThreshold) {
        restoreVolume = currentVolume;
    } else if (PreviewAudioSettings::clamp(restoreVolume) <= kMuteThreshold) {
        restoreVolume = PreviewAudioSettings::clamp(fallbackVolume);
    }
}

void restoreGlobalIfMuted(PreviewAudioSettings& settings)
{
    if (settings.globalMuted()) {
        settings.toggleGlobalMuted();
    }
}

void toggleMuted(double& currentVolume, double& restoreVolume, double fallbackVolume)
{
    currentVolume = PreviewAudioSettings::clamp(currentVolume);
    restoreVolume = PreviewAudioSettings::clamp(restoreVolume);
    if (currentVolume > kMuteThreshold) {
        restoreVolume = currentVolume;
        currentVolume = 0.0;
        return;
    }

    if (restoreVolume <= kMuteThreshold) {
        restoreVolume = PreviewAudioSettings::clamp(fallbackVolume);
    }
    currentVolume = restoreVolume;
}

double valueOrDefault(const QJsonObject& object, const char* key, double fallback)
{
    return object.value(QLatin1String(key)).toDouble(fallback);
}

double maxLegacyValue(double a, double b)
{
    return qMax(a, b);
}

}  // namespace

double PreviewAudioSettings::clamp(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

double PreviewAudioSettings::clampGlobal(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > kMaxGlobalVolume) {
        return kMaxGlobalVolume;
    }
    return value;
}

void PreviewAudioSettings::normalize()
{
    globalVolume = clampGlobal(globalVolume);
    globalRestoreVolume = clampGlobal(globalRestoreVolume);
    if (globalVolume > kMuteThreshold) {
        globalRestoreVolume = globalVolume;
    } else if (globalRestoreVolume <= kMuteThreshold) {
        globalRestoreVolume = kDefaultGlobalVolume;
    }

    normalizeVolumePair(trackVolume, trackRestoreVolume, kDefaultTrackVolume);
    normalizeVolumePair(answerVolume, answerRestoreVolume, kDefaultAnswerVolume);
    normalizeVolumePair(tapVolume, tapRestoreVolume, kDefaultTapVolume);
    normalizeVolumePair(exVolume, exRestoreVolume, kDefaultExVolume);
    normalizeVolumePair(breakVolume, breakRestoreVolume, kDefaultBreakVolume);
    normalizeVolumePair(breakSlideVolume, breakSlideRestoreVolume, kDefaultBreakSlideVolume);
    normalizeVolumePair(slideVolume, slideRestoreVolume, kDefaultSlideVolume);
    normalizeVolumePair(touchVolume, touchRestoreVolume, kDefaultTouchVolume);
    normalizeVolumePair(fireworkVolume, fireworkRestoreVolume, kDefaultFireworkVolume);
}

int PreviewAudioSettings::globalPercent() const
{
    return qRound(clampGlobal(globalVolume) * 100.0);
}

int PreviewAudioSettings::trackPercent() const
{
    return qRound(clamp(trackVolume) * 100.0);
}

int PreviewAudioSettings::answerPercent() const
{
    return qRound(clamp(answerVolume) * 100.0);
}

int PreviewAudioSettings::tapPercent() const
{
    return qRound(clamp(tapVolume) * 100.0);
}

int PreviewAudioSettings::exPercent() const
{
    return qRound(clamp(exVolume) * 100.0);
}

int PreviewAudioSettings::breakPercent() const
{
    return qRound(clamp(breakVolume) * 100.0);
}

int PreviewAudioSettings::breakSlidePercent() const
{
    return qRound(clamp(breakSlideVolume) * 100.0);
}

int PreviewAudioSettings::slidePercent() const
{
    return qRound(clamp(slideVolume) * 100.0);
}

int PreviewAudioSettings::touchPercent() const
{
    return qRound(clamp(touchVolume) * 100.0);
}

int PreviewAudioSettings::fireworkPercent() const
{
    return qRound(clamp(fireworkVolume) * 100.0);
}

void PreviewAudioSettings::setGlobalPercent(int value)
{
    globalVolume = clampGlobal(static_cast<double>(qBound(0, value, 100)) / 100.0);
    if (globalVolume > kMuteThreshold) {
        globalRestoreVolume = globalVolume;
    } else if (clampGlobal(globalRestoreVolume) <= kMuteThreshold) {
        globalRestoreVolume = kDefaultGlobalVolume;
    }
}

void PreviewAudioSettings::setTrackPercent(int value)
{
    setVolumePercent(trackVolume, trackRestoreVolume, value, kDefaultTrackVolume);
    if (trackVolume > kMuteThreshold) {
        restoreGlobalIfMuted(*this);
    }
}

void PreviewAudioSettings::setAnswerPercent(int value)
{
    setVolumePercent(answerVolume, answerRestoreVolume, value, kDefaultAnswerVolume);
    if (answerVolume > kMuteThreshold) {
        restoreGlobalIfMuted(*this);
    }
}

void PreviewAudioSettings::setTapPercent(int value)
{
    setVolumePercent(tapVolume, tapRestoreVolume, value, kDefaultTapVolume);
    if (tapVolume > kMuteThreshold) {
        restoreGlobalIfMuted(*this);
    }
}

void PreviewAudioSettings::setExPercent(int value)
{
    setVolumePercent(exVolume, exRestoreVolume, value, kDefaultExVolume);
    if (exVolume > kMuteThreshold) {
        restoreGlobalIfMuted(*this);
    }
}

void PreviewAudioSettings::setBreakPercent(int value)
{
    setVolumePercent(breakVolume, breakRestoreVolume, value, kDefaultBreakVolume);
    if (breakVolume > kMuteThreshold) {
        restoreGlobalIfMuted(*this);
    }
}

void PreviewAudioSettings::setBreakSlidePercent(int value)
{
    setVolumePercent(breakSlideVolume, breakSlideRestoreVolume, value, kDefaultBreakSlideVolume);
    if (breakSlideVolume > kMuteThreshold) {
        restoreGlobalIfMuted(*this);
    }
}

void PreviewAudioSettings::setSlidePercent(int value)
{
    setVolumePercent(slideVolume, slideRestoreVolume, value, kDefaultSlideVolume);
    if (slideVolume > kMuteThreshold) {
        restoreGlobalIfMuted(*this);
    }
}

void PreviewAudioSettings::setTouchPercent(int value)
{
    setVolumePercent(touchVolume, touchRestoreVolume, value, kDefaultTouchVolume);
    if (touchVolume > kMuteThreshold) {
        restoreGlobalIfMuted(*this);
    }
}

void PreviewAudioSettings::setFireworkPercent(int value)
{
    setVolumePercent(fireworkVolume, fireworkRestoreVolume, value, kDefaultFireworkVolume);
    if (fireworkVolume > kMuteThreshold) {
        restoreGlobalIfMuted(*this);
    }
}

bool PreviewAudioSettings::globalMuted() const
{
    return isMutedVolume(globalVolume);
}

bool PreviewAudioSettings::trackMuted() const
{
    return isMutedVolume(trackVolume);
}

bool PreviewAudioSettings::answerMuted() const
{
    return isMutedVolume(answerVolume);
}

bool PreviewAudioSettings::tapMuted() const
{
    return isMutedVolume(tapVolume);
}

bool PreviewAudioSettings::exMuted() const
{
    return isMutedVolume(exVolume);
}

bool PreviewAudioSettings::breakMuted() const
{
    return isMutedVolume(breakVolume);
}

bool PreviewAudioSettings::breakSlideMuted() const
{
    return isMutedVolume(breakSlideVolume);
}

bool PreviewAudioSettings::slideMuted() const
{
    return isMutedVolume(slideVolume);
}

bool PreviewAudioSettings::touchMuted() const
{
    return isMutedVolume(touchVolume);
}

bool PreviewAudioSettings::fireworkMuted() const
{
    return isMutedVolume(fireworkVolume);
}

bool PreviewAudioSettings::allNonTrackMuted() const
{
    return answerMuted()
        && tapMuted()
        && exMuted()
        && breakMuted()
        && breakSlideMuted()
        && slideMuted()
        && touchMuted()
        && fireworkMuted();
}

void PreviewAudioSettings::toggleGlobalMuted()
{
    globalVolume = clampGlobal(globalVolume);
    globalRestoreVolume = clampGlobal(globalRestoreVolume);
    if (globalVolume > kMuteThreshold) {
        globalRestoreVolume = globalVolume;
        globalVolume = 0.0;
        return;
    }
    if (globalRestoreVolume <= kMuteThreshold) {
        globalRestoreVolume = kDefaultGlobalVolume;
    }
    globalVolume = globalRestoreVolume;
}

void PreviewAudioSettings::toggleTrackMuted()
{
    if (trackMuted()) {
        restoreGlobalIfMuted(*this);
    }
    toggleMuted(trackVolume, trackRestoreVolume, kDefaultTrackVolume);
}

void PreviewAudioSettings::toggleAnswerMuted()
{
    if (answerMuted()) {
        restoreGlobalIfMuted(*this);
    }
    toggleMuted(answerVolume, answerRestoreVolume, kDefaultAnswerVolume);
}

void PreviewAudioSettings::toggleTapMuted()
{
    if (tapMuted()) {
        restoreGlobalIfMuted(*this);
    }
    toggleMuted(tapVolume, tapRestoreVolume, kDefaultTapVolume);
}

void PreviewAudioSettings::toggleExMuted()
{
    if (exMuted()) {
        restoreGlobalIfMuted(*this);
    }
    toggleMuted(exVolume, exRestoreVolume, kDefaultExVolume);
}

void PreviewAudioSettings::toggleBreakMuted()
{
    if (breakMuted()) {
        restoreGlobalIfMuted(*this);
    }
    toggleMuted(breakVolume, breakRestoreVolume, kDefaultBreakVolume);
}

void PreviewAudioSettings::toggleBreakSlideMuted()
{
    if (breakSlideMuted()) {
        restoreGlobalIfMuted(*this);
    }
    toggleMuted(breakSlideVolume, breakSlideRestoreVolume, kDefaultBreakSlideVolume);
}

void PreviewAudioSettings::toggleSlideMuted()
{
    if (slideMuted()) {
        restoreGlobalIfMuted(*this);
    }
    toggleMuted(slideVolume, slideRestoreVolume, kDefaultSlideVolume);
}

void PreviewAudioSettings::toggleTouchMuted()
{
    if (touchMuted()) {
        restoreGlobalIfMuted(*this);
    }
    toggleMuted(touchVolume, touchRestoreVolume, kDefaultTouchVolume);
}

void PreviewAudioSettings::toggleFireworkMuted()
{
    if (fireworkMuted()) {
        restoreGlobalIfMuted(*this);
    }
    toggleMuted(fireworkVolume, fireworkRestoreVolume, kDefaultFireworkVolume);
}

void PreviewAudioSettings::toggleAllNonTrackMuted()
{
    if (!allNonTrackMuted()) {
        if (!answerMuted()) {
            toggleAnswerMuted();
        }
        if (!tapMuted()) {
            toggleTapMuted();
        }
        if (!exMuted()) {
            toggleExMuted();
        }
        if (!breakMuted()) {
            toggleBreakMuted();
        }
        if (!breakSlideMuted()) {
            toggleBreakSlideMuted();
        }
        if (!slideMuted()) {
            toggleSlideMuted();
        }
        if (!touchMuted()) {
            toggleTouchMuted();
        }
        if (!fireworkMuted()) {
            toggleFireworkMuted();
        }
        return;
    }

    toggleAnswerMuted();
    toggleTapMuted();
    toggleExMuted();
    toggleBreakMuted();
    toggleBreakSlideMuted();
    toggleSlideMuted();
    toggleTouchMuted();
    toggleFireworkMuted();
}

QJsonObject PreviewAudioSettings::toJson() const
{
    PreviewAudioSettings normalized = *this;
    normalized.normalize();
    QJsonObject object;
    object.insert("global_volume", normalized.globalVolume);
    object.insert("global_restore_volume", normalized.globalRestoreVolume);
    object.insert("track_volume", normalized.trackVolume);
    object.insert("track_restore_volume", normalized.trackRestoreVolume);
    object.insert("answer_volume", normalized.answerVolume);
    object.insert("answer_restore_volume", normalized.answerRestoreVolume);
    object.insert("tap_volume", normalized.tapVolume);
    object.insert("tap_restore_volume", normalized.tapRestoreVolume);
    object.insert("ex_volume", normalized.exVolume);
    object.insert("ex_restore_volume", normalized.exRestoreVolume);
    object.insert("break_volume", normalized.breakVolume);
    object.insert("break_restore_volume", normalized.breakRestoreVolume);
    object.insert("break_slide_volume", normalized.breakSlideVolume);
    object.insert("break_slide_restore_volume", normalized.breakSlideRestoreVolume);
    object.insert("slide_volume", normalized.slideVolume);
    object.insert("slide_restore_volume", normalized.slideRestoreVolume);
    object.insert("break_slide_tail_cheer_muted", normalized.breakSlideTailCheerMuted);
    object.insert("mine_sfx_enabled", normalized.mineSfxEnabled);
    object.insert("touch_volume", normalized.touchVolume);
    object.insert("touch_restore_volume", normalized.touchRestoreVolume);
    object.insert("firework_volume", normalized.fireworkVolume);
    object.insert("firework_restore_volume", normalized.fireworkRestoreVolume);
    return object;
}

PreviewAudioSettings PreviewAudioSettings::fromJson(const QJsonObject& object)
{
    PreviewAudioSettings settings;
    const bool hasNewBucketSchema = object.contains("global_volume")
        || object.contains("track_volume")
        || object.contains("tap_volume");

    settings.globalVolume = valueOrDefault(
        object,
        "global_volume",
        valueOrDefault(object, "master_volume", settings.globalVolume));
    settings.globalRestoreVolume = valueOrDefault(
        object,
        "global_restore_volume",
        valueOrDefault(
            object,
            "master_restore_volume",
            settings.globalVolume > kMuteThreshold ? settings.globalVolume : kDefaultGlobalVolume));

    settings.trackVolume = valueOrDefault(
        object,
        "track_volume",
        valueOrDefault(object, "bgm_volume", settings.trackVolume));
    settings.trackRestoreVolume = valueOrDefault(
        object,
        "track_restore_volume",
        valueOrDefault(
            object,
            "bgm_restore_volume",
            settings.trackVolume > kMuteThreshold ? settings.trackVolume : kDefaultTrackVolume));

    const double legacySfx = valueOrDefault(object, "sfx_volume", kDefaultTapVolume);

    settings.answerVolume = valueOrDefault(object, "answer_volume", legacySfx);
    settings.answerRestoreVolume = valueOrDefault(
        object,
        "answer_restore_volume",
        settings.answerVolume > kMuteThreshold ? settings.answerVolume : kDefaultAnswerVolume);

    settings.tapVolume = valueOrDefault(
        object,
        "tap_volume",
        valueOrDefault(object, "judge_volume", legacySfx));
    settings.tapRestoreVolume = valueOrDefault(
        object,
        "tap_restore_volume",
        valueOrDefault(
            object,
            "judge_restore_volume",
            settings.tapVolume > kMuteThreshold ? settings.tapVolume : kDefaultTapVolume));

    settings.exVolume = valueOrDefault(object, "ex_volume", legacySfx);
    settings.exRestoreVolume = valueOrDefault(
        object,
        "ex_restore_volume",
        settings.exVolume > kMuteThreshold ? settings.exVolume : kDefaultExVolume);

    settings.breakVolume = valueOrDefault(
        object,
        "break_volume",
        legacySfx);
    settings.breakRestoreVolume = valueOrDefault(
        object,
        "break_restore_volume",
        settings.breakVolume > kMuteThreshold ? settings.breakVolume : kDefaultBreakVolume);

    settings.breakSlideVolume = valueOrDefault(
        object,
        "break_slide_volume",
        settings.breakVolume);
    settings.breakSlideRestoreVolume = valueOrDefault(
        object,
        "break_slide_restore_volume",
        settings.breakSlideVolume > kMuteThreshold ? settings.breakSlideVolume : kDefaultBreakSlideVolume);

    const double legacySlide = valueOrDefault(object, "slide_volume", legacySfx);
    settings.slideVolume = hasNewBucketSchema
        ? valueOrDefault(object, "slide_volume", legacySlide)
        : legacySlide;
    settings.slideRestoreVolume = valueOrDefault(
        object,
        "slide_restore_volume",
        hasNewBucketSchema
            ? (settings.slideVolume > kMuteThreshold ? settings.slideVolume : kDefaultSlideVolume)
            : settings.slideVolume > kMuteThreshold ? settings.slideVolume : kDefaultSlideVolume);

    settings.breakSlideTailCheerMuted = object.value(QLatin1String("break_slide_tail_cheer_muted")).toBool(false);
    settings.mineSfxEnabled = object.value(QLatin1String("mine_sfx_enabled")).toBool(true);

    const double legacyTouch = maxLegacyValue(
        valueOrDefault(object, "touch_volume", legacySfx),
        valueOrDefault(object, "touchhold_volume", legacySfx));
    settings.touchVolume = hasNewBucketSchema
        ? valueOrDefault(object, "touch_volume", legacyTouch)
        : legacyTouch;
    settings.touchRestoreVolume = valueOrDefault(
        object,
        "touch_restore_volume",
        hasNewBucketSchema
            ? (settings.touchVolume > kMuteThreshold ? settings.touchVolume : kDefaultTouchVolume)
            : valueOrDefault(
                object,
                "touchhold_restore_volume",
                settings.touchVolume > kMuteThreshold ? settings.touchVolume : kDefaultTouchVolume));

    const double legacyFirework = valueOrDefault(
        object,
        "firework_volume",
        valueOrDefault(object, "hanabi_volume", legacySfx));
    settings.fireworkVolume = valueOrDefault(object, "firework_volume", legacyFirework);
    settings.fireworkRestoreVolume = valueOrDefault(
        object,
        "firework_restore_volume",
        valueOrDefault(
            object,
            "hanabi_restore_volume",
            settings.fireworkVolume > kMuteThreshold ? settings.fireworkVolume : kDefaultFireworkVolume));

    settings.normalize();
    return settings;
}

PreviewAudioSettings makePreviewLatencyAuditionLevels(const PreviewAudioSettings& mix, int sfxPercent)
{
    PreviewAudioSettings levels = mix;
    // Keep the song audio at the user's normal effective volume by collapsing the
    // chained (global * track) multiplier into a single trackVolume term and
    // pinning globalVolume to 1.0. SFX kinds then get the sandbox slider's value
    // directly (independent of the user's normal mix), so the test taps are easy
    // to hear regardless of how quietly the user runs the song.
    const double effectiveTrackVolume = previewTrackVolume(mix);
    levels.globalVolume = 1.0;
    levels.globalRestoreVolume = 1.0;
    levels.trackVolume = PreviewAudioSettings::clamp(effectiveTrackVolume);
    levels.trackRestoreVolume = levels.trackVolume;

    const double sfxLevel = qBound(0.0, static_cast<double>(sfxPercent) / 100.0, 1.0);
    levels.tapVolume = sfxLevel;
    levels.tapRestoreVolume = sfxLevel;
    levels.exVolume = sfxLevel;
    levels.exRestoreVolume = sfxLevel;
    levels.breakVolume = sfxLevel;
    levels.breakRestoreVolume = sfxLevel;
    levels.breakSlideVolume = sfxLevel;
    levels.breakSlideRestoreVolume = sfxLevel;
    levels.slideVolume = sfxLevel;
    levels.slideRestoreVolume = sfxLevel;
    levels.touchVolume = sfxLevel;
    levels.touchRestoreVolume = sfxLevel;
    levels.fireworkVolume = sfxLevel;
    levels.fireworkRestoreVolume = sfxLevel;
    levels.answerVolume = sfxLevel;
    levels.answerRestoreVolume = sfxLevel;
    levels.normalize();
    return levels;
}

bool resolveBreakSlideTailCheerMutedPreference(const QJsonObject& preview)
{
    const QJsonValue canonical = preview.value(QStringLiteral("break_slide_tail_cheer_muted"));
    if (canonical.isBool()) {
        return canonical.toBool();
    }
    const QJsonObject audio = preview.value(QStringLiteral("audio")).toObject();
    return audio.value(QStringLiteral("break_slide_tail_cheer_muted")).toBool(false);
}

PreviewAudioSettings previewAudioSettingsWithBreakSlideTailCheerPreference(
    PreviewAudioSettings settings,
    bool muted)
{
    settings.breakSlideTailCheerMuted = muted;
    settings.normalize();
    return settings;
}
