#include "VideoExportSettings.h"

#include "VideoExportRuntimePolicy.h"

#include <QRegularExpression>
#include <QtNumeric>

namespace miacode::video_export {

namespace {

template <std::size_t N>
int closestOption(int requested, const std::array<int, N>& options, bool preferHigherOnTie)
{
    int closest = options.front();
    int closestDelta = qAbs(requested - closest);
    for (int candidate : options) {
        const int delta = qAbs(requested - candidate);
        if (delta < closestDelta || (preferHigherOnTie && delta == closestDelta && candidate > closest)) {
            closest = candidate;
            closestDelta = delta;
        }
    }
    return closest;
}

}  // namespace

int normalizedVideoExportAudioBitrateKbps(int requested)
{
    return closestOption(requested, kVideoExportAudioBitrateOptionsKbps, false);
}

int normalizedVideoExportFps(int requested)
{
    return closestOption(requested, kVideoExportFpsOptions, true);
}

QString videoExportPreferencePresetToken(VideoExportPreset preset)
{
    return preset == VideoExportPreset::Fast
        ? QStringLiteral("fast")
        : QStringLiteral("high_quality");
}

VideoExportPreset videoExportPresetFromPreference(const QJsonValue& value, VideoExportPreset fallback)
{
    if (!value.isString()) {
        return fallback;
    }
    const QString token = value.toString().trimmed();
    if (token.compare(QStringLiteral("high_quality"), Qt::CaseInsensitive) == 0
        || token.compare(QStringLiteral("high_compression"), Qt::CaseInsensitive) == 0) {
        return VideoExportPreset::HighQuality;
    }
    if (token.compare(QStringLiteral("fast"), Qt::CaseInsensitive) == 0) {
        return VideoExportPreset::Fast;
    }
    return fallback;
}

VideoExportSizePreset videoExportSizePresetFromPreference(
    const QJsonValue& value,
    VideoExportSizePreset fallback)
{
    const QString token = value.toString().trimmed();
    if (token.compare(QStringLiteral("compact"), Qt::CaseInsensitive) == 0) {
        return VideoExportSizePreset::Compact;
    }
    if (token.compare(QStringLiteral("ultra_compact"), Qt::CaseInsensitive) == 0) {
        return VideoExportSizePreset::UltraCompact;
    }
    if (token.compare(QStringLiteral("ultra_compact_with_pv"), Qt::CaseInsensitive) == 0) {
        return VideoExportSizePreset::UltraCompactWithPv;
    }
    if (token.compare(QStringLiteral("standard"), Qt::CaseInsensitive) == 0) {
        return VideoExportSizePreset::Standard;
    }
    return fallback;
}

void applyVideoExportPreferences(const QJsonObject& settings, VideoExportTask* task)
{
    if (task == nullptr) {
        return;
    }
    const int width = settings.value(QStringLiteral("resolution_width")).toInt(task->outputWidth);
    const int height = settings.value(QStringLiteral("resolution_height")).toInt(task->outputHeight);
    if (width > 0 && height > 0) {
        task->outputWidth = width;
        task->outputHeight = height;
    }
    task->fps = normalizedVideoExportFps(settings.value(QStringLiteral("fps")).toInt(task->fps));
    task->audioBitrateKbps = normalizedVideoExportAudioBitrateKbps(
        settings.value(QStringLiteral("audio_bitrate_kbps")).toInt(task->audioBitrateKbps));
    task->preset = videoExportPresetFromPreference(settings.value(QStringLiteral("preset")), task->preset);
    task->sizePreset = videoExportSizePresetFromPreference(
        settings.value(QStringLiteral("size_preset")), task->sizePreset);
    task->clockCountEnabled = settings.value(QStringLiteral("clock_count_enabled"))
                                  .toBool(task->clockCountEnabled);
    task->fixHudTextLayout = settings.value(QStringLiteral("fix_hud_text_layout"))
                                 .toBool(task->fixHudTextLayout);
    task->intro.enabled = settings.value(QStringLiteral("add_intro")).toBool(task->intro.enabled);
    task->intro.backgroundMode = settings.value(QStringLiteral("intro_background_mode"))
                                     .toString(task->intro.backgroundMode);
    task->intro.customBackgroundPath = settings.value(QStringLiteral("intro_background_custom_path"))
                                           .toString(task->intro.customBackgroundPath);
    task->intro.blurBackground = settings.value(QStringLiteral("intro_background_blur"))
                                     .toBool(task->intro.blurBackground);
    task->intro.mode = settings.value(QStringLiteral("intro_card_type")).toString(task->intro.mode);
    task->intro.cardShadow = settings.value(QStringLiteral("intro_card_shadow"))
                                 .toBool(task->intro.cardShadow);
    task->intro.lvRenderMode = settings.value(QStringLiteral("intro_level_text_render"))
                                   .toBool(task->intro.lvRenderMode.compare(
                                       QStringLiteral("text"), Qt::CaseInsensitive) == 0)
        ? QStringLiteral("text")
        : QStringLiteral("atlas");
    task->intro.fontDisplayPath = settings.value(QStringLiteral("intro_card_font_display"))
                                      .toString(task->intro.fontDisplayPath);
    task->intro.fontBodyPath = settings.value(QStringLiteral("intro_card_font_body"))
                                   .toString(task->intro.fontBodyPath);
    const double introSoundVolume = settings.value(QStringLiteral("intro_sound_volume"))
                                        .toDouble(task->introSoundVolume);
    if (qIsFinite(introSoundVolume)) {
        task->introSoundVolume = qBound(0.0, introSoundVolume, 2.0);
    }
}

void appendVideoExportPreferences(QJsonObject* settings, const VideoExportTask& task)
{
    if (settings == nullptr) {
        return;
    }
    settings->insert(QStringLiteral("resolution_width"), task.outputWidth);
    settings->insert(QStringLiteral("resolution_height"), task.outputHeight);
    settings->insert(QStringLiteral("fps"), task.fps);
    settings->insert(QStringLiteral("audio_bitrate_kbps"), task.audioBitrateKbps);
    settings->insert(QStringLiteral("preset"), videoExportPreferencePresetToken(task.preset));
    settings->insert(QStringLiteral("size_preset"), videoExportSizePresetToken(task.sizePreset));
    settings->insert(QStringLiteral("clock_count_enabled"), task.clockCountEnabled);
    settings->insert(QStringLiteral("fix_hud_text_layout"), task.fixHudTextLayout);
    settings->insert(QStringLiteral("add_intro"), task.intro.enabled);
    settings->insert(QStringLiteral("intro_background_mode"), task.intro.backgroundMode);
    settings->insert(QStringLiteral("intro_background_custom_path"), task.intro.customBackgroundPath.trimmed());
    settings->insert(QStringLiteral("intro_background_blur"), task.intro.blurBackground);
    settings->insert(QStringLiteral("intro_card_type"), task.intro.mode);
    settings->insert(QStringLiteral("intro_card_shadow"), task.intro.cardShadow);
    settings->insert(
        QStringLiteral("intro_level_text_render"),
        task.intro.lvRenderMode.compare(QStringLiteral("text"), Qt::CaseInsensitive) == 0);
    settings->insert(QStringLiteral("intro_card_font_display"), task.intro.fontDisplayPath);
    settings->insert(QStringLiteral("intro_card_font_body"), task.intro.fontBodyPath);
    settings->insert(
        QStringLiteral("intro_sound_volume"),
        qBound(0.0, qIsFinite(task.introSoundVolume) ? task.introSoundVolume : 1.0, 2.0));
}

QString sanitizeVideoExportTimestamp(QString text)
{
    text.replace(QChar(0xff1a), QLatin1Char(':'));
    return text.trimmed();
}

QString formatVideoExportTimestamp(double seconds)
{
    const qint64 totalMs = qMax<qint64>(0, qRound64(seconds * 1000.0));
    const qint64 minutes = totalMs / 60000;
    const qint64 sec = (totalMs / 1000) % 60;
    const qint64 ms = totalMs % 1000;
    return QStringLiteral("%1:%2:%3")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(sec, 2, 10, QChar('0'))
        .arg(ms, 3, 10, QChar('0'));
}

bool parseVideoExportTimestamp(const QString& text, double* seconds)
{
    const QString sanitized = sanitizeVideoExportTimestamp(text);
    static const QRegularExpression re(QStringLiteral("^(\\d+)(?::(\\d{1,2}))?(?::(\\d{1,3}))?$"));
    const QRegularExpressionMatch match = re.match(sanitized);
    if (!match.hasMatch()) {
        return false;
    }
    bool minOk = false;
    const int minutes = match.captured(1).toInt(&minOk);
    if (!minOk || minutes < 0) {
        return false;
    }
    int sec = 0;
    if (!match.captured(2).isEmpty()) {
        bool secOk = false;
        sec = match.captured(2).toInt(&secOk);
        if (!secOk || sec < 0 || sec > 59) {
            return false;
        }
    }
    int ms = 0;
    if (!match.captured(3).isEmpty()) {
        bool msOk = false;
        ms = match.captured(3).toInt(&msOk);
        if (!msOk || ms < 0 || ms > 999) {
            return false;
        }
    }
    if (seconds != nullptr) {
        *seconds = static_cast<double>(minutes) * 60.0
            + static_cast<double>(sec)
            + static_cast<double>(ms) / 1000.0;
    }
    return true;
}

bool isFullRangeVideoExport(double exportStartSeconds)
{
    return exportStartSeconds <= kFullRangeVideoExportStartEpsilonSeconds;
}

void copyVideoExportUserSettings(const VideoExportTask& source, VideoExportTask* target)
{
    if (target == nullptr) {
        return;
    }
    target->outputWidth = source.outputWidth;
    target->outputHeight = source.outputHeight;
    target->fps = source.fps;
    target->audioBitrateKbps = source.audioBitrateKbps;
    target->preset = source.preset;
    target->sizePreset = source.sizePreset;
    target->backgroundBrightnessOuter = source.backgroundBrightnessOuter;
    target->backgroundBrightnessInner = source.backgroundBrightnessInner;
    target->layoutSquareScale = source.layoutSquareScale;
    target->smoothBrightness = source.smoothBrightness;
    target->backgroundScaleMode = source.backgroundScaleMode;
    target->tapFlowSpeed = source.tapFlowSpeed;
    target->touchFlowSpeed = source.touchFlowSpeed;
    target->showTimestamp = source.showTimestamp;
    target->showObjectStatsHud = source.showObjectStatsHud;
    target->showChartInfoHud = source.showChartInfoHud;
    target->fixHudTextLayout = source.fixHudTextLayout;
    target->clockCountEnabled = source.clockCountEnabled;
    target->intro.enabled = source.intro.enabled;
    target->intro.backgroundMode = source.intro.backgroundMode;
    target->intro.customBackgroundPath = source.intro.customBackgroundPath;
    target->intro.blurBackground = source.intro.blurBackground;
    target->intro.mode = source.intro.mode;
    target->intro.cardShadow = source.intro.cardShadow;
    target->intro.lvRenderMode = source.intro.lvRenderMode;
    target->intro.fontDisplayPath = source.intro.fontDisplayPath;
    target->intro.fontBodyPath = source.intro.fontBodyPath;
    target->introSoundFileName = source.introSoundFileName;
    target->introSoundVolume = qBound(
        0.0,
        qIsFinite(source.introSoundVolume) ? source.introSoundVolume : 1.0,
        2.0);
}

}  // namespace miacode::video_export
