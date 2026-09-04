#include "runtime/Session.h"

#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "audio/PreviewAudioDeviceWatcher.h"
#include "common/DebugLog.h"
#include "runtime/document/DocumentSessionHost.h"
#include "runtime/editor/EditorHost.h"
#include "runtime/export/VideoExportHost.h"
#include "runtime/media/MediaJobsHost.h"
#include "runtime/playback/PlaybackCoordinator.h"
#include "runtime/playback/PlaybackSurfaceAdapters.h"
#include "runtime/preview/PreviewHost.h"
#include "runtime/preview/StageMediaHost.h"
#include "runtime/settings/SettingsHost.h"
#include "runtime/shell/ShellHost.h"
#include "runtime/timeline/TimelineHost.h"
#include "runtime/validation/ValidationHost.h"
#include "QtPreviewSfxRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"
#include "preview/runtime/PreviewRuntime.h"
#include "tools/latency/LatencySandboxController.h"

Session::~Session()
{
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/qml_session"),
        QStringLiteral("step=destructor_body_entered"),
        /*force=*/true);

    applicationServices_.setExportEngine(nullptr);
    applicationServices_.setEditorPageRouter(nullptr);
    applicationServices_.setLatencyEngine(nullptr);
    if (previewHost_ != nullptr) {
        previewHost_->invalidateSession();
    }
    if (timelineHost_ != nullptr) {
        timelineHost_->invalidateSession();
    }
    if (playbackPreviewSurface_ != nullptr) {
        playbackPreviewSurface_->invalidateSession();
    }
    if (playbackTimelineSurface_ != nullptr) {
        playbackTimelineSurface_->invalidateSession();
    }
    if (playback_ != nullptr) {
        playback_->invalidateSession();
    }
    applicationServices_.setTimelineSurface(nullptr);
    applicationServices_.setPreviewSurface(nullptr);
    applicationServices_.setPlaybackControl(nullptr);
    applicationServices_.setPlaybackStateAuthority(nullptr);
    applicationServices_.setPreferencesStore(nullptr);
    applicationServices_.setDocumentBridge(nullptr);
    applicationServices_.setExportPageSession(nullptr);

    setPreviewFixedTimerHighResolutionActive(false);
    shutdownPreviewStageMediaHost();

    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("app_shutdown/qml_session"),
        QStringLiteral("step=destructor_body_exit"),
        /*force=*/true);
}

void Session::preparePreviewForShutdown()
{
    if (quickShellPreviewCompositeSurface_ != nullptr) {
        quickShellPreviewCompositeSurface_->setMediaHost(nullptr);
        quickShellPreviewCompositeSurface_->setActive(false);
    }

    if (previewSeekDebounceTimer_ != nullptr) {
        previewSeekDebounceTimer_->stop();
    }
    if (qtPreviewTimer_ != nullptr) {
        qtPreviewTimer_->stop();
    }
    if (qtPreviewTimelineTimer_ != nullptr) {
        qtPreviewTimelineTimer_->stop();
    }
    if (previewStatsUiTimer_ != nullptr) {
        previewStatsUiTimer_->stop();
    }
    if (previewHeldSeekTimer_ != nullptr) {
        previewHeldSeekTimer_->stop();
    }
    setPreviewFixedTimerHighResolutionActive(false);

    if (scene_ != nullptr) {
        scene_->setActivePlaybackProfilingEnabled(false);
    }
    if (previewAudioDeviceWatcher_ != nullptr) {
        previewAudioDeviceWatcher_->disconnect(this);
        delete previewAudioDeviceWatcher_;
        previewAudioDeviceWatcher_ = nullptr;
    }
    if (previewSfxRuntime_ != nullptr) {
        previewSfxRuntime_->prepareForShutdown();
    }
    shutdownPreviewStageMediaHost();
}

QString Session::windowTitle() const
{
    return titleText_;
}

void Session::noteStatus(const QString&)
{
}
