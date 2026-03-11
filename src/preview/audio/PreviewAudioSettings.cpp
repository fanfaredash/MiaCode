#include "PreviewAudioSettings.h"

#include <QtGlobal>

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

void PreviewAudioSettings::normalize()
{
    bgmVolume = clamp(bgmVolume);
    answerVolume = clamp(answerVolume);
    slideVolume = clamp(slideVolume);
    breakVolume = clamp(breakVolume);
    exVolume = clamp(exVolume);
    touchVolume = clamp(touchVolume);
    touchholdVolume = clamp(touchholdVolume);
    fireworkVolume = clamp(fireworkVolume);
}

int PreviewAudioSettings::bgmPercent() const
{
    return qRound(clamp(bgmVolume) * 100.0);
}

int PreviewAudioSettings::answerPercent() const
{
    return qRound(clamp(answerVolume) * 100.0);
}

int PreviewAudioSettings::slidePercent() const
{
    return qRound(clamp(slideVolume) * 100.0);
}

int PreviewAudioSettings::breakPercent() const
{
    return qRound(clamp(breakVolume) * 100.0);
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
    bgmVolume = clamp(static_cast<double>(qBound(0, value, 100)) / 100.0);
}

void PreviewAudioSettings::setAnswerPercent(int value)
{
    answerVolume = clamp(static_cast<double>(qBound(0, value, 100)) / 100.0);
}

void PreviewAudioSettings::setSlidePercent(int value)
{
    slideVolume = clamp(static_cast<double>(qBound(0, value, 100)) / 100.0);
}

void PreviewAudioSettings::setBreakPercent(int value)
{
    breakVolume = clamp(static_cast<double>(qBound(0, value, 100)) / 100.0);
}

void PreviewAudioSettings::setExPercent(int value)
{
    exVolume = clamp(static_cast<double>(qBound(0, value, 100)) / 100.0);
}

void PreviewAudioSettings::setTouchPercent(int value)
{
    touchVolume = clamp(static_cast<double>(qBound(0, value, 100)) / 100.0);
}

void PreviewAudioSettings::setTouchholdPercent(int value)
{
    touchholdVolume = clamp(static_cast<double>(qBound(0, value, 100)) / 100.0);
}

void PreviewAudioSettings::setFireworkPercent(int value)
{
    fireworkVolume = clamp(static_cast<double>(qBound(0, value, 100)) / 100.0);
}

QJsonObject PreviewAudioSettings::toJson() const
{
    PreviewAudioSettings normalized = *this;
    normalized.normalize();
    QJsonObject object;
    object.insert("bgm_volume", normalized.bgmVolume);
    object.insert("answer_volume", normalized.answerVolume);
    object.insert("slide_volume", normalized.slideVolume);
    object.insert("break_volume", normalized.breakVolume);
    object.insert("ex_volume", normalized.exVolume);
    object.insert("touch_volume", normalized.touchVolume);
    object.insert("touchhold_volume", normalized.touchholdVolume);
    object.insert("firework_volume", normalized.fireworkVolume);
    return object;
}

PreviewAudioSettings PreviewAudioSettings::fromJson(const QJsonObject& object)
{
    PreviewAudioSettings settings;
    settings.bgmVolume = object.value("bgm_volume").toDouble(settings.bgmVolume);
    const double answerLegacySfx = object.value("sfx_volume").toDouble(settings.answerVolume);
    const double slideLegacySfx = object.value("sfx_volume").toDouble(settings.slideVolume);
    const double breakLegacySfx = object.value("sfx_volume").toDouble(settings.breakVolume);
    const double exLegacySfx = object.value("sfx_volume").toDouble(settings.exVolume);
    const double touchLegacySfx = object.value("sfx_volume").toDouble(settings.touchVolume);
    const double touchholdLegacySfx = object.value("sfx_volume").toDouble(settings.touchholdVolume);
    const double fireworkLegacySfx = object.value("sfx_volume").toDouble(settings.fireworkVolume);
    settings.answerVolume = object.value("answer_volume").toDouble(answerLegacySfx);
    settings.slideVolume = object.value("slide_volume").toDouble(slideLegacySfx);
    settings.breakVolume = object.value("break_volume").toDouble(breakLegacySfx);
    settings.exVolume = object.value("ex_volume").toDouble(exLegacySfx);
    settings.touchVolume = object.value("touch_volume").toDouble(touchLegacySfx);
    settings.touchholdVolume = object.value("touchhold_volume").toDouble(touchholdLegacySfx);
    settings.fireworkVolume = object.value("firework_volume").toDouble(fireworkLegacySfx);
    settings.normalize();
    return settings;
}
