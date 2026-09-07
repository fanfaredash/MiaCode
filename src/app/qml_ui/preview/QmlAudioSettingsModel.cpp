#include "QmlAudioSettingsModel.h"

#include "audio/PreviewAudioSettings.h"
#include "audio/PreviewAudioWorkerProtocol.h"
#include "audio/QtPreviewSfxRuntime.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewSfxSemantics.h"
#include "ui/UiText.h"

#include <QDir>
#include <QTimer>
#include <QVariantMap>

#include <array>
#include <utility>

namespace miacode::qml_ui {

namespace {

// A slider settles long before the user stops dragging; this is how long the
// mixer waits for quiet before it auditions. Same 220 ms the Widgets dialog
// used.
constexpr int kAuditionSettleMs = 220;

// One row per mixer channel: its id, its UiText key, the pair of accessors that
// read and write it, and the SFX it auditions. Keeping them together is what
// stops a channel's slider, its mute button and its audition sample from being
// wired to different things.
struct ChannelSpec {
    const char* key;
    const char* labelKey;
    int (PreviewAudioSettings::*percent)() const;
    void (PreviewAudioSettings::*setPercent)(int);
    bool (PreviewAudioSettings::*muted)() const;
    void (PreviewAudioSettings::*toggleMuted)();
    // Empty where there is nothing to play: the BGM channel's "sample" is the
    // track itself, and the master's own level is demonstrated by 演示音.
    const char* audition;
};

const std::array<ChannelSpec, 10>& channelSpecs()
{
    static const std::array<ChannelSpec, 10> specs{{
        {"global", "dialog.render_settings.audio.global",
         &PreviewAudioSettings::globalPercent, &PreviewAudioSettings::setGlobalPercent,
         &PreviewAudioSettings::globalMuted, &PreviewAudioSettings::toggleGlobalMuted, "answer"},
        {"track", "dialog.render_settings.audio.track",
         &PreviewAudioSettings::trackPercent, &PreviewAudioSettings::setTrackPercent,
         &PreviewAudioSettings::trackMuted, &PreviewAudioSettings::toggleTrackMuted, ""},
        {"answer", "dialog.render_settings.audio.answer",
         &PreviewAudioSettings::answerPercent, &PreviewAudioSettings::setAnswerPercent,
         &PreviewAudioSettings::answerMuted, &PreviewAudioSettings::toggleAnswerMuted, "answer"},
        {"tap", "dialog.render_settings.audio.tap",
         &PreviewAudioSettings::tapPercent, &PreviewAudioSettings::setTapPercent,
         &PreviewAudioSettings::tapMuted, &PreviewAudioSettings::toggleTapMuted, "judge"},
        {"ex", "dialog.render_settings.audio.ex",
         &PreviewAudioSettings::exPercent, &PreviewAudioSettings::setExPercent,
         &PreviewAudioSettings::exMuted, &PreviewAudioSettings::toggleExMuted, "ex"},
        {"break", "dialog.render_settings.audio.break",
         &PreviewAudioSettings::breakPercent, &PreviewAudioSettings::setBreakPercent,
         &PreviewAudioSettings::breakMuted, &PreviewAudioSettings::toggleBreakMuted, "break"},
        {"breakSlide", "dialog.render_settings.audio.break_slide",
         &PreviewAudioSettings::breakSlidePercent, &PreviewAudioSettings::setBreakSlidePercent,
         &PreviewAudioSettings::breakSlideMuted, &PreviewAudioSettings::toggleBreakSlideMuted,
         "break_slide"},
        {"slide", "dialog.render_settings.audio.slide",
         &PreviewAudioSettings::slidePercent, &PreviewAudioSettings::setSlidePercent,
         &PreviewAudioSettings::slideMuted, &PreviewAudioSettings::toggleSlideMuted, "slide"},
        {"touch", "dialog.render_settings.audio.touch",
         &PreviewAudioSettings::touchPercent, &PreviewAudioSettings::setTouchPercent,
         &PreviewAudioSettings::touchMuted, &PreviewAudioSettings::toggleTouchMuted, "touch"},
        {"firework", "dialog.render_settings.audio.firework",
         &PreviewAudioSettings::fireworkPercent, &PreviewAudioSettings::setFireworkPercent,
         &PreviewAudioSettings::fireworkMuted, &PreviewAudioSettings::toggleFireworkMuted,
         "firework"},
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

QmlAudioSettingsModel::QmlAudioSettingsModel(miacode::v2::PreviewSurface*& surfaceSlot,
                                             QObject* parent)
    : QObject(parent)
    , surfaceSlot_(&surfaceSlot)
{
}

QmlAudioSettingsModel::~QmlAudioSettingsModel() = default;

QVariantList QmlAudioSettingsModel::channels() const
{
    QVariantList rows;
    if (surface() == nullptr) {
        return rows;
    }
    const PreviewAudioSettings settings = surface()->audioSettings();
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
    if (surface() == nullptr || spec == nullptr) {
        return;
    }
    PreviewAudioSettings settings = surface()->audioSettings();
    if ((settings.*(spec->percent))() == percent) {
        return;
    }
    (settings.*(spec->setPercent))(percent);
    surface()->applyAudioSettings(settings);
    queueAudition(QLatin1String(spec->audition));
    emit changed();
}

void QmlAudioSettingsModel::toggleChannelMuted(const QString& key)
{
    const ChannelSpec* spec = specForKey(key);
    if (surface() == nullptr || spec == nullptr) {
        return;
    }
    PreviewAudioSettings settings = surface()->audioSettings();
    // The value type owns the restore-level bookkeeping; the row only asks it
    // to flip, which is what keeps unmute returning to the previous level.
    (settings.*(spec->toggleMuted))();
    const bool mutedNow = (settings.*(spec->muted))();
    if (key == QLatin1String("global")) {
        // Master mute carries every other channel with it, so the rows show what
        // the user just did to them rather than staying lit under a silent
        // master. The Widgets dialog spelled this out as eighteen hand-written
        // ifs; over a table it is the loop it always was.
        for (const ChannelSpec& other : channelSpecs()) {
            if (&other == spec) {
                continue;
            }
            if ((settings.*(other.muted))() != mutedNow) {
                (settings.*(other.toggleMuted))();
            }
        }
    }
    surface()->applyAudioSettings(settings);
    // Nothing to demonstrate about a channel that was just silenced.
    queueAudition(mutedNow ? QString() : QLatin1String(spec->audition));
    emit changed();
}

void QmlAudioSettingsModel::saveAsSoftwareDefault()
{
    if (surface() != nullptr) {
        surface()->saveAudioSettingsAsSoftwareDefault();
    }
}

void QmlAudioSettingsModel::restoreSoftwareDefault()
{
    if (surface() != nullptr) {
        surface()->restoreAudioSettingsFromSoftwareDefault();
        // The restored mix is not any one channel's edit, so there is no
        // channel to audition — drop whatever the last edit had queued.
        queueAudition(QString());
        emit changed();
    }
}

void QmlAudioSettingsModel::setAuditionHeld(bool held)
{
    if (auditionHeld_ == held) {
        return;
    }
    auditionHeld_ = held;
    if (!held && !pendingAudition_.isEmpty() && auditionTimer_ != nullptr
        && !auditionTimer_->isActive()) {
        // The settle timer already fired while the handle was down and deferred
        // to this moment.
        flushAudition();
    }
}

void QmlAudioSettingsModel::releaseAudition()
{
    if (auditionTimer_ != nullptr) {
        auditionTimer_->stop();
    }
    pendingAudition_.clear();
    auditionKindAwaitingAssets_.clear();
    auditionReloadAssetGeneration_ = 0;
    auditionReloadSequence_ = 0;
    auditionSfxDir_.clear();
    auditionHeld_ = false;
    delete auditionRuntime_;
    auditionRuntime_ = nullptr;
}

void QmlAudioSettingsModel::queueAudition(const QString& kind)
{
    pendingAudition_ = kind;
    if (kind.isEmpty()) {
        if (auditionTimer_ != nullptr) {
            auditionTimer_->stop();
        }
        return;
    }
    if (auditionTimer_ == nullptr) {
        auditionTimer_ = new QTimer(this);
        auditionTimer_->setSingleShot(true);
        auditionTimer_->setInterval(kAuditionSettleMs);
        connect(auditionTimer_, &QTimer::timeout, this, &QmlAudioSettingsModel::flushAudition);
    }
    auditionTimer_->start();
}

void QmlAudioSettingsModel::flushAudition()
{
    if (auditionHeld_) {
        // Still dragging. setAuditionHeld(false) picks this back up on release.
        return;
    }
    const QString kind = std::exchange(pendingAudition_, QString());
    if (!kind.isEmpty()) {
        playAudition(kind);
    }
}

bool QmlAudioSettingsModel::playAudition(const QString& kind)
{
    if (surface() == nullptr || kind.isEmpty()) {
        return false;
    }
    // Don't audition over a running preview: the user already hears the real
    // SFX from live playback, so a second runtime playing the same sample just
    // doubles it. Audition is only meaningful when the preview is idle.
    if (surface()->playing()) {
        return false;
    }
    const QString sfxDir = miacode::preview_sfx::resolveSfxDirectory();
    if (sfxDir.isEmpty()) {
        return false;
    }
    QString resolvedKind = previewSfxNormalizedKind(kind);
    if (resolvedKind == QLatin1String("break_slide")) {
        // The break-slide channel covers a head and a tail; the head is the one
        // that says what the level sounds like.
        resolvedKind = QStringLiteral("break_slide_start");
    }

    if (auditionRuntime_ == nullptr) {
        auditionRuntime_ = new QtPreviewSfxRuntime(this);
        connect(auditionRuntime_,
                &QtPreviewSfxRuntime::commandCompleted,
                this,
                [this](const QtPreviewSfxRuntime::Completion& completion) {
                    using namespace miacode::preview_audio;
                    if (completion.kind != CommandKind::ReloadAssets
                        || completion.identity.sequence != auditionReloadSequence_
                        || !acceptsAssetCompletion(auditionReloadAssetGeneration_, completion)) {
                        return;
                    }
                    auditionReloadAssetGeneration_ = 0;
                    auditionReloadSequence_ = 0;
                    const QString kindNow = std::exchange(auditionKindAwaitingAssets_, QString());
                    if (!completion.success || kindNow.isEmpty()
                        || surface() == nullptr || surface()->playing()) {
                        return;
                    }
                    auditionRuntime_->applyLevels(surface()->audioSettings());
                    auditionRuntime_->stopAll();
                    auditionRuntime_->audition(kindNow);
                });
    }

    const QString resolvedSfxDir = QDir::cleanPath(sfxDir);
    if (auditionSfxDir_ != resolvedSfxDir || !auditionRuntime_->audioEngineInitialized()) {
        // Samples aren't in yet. Post the load and let its completion play the
        // kind we were asked for; this call is the one that goes silent.
        const QtPreviewSfxRuntime::AssetSubmission reload =
            auditionRuntime_->reloadAssetsForChartWithWarmupPaths(
                QString(), QString(), resolvedSfxDir, surface()->audioSettings());
        auditionSfxDir_ = resolvedSfxDir;
        auditionKindAwaitingAssets_ = resolvedKind;
        auditionReloadAssetGeneration_ = reload.post.accepted ? reload.identity.assetGeneration : 0;
        auditionReloadSequence_ = reload.post.accepted ? reload.identity.sequence : 0;
        return false;
    }

    auditionRuntime_->applyLevels(surface()->audioSettings());
    auditionRuntime_->stopAll();
    return auditionRuntime_->audition(resolvedKind);
}

bool QmlAudioSettingsModel::breakSlideTailCheerMuted() const
{
    return surface() != nullptr && surface()->audioSettings().breakSlideTailCheerMuted;
}

void QmlAudioSettingsModel::setBreakSlideTailCheerMuted(bool muted)
{
    if (surface() == nullptr || muted == breakSlideTailCheerMuted()) {
        return;
    }
    PreviewAudioSettings settings = surface()->audioSettings();
    settings.breakSlideTailCheerMuted = muted;
    surface()->applyAudioSettings(settings);
    emit changed();
}

}  // namespace miacode::qml_ui
