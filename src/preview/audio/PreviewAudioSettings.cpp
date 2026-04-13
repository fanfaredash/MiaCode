#include "PreviewAudioSettings.h"

#include <QtGlobal>

namespace {

constexpr double kDefaultBgmVolume = 0.25;
constexpr double kDefaultMasterVolume = 1.0;
constexpr double kMaxMasterVolume = 2.0;
constexpr double kDefaultAnswerVolume = 0.35;
constexpr double kDefaultJudgeVolume = 0.15;
constexpr double kDefaultSlideVolume = 0.15;
constexpr double kDefaultBreakVolume = 0.15;
constexpr double kDefaultBreakSlideVolume = 0.15;
constexpr double kDefaultExVolume = 0.15;
constexpr double kDefaultTouchVolume = 0.15;
constexpr double kDefaultFireworkVolume = 0.15;
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

void restoreMasterIfMuted(PreviewAudioSettings& settings)
{
    if (settings.masterMuted()) {
        settings.toggleMasterMuted();
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

double PreviewAudioSettings::clampMaster(double value)
{
    if (value < 0.0) {
        return 0.0;
    }
    if (value > kMaxMasterVolume) {
        return kMaxMasterVolume;
    }
    return value;
}

void PreviewAudioSettings::normalize()
{
    masterVolume = clampMaster(masterVolume);
    masterRestoreVolume = clampMaster(masterRestoreVolume);
    if (masterVolume > kMuteThreshold) {
        masterRestoreVolume = masterVolume;
    } else if (masterRestoreVolume <= kMuteThreshold) {
        masterRestoreVolume = kDefaultMasterVolume;
    }
    normalizeVolumePair(bgmVolume, bgmRestoreVolume, kDefaultBgmVolume);
    normalizeVolumePair(answerVolume, answerRestoreVolume, kDefaultAnswerVolume);
    normalizeVolumePair(judgeVolume, judgeRestoreVolume, kDefaultJudgeVolume);
    normalizeVolumePair(slideVolume, slideRestoreVolume, kDefaultSlideVolume);
    normalizeVolumePair(breakVolume, breakRestoreVolume, kDefaultBreakVolume);
    normalizeVolumePair(breakSlideVolume, breakSlideRestoreVolume, kDefaultBreakSlideVolume);
    normalizeVolumePair(exVolume, exRestoreVolume, kDefaultExVolume);
    normalizeVolumePair(touchVolume, touchRestoreVolume, kDefaultTouchVolume);
    normalizeVolumePair(touchholdVolume, touchholdRestoreVolume, kDefaultTouchVolume);
    normalizeVolumePair(fireworkVolume, fireworkRestoreVolume, kDefaultFireworkVolume);
}

int PreviewAudioSettings::bgmPercent() const
{
    return qRound(clamp(bgmVolume) * 100.0);
}

int PreviewAudioSettings::masterPercent() const
{
    return qRound(clampMaster(masterVolume) * 100.0);
}

int PreviewAudioSettings::answerPercent() const
{
    return qRound(clamp(answerVolume) * 100.0);
}

int PreviewAudioSettings::judgePercent() const
{
    return qRound(clamp(judgeVolume) * 100.0);
}

int PreviewAudioSettings::slidePercent() const
{
    return qRound(clamp(slideVolume) * 100.0);
}

int PreviewAudioSettings::breakPercent() const
{
    return qRound(clamp(breakVolume) * 100.0);
}

int PreviewAudioSettings::breakSlidePercent() const
{
    return qRound(clamp(breakSlideVolume) * 100.0);
}

int PreviewAudioSettings::exPercent() const
{
    return qRound(clamp(exVolume) * 100.0);
}

int PreviewAudioSettings::touchPercent() const
{
    return qRound(clamp(touchVolume) * 100.0);
}

int PreviewAudioSettings::touchholdPercent() const
{
    return qRound(clamp(touchholdVolume) * 100.0);
}

int PreviewAudioSettings::fireworkPercent() const
{
    return qRound(clamp(fireworkVolume) * 100.0);
}

void PreviewAudioSettings::setBgmPercent(int value)
{
    setVolumePercent(bgmVolume, bgmRestoreVolume, value, kDefaultBgmVolume);
    if (bgmVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

void PreviewAudioSettings::setMasterPercent(int value)
{
    masterVolume = clampMaster(static_cast<double>(qBound(0, value, 200)) / 100.0);
    if (masterVolume > kMuteThreshold) {
        masterRestoreVolume = masterVolume;
    } else if (clampMaster(masterRestoreVolume) <= kMuteThreshold) {
        masterRestoreVolume = kDefaultMasterVolume;
    }
}

void PreviewAudioSettings::setAnswerPercent(int value)
{
    setVolumePercent(answerVolume, answerRestoreVolume, value, kDefaultAnswerVolume);
    if (answerVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

void PreviewAudioSettings::setJudgePercent(int value)
{
    setVolumePercent(judgeVolume, judgeRestoreVolume, value, kDefaultJudgeVolume);
    if (judgeVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

void PreviewAudioSettings::setSlidePercent(int value)
{
    setVolumePercent(slideVolume, slideRestoreVolume, value, kDefaultSlideVolume);
    if (slideVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

void PreviewAudioSettings::setBreakPercent(int value)
{
    setVolumePercent(breakVolume, breakRestoreVolume, value, kDefaultBreakVolume);
    if (breakVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

void PreviewAudioSettings::setBreakSlidePercent(int value)
{
    setVolumePercent(breakSlideVolume, breakSlideRestoreVolume, value, kDefaultBreakSlideVolume);
    if (breakSlideVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

void PreviewAudioSettings::setExPercent(int value)
{
    setVolumePercent(exVolume, exRestoreVolume, value, kDefaultExVolume);
    if (exVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

void PreviewAudioSettings::setTouchPercent(int value)
{
    setVolumePercent(touchVolume, touchRestoreVolume, value, kDefaultTouchVolume);
    if (touchVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

void PreviewAudioSettings::setTouchholdPercent(int value)
{
    setVolumePercent(touchholdVolume, touchholdRestoreVolume, value, kDefaultTouchVolume);
    if (touchholdVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

void PreviewAudioSettings::setFireworkPercent(int value)
{
    setVolumePercent(fireworkVolume, fireworkRestoreVolume, value, kDefaultFireworkVolume);
    if (fireworkVolume > kMuteThreshold) {
        restoreMasterIfMuted(*this);
    }
}

bool PreviewAudioSettings::masterMuted() const
{
    return isMutedVolume(masterVolume);
}

bool PreviewAudioSettings::bgmMuted() const
{
    return isMutedVolume(bgmVolume);
}

bool PreviewAudioSettings::answerMuted() const
{
    return isMutedVolume(answerVolume);
}

bool PreviewAudioSettings::judgeMuted() const
{
    return isMutedVolume(judgeVolume);
}

bool PreviewAudioSettings::slideMuted() const
{
    return isMutedVolume(slideVolume);
}

bool PreviewAudioSettings::breakMuted() const
{
    return isMutedVolume(breakVolume);
}

bool PreviewAudioSettings::breakSlideMuted() const
{
    return isMutedVolume(breakSlideVolume);
}

bool PreviewAudioSettings::exMuted() const
{
    return isMutedVolume(exVolume);
}

bool PreviewAudioSettings::touchMuted() const
{
    return isMutedVolume(touchVolume);
}

bool PreviewAudioSettings::touchholdMuted() const
{
    return isMutedVolume(touchholdVolume);
}

bool PreviewAudioSettings::fireworkMuted() const
{
    return isMutedVolume(fireworkVolume);
}

bool PreviewAudioSettings::allNonBgmMuted() const
{
    return answerMuted()
        && judgeMuted()
        && slideMuted()
        && breakMuted()
        && breakSlideMuted()
        && exMuted()
        && touchMuted()
        && touchholdMuted()
        && fireworkMuted();
}

void PreviewAudioSettings::toggleMasterMuted()
{
    masterVolume = clampMaster(masterVolume);
    masterRestoreVolume = clampMaster(masterRestoreVolume);
    if (masterVolume > kMuteThreshold) {
        masterRestoreVolume = masterVolume;
        masterVolume = 0.0;
        return;
    }
    if (masterRestoreVolume <= kMuteThreshold) {
        masterRestoreVolume = kDefaultMasterVolume;
    }
    masterVolume = masterRestoreVolume;
}

void PreviewAudioSettings::toggleBgmMuted()
{
    if (bgmMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(bgmVolume, bgmRestoreVolume, kDefaultBgmVolume);
}

void PreviewAudioSettings::toggleAnswerMuted()
{
    if (answerMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(answerVolume, answerRestoreVolume, kDefaultAnswerVolume);
}

void PreviewAudioSettings::toggleJudgeMuted()
{
    if (judgeMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(judgeVolume, judgeRestoreVolume, kDefaultJudgeVolume);
}

void PreviewAudioSettings::toggleSlideMuted()
{
    if (slideMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(slideVolume, slideRestoreVolume, kDefaultSlideVolume);
}

void PreviewAudioSettings::toggleBreakMuted()
{
    if (breakMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(breakVolume, breakRestoreVolume, kDefaultBreakVolume);
}

void PreviewAudioSettings::toggleBreakSlideMuted()
{
    if (breakSlideMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(breakSlideVolume, breakSlideRestoreVolume, kDefaultBreakSlideVolume);
}

void PreviewAudioSettings::toggleExMuted()
{
    if (exMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(exVolume, exRestoreVolume, kDefaultExVolume);
}

void PreviewAudioSettings::toggleTouchMuted()
{
    if (touchMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(touchVolume, touchRestoreVolume, kDefaultTouchVolume);
}

void PreviewAudioSettings::toggleTouchholdMuted()
{
    if (touchholdMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(touchholdVolume, touchholdRestoreVolume, kDefaultTouchVolume);
}

void PreviewAudioSettings::toggleFireworkMuted()
{
    if (fireworkMuted()) {
        restoreMasterIfMuted(*this);
    }
    toggleMuted(fireworkVolume, fireworkRestoreVolume, kDefaultFireworkVolume);
}

void PreviewAudioSettings::toggleAllNonBgmMuted()
{
    if (!allNonBgmMuted()) {
        if (!answerMuted()) {
            toggleAnswerMuted();
        }
        if (!judgeMuted()) {
            toggleJudgeMuted();
        }
        if (!slideMuted()) {
            toggleSlideMuted();
        }
        if (!breakMuted()) {
            toggleBreakMuted();
        }
        if (!breakSlideMuted()) {
            toggleBreakSlideMuted();
        }
        if (!exMuted()) {
            toggleExMuted();
        }
        if (!touchMuted()) {
            toggleTouchMuted();
        }
        if (!touchholdMuted()) {
            toggleTouchholdMuted();
        }
        if (!fireworkMuted()) {
            toggleFireworkMuted();
        }
        return;
    }

    toggleAnswerMuted();
    toggleJudgeMuted();
    toggleSlideMuted();
    toggleBreakMuted();
    toggleBreakSlideMuted();
    toggleExMuted();
    toggleTouchMuted();
    toggleTouchholdMuted();
    toggleFireworkMuted();
}

QJsonObject PreviewAudioSettings::toJson() const
{
    PreviewAudioSettings normalized = *this;
    normalized.normalize();
    QJsonObject object;
    object.insert("master_volume", normalized.masterVolume);
    object.insert("master_restore_volume", normalized.masterRestoreVolume);
    object.insert("bgm_volume", normalized.bgmVolume);
    object.insert("bgm_restore_volume", normalized.bgmRestoreVolume);
    object.insert("answer_volume", normalized.answerVolume);
    object.insert("answer_restore_volume", normalized.answerRestoreVolume);
    object.insert("judge_volume", normalized.judgeVolume);
    object.insert("judge_restore_volume", normalized.judgeRestoreVolume);
    object.insert("slide_volume", normalized.slideVolume);
    object.insert("slide_restore_volume", normalized.slideRestoreVolume);
    object.insert("break_volume", normalized.breakVolume);
    object.insert("break_restore_volume", normalized.breakRestoreVolume);
    object.insert("break_slide_volume", normalized.breakSlideVolume);
    object.insert("break_slide_restore_volume", normalized.breakSlideRestoreVolume);
    object.insert("ex_volume", normalized.exVolume);
    object.insert("ex_restore_volume", normalized.exRestoreVolume);
    object.insert("touch_volume", normalized.touchVolume);
    object.insert("touch_restore_volume", normalized.touchRestoreVolume);
    object.insert("touchhold_volume", normalized.touchholdVolume);
    object.insert("touchhold_restore_volume", normalized.touchholdRestoreVolume);
    object.insert("firework_volume", normalized.fireworkVolume);
    object.insert("firework_restore_volume", normalized.fireworkRestoreVolume);
    return object;
}

PreviewAudioSettings PreviewAudioSettings::fromJson(const QJsonObject& object)
{
    PreviewAudioSettings settings;
    settings.masterVolume = object.value("master_volume").toDouble(settings.masterVolume);
    settings.masterRestoreVolume = object.value("master_restore_volume").toDouble(
        settings.masterVolume > kMuteThreshold ? settings.masterVolume : kDefaultMasterVolume
    );
    settings.bgmVolume = object.value("bgm_volume").toDouble(settings.bgmVolume);
    settings.bgmRestoreVolume = object.value("bgm_restore_volume").toDouble(
        settings.bgmVolume > kMuteThreshold ? settings.bgmVolume : kDefaultBgmVolume
    );
    const double answerLegacySfx = object.value("sfx_volume").toDouble(settings.answerVolume);
    const double judgeLegacySfx = object.value("sfx_volume").toDouble(settings.judgeVolume);
    const double slideLegacySfx = object.value("sfx_volume").toDouble(settings.slideVolume);
    const double breakLegacySfx = object.value("sfx_volume").toDouble(settings.breakVolume);
    const double breakSlideLegacyVolume = object.value("slide_volume").toDouble(slideLegacySfx);
    const double exLegacySfx = object.value("sfx_volume").toDouble(settings.exVolume);
    const double touchLegacySfx = object.contains("touch_volume")
        ? object.value("touch_volume").toDouble(object.value("sfx_volume").toDouble(settings.touchVolume))
        : object.value("touchhold_volume").toDouble(object.value("sfx_volume").toDouble(settings.touchVolume));
    const double fireworkLegacySfx = object.value("sfx_volume").toDouble(settings.fireworkVolume);
    settings.answerVolume = object.value("answer_volume").toDouble(answerLegacySfx);
    settings.answerRestoreVolume = object.value("answer_restore_volume").toDouble(
        settings.answerVolume > kMuteThreshold ? settings.answerVolume : kDefaultAnswerVolume
    );
    settings.judgeVolume = object.value("judge_volume").toDouble(judgeLegacySfx);
    settings.judgeRestoreVolume = object.value("judge_restore_volume").toDouble(
        settings.judgeVolume > kMuteThreshold ? settings.judgeVolume : kDefaultJudgeVolume
    );
    settings.slideVolume = object.value("slide_volume").toDouble(slideLegacySfx);
    settings.slideRestoreVolume = object.value("slide_restore_volume").toDouble(
        settings.slideVolume > kMuteThreshold ? settings.slideVolume : kDefaultSlideVolume
    );
    settings.breakVolume = object.value("break_volume").toDouble(breakLegacySfx);
    settings.breakRestoreVolume = object.value("break_restore_volume").toDouble(
        settings.breakVolume > kMuteThreshold ? settings.breakVolume : kDefaultBreakVolume
    );
    settings.breakSlideVolume = object.value("break_slide_volume").toDouble(breakSlideLegacyVolume);
    settings.breakSlideRestoreVolume = object.value("break_slide_restore_volume").toDouble(
        settings.breakSlideVolume > kMuteThreshold ? settings.breakSlideVolume : kDefaultBreakSlideVolume
    );
    settings.exVolume = object.value("ex_volume").toDouble(exLegacySfx);
    settings.exRestoreVolume = object.value("ex_restore_volume").toDouble(
        settings.exVolume > kMuteThreshold ? settings.exVolume : kDefaultExVolume
    );
    settings.touchVolume = object.value("touch_volume").toDouble(touchLegacySfx);
    settings.touchRestoreVolume = object.value("touch_restore_volume").toDouble(
        settings.touchVolume > kMuteThreshold ? settings.touchVolume : kDefaultTouchVolume
    );
    settings.touchholdVolume = object.value("touchhold_volume").toDouble(settings.touchVolume);
    settings.touchholdRestoreVolume = object.value("touchhold_restore_volume").toDouble(
        settings.touchholdVolume > kMuteThreshold ? settings.touchholdVolume : settings.touchRestoreVolume
    );
    settings.fireworkVolume = object.value("firework_volume").toDouble(fireworkLegacySfx);
    settings.fireworkRestoreVolume = object.value("firework_restore_volume").toDouble(
        settings.fireworkVolume > kMuteThreshold ? settings.fireworkVolume : kDefaultFireworkVolume
    );
    settings.normalize();
    return settings;
}
