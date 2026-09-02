#pragma once

#include "core/video/PreviewRenderSettings.h"

#include <QString>

namespace miacode::v2 {

// The playback coordinator's fourth narrow port: a seam onto the preview
// stage-media route (warmup, chart-path resync, initialization-on-demand),
// the audio-runtime/outline-canvas re-applies that follow a chart or
// preference change, and preview shutdown.
//
// Cut by capability, like PlaybackPreferencesPort and unlike
// PlaybackValidationPort/PlaybackDocumentPort (which are cut by host because
// their methods already have exactly one owner today): eight of these nine
// methods belong to StageMediaHost, but preparePreviewForShutdown is Session's
// own orchestration — beyond handing the stage media off, it also stops the
// preview timers and clears the scene/composite surface, none of which
// StageMediaHost touches. Session implements the port, same as the
// preferences port, because reaching across that orchestration is exactly the
// job Session keeps once each domain has its own host.
//
// Before this port existed, six of ensurePreviewStageMediaRouteInitialized's
// call sites and syncPreviewStageMediaRouteChartPath's one call site were
// flagged as depending on a runtime branch to classify (StageMediaHost::
// ensurePreviewStageMediaHostInitialized() really does construct a
// PreviewStageMediaHost, wire several QObject::connect calls and read the
// document's videoPath — not a thin forward that could be moved verbatim into
// the coordinator). That question only mattered for moving the function body.
// Routing through a port leaves the body exactly where it is; the coordinator
// only says "make sure the stage media route is initialized" and does not
// need to know how, so the ambiguity is moot rather than resolved.
//
// No default arguments here (stage 3.5 precedent, repeated at
// PlaybackValidationPort): a default on syncPreviewStageMediaRouteChartPath's
// interface declaration would make every three-argument call ambiguous
// against the four-argument virtual. Session/StageMediaHost keep their own
// default argument on the override — a virtual function's default argument is
// resolved statically at the call site, not dispatched, so that default only
// affects callers that already hold a Session& or StageMediaHost&.
//
// Deliberately free of Session, QWidget, and QML/QSG types: PreviewPortSpec
// proves that at link time, by implementing this port with a fake that pulls
// in neither.
class PlaybackPreviewPort
{
public:
    virtual ~PlaybackPreviewPort() = default;

    virtual void ensurePreviewStageMediaRouteInitialized() = 0;
    virtual void syncPreviewStageMediaRouteChartPath(
        const QString& chartPath,
        const QString& trackPath,
        double pausedSecond,
        const QString& chartVideoOverridePath) = 0;
    virtual void schedulePreviewSubsystemWarmup() = 0;
    virtual void applyPreviewAudioSettingsToRuntime() = 0;
    virtual void loadProjectAudioPreferences() = 0;
    virtual void applyEffectivePreviewOutlineVariantToCanvas() = 0;
    virtual void preparePreviewForShutdown() = 0;
    virtual QString resolvePreviewSkinDir() const = 0;
    virtual void applyPreviewOutlineVariant(PreviewOutlineVariant variant, bool useAutoSelection,
                                             bool persistState) = 0;
};

}  // namespace miacode::v2
