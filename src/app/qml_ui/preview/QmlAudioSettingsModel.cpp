#include "QmlAudioSettingsModel.h"

#include "audio/PreviewAudioSettings.h"
#include "mainwindow/MainWindow.h"
#include "ui/UiText.h"

#include <QVariantMap>

#include <array>

namespace miacode::qml_ui {

namespace {

// One row per mixer channel: its id, its UiText key, and the pair of accessors
// that read and write it. Keeping the four together is what stops a channel's
// slider and its mute button from being wired to different things.
struct ChannelSpec {
    const char* key;
    const char* labelKey;
    int (PreviewAudioSettings::*percent)() const;
    void (PreviewAudioSettings::*setPercent)(int);
    bool (PreviewAudioSettings::*muted)() const;
    void (PreviewAudioSettings::*toggleMuted)();
};

const std::array<ChannelSpec, 10>& channelSpecs()
{
    static const std::array<ChannelSpec, 10> specs{{
        {"global", "dialog.render_settings.audio.global",
         &PreviewAudioSettings::globalPercent, &PreviewAudioSettings::setGlobalPercent,
         &PreviewAudioSettings::globalMuted, &PreviewAudioSettings::toggleGlobalMuted},
        {"track", "dialog.render_settings.audio.track",
         &PreviewAudioSettings::trackPercent, &PreviewAudioSettings::setTrackPercent,
         &PreviewAudioSettings::trackMuted, &PreviewAudioSettings::toggleTrackMuted},
        {"answer", "dialog.render_settings.audio.answer",
         &PreviewAudioSettings::answerPercent, &PreviewAudioSettings::setAnswerPercent,
         &PreviewAudioSettings::answerMuted, &PreviewAudioSettings::toggleAnswerMuted},
        {"tap", "dialog.render_settings.audio.tap",
         &PreviewAudioSettings::tapPercent, &PreviewAudioSettings::setTapPercent,
         &PreviewAudioSettings::tapMuted, &PreviewAudioSettings::toggleTapMuted},
        {"ex", "dialog.render_settings.audio.ex",
         &PreviewAudioSettings::exPercent, &PreviewAudioSettings::setExPercent,
         &PreviewAudioSettings::exMuted, &PreviewAudioSettings::toggleExMuted},
        {"break", "dialog.render_settings.audio.break",
         &PreviewAudioSettings::breakPercent, &PreviewAudioSettings::setBreakPercent,
         &PreviewAudioSettings::breakMuted, &PreviewAudioSettings::toggleBreakMuted},
        {"breakSlide", "dialog.render_settings.audio.break_slide",
         &PreviewAudioSettings::breakSlidePercent, &PreviewAudioSettings::setBreakSlidePercent,
         &PreviewAudioSettings::breakSlideMuted, &PreviewAudioSettings::toggleBreakSlideMuted},
        {"slide", "dialog.render_settings.audio.slide",
         &PreviewAudioSettings::slidePercent, &PreviewAudioSettings::setSlidePercent,
         &PreviewAudioSettings::slideMuted, &PreviewAudioSettings::toggleSlideMuted},
        {"touch", "dialog.render_settings.audio.touch",
         &PreviewAudioSettings::touchPercent, &PreviewAudioSettings::setTouchPercent,
         &PreviewAudioSettings::touchMuted, &PreviewAudioSettings::toggleTouchMuted},
        {"firework", "dialog.render_settings.audio.firework",
         &PreviewAudioSettings::fireworkPercent, &PreviewAudioSettings::setFireworkPercent,
         &PreviewAudioSettings::fireworkMuted, &PreviewAudioSettings::toggleFireworkMuted},
    }};
    return specs;
}

const ChannelSpec* specForKey(const QString& key)
{
    for (const ChannelSpec& spec : channelSpecs()) {
        if (key == QLatin1String(spec.key)) {
            return &spec;
        }
    }
    return nullptr;
}

}  // namespace

QmlAudioSettingsModel::QmlAudioSettingsModel(MainWindow& backend, QObject* parent)
    : QObject(parent)
    , backend_(&backend)
{
}

QVariantList QmlAudioSettingsModel::channels() const
{
    QVariantList rows;
    if (backend_ == nullptr) {
        return rows;
    }
    const PreviewAudioSettings settings = backend_->currentPreviewAudioSettings();
    for (const ChannelSpec& spec : channelSpecs()) {
        rows.append(QVariantMap{
            {QStringLiteral("key"), QLatin1String(spec.key)},
            {QStringLiteral("label"), UiText::text(QLatin1String(spec.labelKey))},
            {QStringLiteral("percent"), (settings.*(spec.percent))()},
            {QStringLiteral("muted"), (settings.*(spec.muted))()},
        });
    }
    return rows;
}

void QmlAudioSettingsModel::setChannelPercent(const QString& key, int percent)
{
    const ChannelSpec* spec = specForKey(key);
    if (backend_ == nullptr || spec == nullptr) {
        return;
    }
    PreviewAudioSettings settings = backend_->currentPreviewAudioSettings();
    if ((settings.*(spec->percent))() == percent) {
        return;
    }
    (settings.*(spec->setPercent))(percent);
    backend_->applyPreviewAudioSettingsFromUi(settings);
    emit changed();
}

void QmlAudioSettingsModel::toggleChannelMuted(const QString& key)
{
    const ChannelSpec* spec = specForKey(key);
    if (backend_ == nullptr || spec == nullptr) {
        return;
    }
    PreviewAudioSettings settings = backend_->currentPreviewAudioSettings();
    // The value type owns the restore-level bookkeeping; the row only asks it
    // to flip, which is what keeps unmute returning to the previous level.
    (settings.*(spec->toggleMuted))();
    backend_->applyPreviewAudioSettingsFromUi(settings);
    emit changed();
}

void QmlAudioSettingsModel::saveAsSoftwareDefault()
{
    if (backend_ != nullptr) {
        backend_->savePreviewAudioSettingsAsSoftwareDefault();
    }
}

void QmlAudioSettingsModel::restoreSoftwareDefault()
{
    if (backend_ != nullptr) {
        backend_->restorePreviewAudioSettingsFromSoftwareDefault();
        emit changed();
    }
}

bool QmlAudioSettingsModel::breakSlideTailCheerMuted() const
{
    return backend_ != nullptr && backend_->currentPreviewAudioSettings().breakSlideTailCheerMuted;
}

void QmlAudioSettingsModel::setBreakSlideTailCheerMuted(bool muted)
{
    if (backend_ == nullptr || muted == breakSlideTailCheerMuted()) {
        return;
    }
    PreviewAudioSettings settings = backend_->currentPreviewAudioSettings();
    settings.breakSlideTailCheerMuted = muted;
    backend_->applyPreviewAudioSettingsFromUi(settings);
    emit changed();
}

}  // namespace miacode::qml_ui
