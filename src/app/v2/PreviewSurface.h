#pragma once

#include "audio/PreviewAudioSettings.h"
#include "common/MuriRenderOptions.h"
#include "core/video/PreviewRenderSettings.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QVariantMap>

namespace miacode::v2 {

// The live preview: its transport, its runtime objects, and the settings that
// are not plain appearance values.
//
// Stage 3.5 item 2. The appearance VALUES already left for
// PreviewAppearanceState; what stays here is everything that needs the running
// preview — the playhead, the scrub gesture, the rate, the QSG runtime and
// stage-media host QML binds to, the skin/outline catalog on disk, and the
// audio mixer.
//
// The window's `shell*` prefixes are v1 QuickShell controller history and are
// not carried forward.
//
// Deliberately Qt Widgets-free. The two runtime objects cross as plain
// QObject* so this header does not need Qt Quick either.
class PreviewSurface
{
public:
    virtual ~PreviewSurface() = default;

    // ---- transport ----
    virtual bool playing() const = 0;
    virtual double positionSeconds() const = 0;
    virtual double durationSeconds() const = 0;
    // Negative when 添加片头 is on: the intro occupies time before chart zero.
    virtual double lowerBoundSeconds() const = 0;
    virtual void togglePlayback() = 0;
    virtual void stop() = 0;
    virtual void seek(double second) = 0;

    // A scrub is a gesture, not a seek: it suspends the follow behaviour for
    // its duration, so it has to be bracketed rather than inferred from a
    // stream of positions.
    virtual void beginScrub() = 0;
    virtual void updateScrub(double second, bool centerView) = 0;
    virtual void endScrub(double second, bool centerView) = 0;

    // No rate getter: the shell shows the label, never the number.
    virtual void setPlaybackRate(double rate) = 0;
    // Steps through the preset rate ladder; direction is -1 or +1.
    virtual void nudgePlaybackRate(int direction) = 0;
    virtual QString playbackRateLabel() const = 0;

    // ---- what QML binds to ----
    virtual QObject* previewRuntimeObject() const = 0;
    virtual QObject* stageMediaHostObject() const = 0;
    virtual double canvasAspectRatio() const = 0;
    virtual QStringList statsTexts() const = 0;

    // ---- 无理 overlay render mode ----
    virtual RenderMode muriRenderMode() const = 0;
    virtual void setMuriRenderMode(RenderMode mode) = 0;
    virtual void toggleMuriRenderMode() = 0;

    // ---- skin / judge-line catalog on disk ----
    virtual QStringList availableSkinDirectoryNames() const = 0;
    virtual QString skinDisplayName(const QString& directoryName) const = 0;
    virtual QString resolveSkinDir() const = 0;
    virtual QString resolveSkinRootDir() const = 0;
    virtual QString resolveCustomOutlineDir() const = 0;
    // Applying an outline variant is not just storing it: it may pick one
    // automatically for the chart, and it persists.
    virtual void applyOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection,
                                     bool persistState) = 0;

    // ---- render settings, as a value map the settings page renders ----
    virtual QVariantMap renderSettings() const = 0;
    virtual void setRenderSetting(const QString& key, const QVariant& value) = 0;
    // Pushes whatever changed onto the live surfaces.
    virtual void refreshSurfaces() = 0;
    // Pushes the current mixer levels into the live SFX runtime. A no-op until
    // the audio engine is up, which is the guard callers used to repeat.
    virtual void applySfxLevels() = 0;

    // Quiesce the preview before the root window goes away — stop playback and
    // release what the render thread holds. Called once, from the accepted
    // close path.
    virtual void prepareForShutdown() = 0;

    // ---- audio mixer ----
    virtual PreviewAudioSettings audioSettings() const = 0;
    virtual void applyAudioSettings(const PreviewAudioSettings& settings) = 0;
    virtual void saveAudioSettingsAsSoftwareDefault() = 0;
    virtual void restoreAudioSettingsFromSoftwareDefault() = 0;

protected:
    PreviewSurface() = default;
    PreviewSurface(const PreviewSurface&) = default;
    PreviewSurface& operator=(const PreviewSurface&) = default;
};

}  // namespace miacode::v2
