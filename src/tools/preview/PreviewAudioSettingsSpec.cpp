#include <QCoreApplication>
#include <QTextStream>

#include "common/PreviewSfxAssets.h"
#include "audio/PreviewAudioSettings.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool requireNear(double actual, double expected, double epsilon, const QString& message, QTextStream& err)
{
    return require(qAbs(actual - expected) <= epsilon,
                   QStringLiteral("%1 actual=%2 expected=%3")
                       .arg(message)
                       .arg(actual, 0, 'f', 6)
                       .arg(expected, 0, 'f', 6),
                   err);
}

bool verifyAssetRouting(QTextStream& err)
{
    if (!require(
            miacode::preview_sfx::assetFileNamesForKind(QStringLiteral("judge"))
                    == QStringList{QStringLiteral("tap_perfect.wav"), QStringLiteral("judge.wav")},
            QStringLiteral("judge should prefer Majdata tap_perfect before legacy judge"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview_sfx::assetFileNamesForKind(QStringLiteral("judge_break"))
                    == QStringList{QStringLiteral("break_tap.wav"), QStringLiteral("judge_break.wav")},
            QStringLiteral("judge_break should prefer Majdata break_tap before legacy judge_break"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview_sfx::assetFileNamesForKind(QStringLiteral("break_slide_break")).isEmpty(),
            QStringLiteral("break_slide_break should not route through a separate asset kind"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview_sfx::assetFileNamesForKind(QStringLiteral("break_slide_tail_break"))
                    == QStringList{QStringLiteral("break.wav")},
            QStringLiteral("break_slide_tail_break should route to break.wav"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview_sfx::assetFileNamesForKind(QStringLiteral("touchhold"))
                    == QStringList{QStringLiteral("touch_Hold_riser.wav"), QStringLiteral("touchHold_riser.wav")},
            QStringLiteral("touchhold should prefer Majdata touch_Hold_riser before legacy touchHold_riser"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview_sfx::assetFileNamesForKind(QStringLiteral("firework"))
                    == QStringList{
                        QStringLiteral("touch_hanabi.wav"),
                        QStringLiteral("firework.wav"),
                        QStringLiteral("hanabi.wav")},
            QStringLiteral("firework should prefer Majdata touch_hanabi before legacy aliases"),
            err)) {
        return false;
    }
    return true;
}

bool verifyDirectBucketGain(QTextStream& err)
{
    PreviewAudioSettings settings;
    settings.globalVolume = 0.5;
    settings.trackVolume = 0.7;
    settings.answerVolume = 0.8;
    settings.tapVolume = 0.6;
    settings.exVolume = 0.4;
    settings.breakVolume = 0.3;
    settings.breakSlideVolume = 0.7;
    settings.slideVolume = 0.2;
    settings.touchVolume = 0.9;
    settings.fireworkVolume = 1.0;
    settings.normalize();

    if (!requireNear(previewTrackVolume(settings), 0.35, 1e-9, QStringLiteral("track gain should be track * global"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("answer")), 0.40, 1e-9, QStringLiteral("answer should use direct bucket gain"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("judge")), 0.30, 1e-9, QStringLiteral("judge should map to Tap without extra scaling"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("ex")), 0.20, 1e-9, QStringLiteral("ex should use direct EX bucket gain"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("break")), 0.15, 1e-9, QStringLiteral("break should use direct Break bucket gain"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("judge_break")), 0.15, 1e-9, QStringLiteral("judge_break should share Break bucket"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("break_touch")), 0.15, 1e-9, QStringLiteral("break_touch should share Break bucket"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("slide")), 0.10, 1e-9, QStringLiteral("slide should use direct Slide bucket gain"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("break_slide_start")), 0.35, 1e-9, QStringLiteral("break_slide_start should use direct Break Slide bucket gain"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("break_slide_tail_break")), 0.35, 1e-9, QStringLiteral("break_slide_tail_break should use direct Break Slide bucket gain"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("judge_break_slide")), 0.35, 1e-9, QStringLiteral("judge_break_slide should use direct Break Slide bucket gain"), err)) {
        return false;
    }
    settings.breakSlideTailCheerMuted = true;
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("break")), 0.15, 1e-9, QStringLiteral("break bucket gain should stay unchanged when break-slide tail break is muted"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("judge_break_slide")), 0.35, 1e-9, QStringLiteral("muted break-slide tail break should keep judge_break_slide volume unchanged"), err)) {
        return false;
    }
    settings.breakSlideTailCheerMuted = false;
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("break_slide")), 0.35, 1e-9, QStringLiteral("break_slide should use direct Break Slide bucket gain"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("break_slide_break")), 0.0, 1e-9, QStringLiteral("break_slide_break should not be a routed SFX kind"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("touch")), 0.45, 1e-9, QStringLiteral("touch should use direct Touch bucket gain"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("touchhold")), 0.45, 1e-9, QStringLiteral("touchhold should share Touch bucket"), err)) {
        return false;
    }
    if (!requireNear(previewSfxVolumeForKind(settings, QStringLiteral("firework")), 0.50, 1e-9, QStringLiteral("firework should use direct Firework bucket gain"), err)) {
        return false;
    }
    return true;
}

bool verifyLegacyMigration(QTextStream& err)
{
    QJsonObject legacy;
    legacy.insert(QStringLiteral("master_volume"), 0.4);
    legacy.insert(QStringLiteral("master_restore_volume"), 0.4);
    legacy.insert(QStringLiteral("bgm_volume"), 0.9);
    legacy.insert(QStringLiteral("answer_volume"), 0.8);
    legacy.insert(QStringLiteral("judge_volume"), 0.2);
    legacy.insert(QStringLiteral("break_volume"), 0.3);
    legacy.insert(QStringLiteral("slide_volume"), 0.1);
    legacy.insert(QStringLiteral("break_slide_volume"), 0.7);
    legacy.insert(QStringLiteral("touch_volume"), 0.25);
    legacy.insert(QStringLiteral("touchhold_volume"), 0.5);
    legacy.insert(QStringLiteral("firework_volume"), 0.6);

    const PreviewAudioSettings settings = PreviewAudioSettings::fromJson(legacy);
    if (!requireNear(settings.globalVolume, 0.4, 1e-9, QStringLiteral("legacy master should migrate to global"), err)) {
        return false;
    }
    if (!requireNear(settings.trackVolume, 0.9, 1e-9, QStringLiteral("legacy bgm should migrate to track"), err)) {
        return false;
    }
    if (!requireNear(settings.tapVolume, 0.2, 1e-9, QStringLiteral("legacy judge should migrate to tap"), err)) {
        return false;
    }
    if (!requireNear(settings.breakVolume, 0.3, 1e-9, QStringLiteral("legacy break should stay on Break bucket"), err)) {
        return false;
    }
    if (!requireNear(settings.breakSlideVolume, 0.7, 1e-9, QStringLiteral("legacy break_slide should migrate to Break Slide bucket"), err)) {
        return false;
    }
    if (!requireNear(settings.slideVolume, 0.1, 1e-9, QStringLiteral("legacy slide should stay on slide bucket"), err)) {
        return false;
    }
    if (!requireNear(settings.touchVolume, 0.5, 1e-9, QStringLiteral("legacy touch and touchhold should merge into touch by max"), err)) {
        return false;
    }
    if (!requireNear(settings.fireworkVolume, 0.6, 1e-9, QStringLiteral("legacy firework should migrate to firework"), err)) {
        return false;
    }
    return true;
}

bool verifyNewSchemaPreference(QTextStream& err)
{
    QJsonObject mixed;
    mixed.insert(QStringLiteral("global_volume"), 0.3);
    mixed.insert(QStringLiteral("track_volume"), 0.8);
    mixed.insert(QStringLiteral("tap_volume"), 0.9);
    mixed.insert(QStringLiteral("break_volume"), 0.13);
    mixed.insert(QStringLiteral("slide_volume"), 0.11);
    mixed.insert(QStringLiteral("break_slide_volume"), 0.12);
    mixed.insert(QStringLiteral("break_slide_tail_cheer_muted"), true);
    mixed.insert(QStringLiteral("touch_volume"), 0.22);
    mixed.insert(QStringLiteral("bgm_volume"), 0.4);
    mixed.insert(QStringLiteral("judge_volume"), 0.2);
    mixed.insert(QStringLiteral("touchhold_volume"), 0.5);

    const PreviewAudioSettings settings = PreviewAudioSettings::fromJson(mixed);
    if (!requireNear(settings.trackVolume, 0.8, 1e-9, QStringLiteral("new track key should win over legacy bgm"), err)) {
        return false;
    }
    if (!requireNear(settings.tapVolume, 0.9, 1e-9, QStringLiteral("new tap key should win over legacy judge"), err)) {
        return false;
    }
    if (!requireNear(settings.slideVolume, 0.11, 1e-9, QStringLiteral("new slide key should win over legacy break_slide"), err)) {
        return false;
    }
    if (!requireNear(settings.breakVolume, 0.13, 1e-9, QStringLiteral("new break key should own ordinary Break gain"), err)) {
        return false;
    }
    if (!requireNear(settings.breakSlideVolume, 0.12, 1e-9, QStringLiteral("new break_slide key should own Break Slide gain"), err)) {
        return false;
    }
    if (!require(settings.breakSlideTailCheerMuted, QStringLiteral("new break-slide cheer mute key should persist"), err)) {
        return false;
    }
    if (!requireNear(settings.touchVolume, 0.22, 1e-9, QStringLiteral("new touch key should win over legacy touchhold"), err)) {
        return false;
    }
    return true;
}

bool verifyJsonShape(QTextStream& err)
{
    PreviewAudioSettings settings;
    const QJsonObject object = settings.toJson();
    if (!require(object.contains(QStringLiteral("global_volume")), QStringLiteral("new schema should write global_volume"), err)) {
        return false;
    }
    if (!require(object.contains(QStringLiteral("track_volume")), QStringLiteral("new schema should write track_volume"), err)) {
        return false;
    }
    if (!require(object.contains(QStringLiteral("tap_volume")), QStringLiteral("new schema should write tap_volume"), err)) {
        return false;
    }
    if (!require(object.contains(QStringLiteral("break_slide_tail_cheer_muted")), QStringLiteral("new schema should write break-slide tail break filter option"), err)) {
        return false;
    }
    if (!require(object.contains(QStringLiteral("break_slide_volume")), QStringLiteral("new schema should write break_slide_volume"), err)) {
        return false;
    }
    if (!require(!object.contains(QStringLiteral("bgm_volume")), QStringLiteral("new schema should stop writing bgm_volume"), err)) {
        return false;
    }
    if (!require(!object.contains(QStringLiteral("judge_volume")), QStringLiteral("new schema should stop writing judge_volume"), err)) {
        return false;
    }
    if (!require(!object.contains(QStringLiteral("touchhold_volume")), QStringLiteral("new schema should stop writing touchhold_volume"), err)) {
        return false;
    }
    return true;
}

bool verifyApplicationCheerPreference(QTextStream& err)
{
    QJsonObject preview{
        {QStringLiteral("audio"), QJsonObject{{QStringLiteral("break_slide_tail_cheer_muted"), false}}},
        {QStringLiteral("break_slide_tail_cheer_muted"), true},
    };
    if (!require(resolveBreakSlideTailCheerMutedPreference(preview),
                 QStringLiteral("canonical preview sibling key should win over audio preset mirror"), err)) {
        return false;
    }
    preview.remove(QStringLiteral("break_slide_tail_cheer_muted"));
    if (!require(!resolveBreakSlideTailCheerMutedPreference(preview),
                 QStringLiteral("legacy audio preset field should remain a migration fallback"), err)) {
        return false;
    }
    PreviewAudioSettings preset;
    preset.globalVolume = 0.25;
    preset.breakSlideTailCheerMuted = false;
    const PreviewAudioSettings applied = previewAudioSettingsWithBreakSlideTailCheerPreference(preset, true);
    return requireNear(applied.globalVolume, 0.25, 1e-9,
                       QStringLiteral("applying canonical cheer preference should preserve preset mix"), err)
        && require(applied.breakSlideTailCheerMuted,
                   QStringLiteral("applying a local preset should not revert canonical cheer preference"), err);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyAssetRouting(err)) {
        return 1;
    }
    if (!verifyDirectBucketGain(err)) {
        return 1;
    }
    if (!verifyLegacyMigration(err)) {
        return 1;
    }
    if (!verifyNewSchemaPreference(err)) {
        return 1;
    }
    if (!verifyJsonShape(err)) {
        return 1;
    }
    if (!verifyApplicationCheerPreference(err)) {
        return 1;
    }

    out << "preview_audio_settings_spec ok" << Qt::endl;
    return 0;
}
