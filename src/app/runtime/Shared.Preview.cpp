#include "runtime/Shared.h"

#include "runtime/Session.h"

#include "QtPreviewSfxRuntime.h"
#include "UiText.h"
#include "app/quick_shell/QuickShellPreviewCompositeSurface.h"
#include "common/AssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/OperationLog.h"
#include "common/PreviewSfxAssets.h"
#include "preview/runtime/PreviewRuntime.h"
#include "preview/runtime/PreviewStageMediaHost.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QStringList>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

#include <cstdio>

namespace miacode::runtime::shared {

namespace {

// Retired name for the standard skin directory; normalizePreviewSkinDirectoryName
// below maps it forward so old project files/preferences keep resolving.
QString legacyStandardPreviewSkinDirectoryName()
{
    return QStringLiteral("skinSTD");
}

// Windows-only status logging for setPreviewFixedTimerHighResolutionActive below;
// not used outside that function.
void appendPreviewFramePacingStatusLog(const QString& action, const QString& payload = QString())
{
    QString text = QStringLiteral("action=%1").arg(action);
    if (!payload.trimmed().isEmpty()) {
        text += QStringLiteral(" ") + payload.trimmed();
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("preview/frame_pacing"),
        text,
        true
    );
}

}  // namespace

// Stage 4.9d-4a extractions — moved in verbatim from their previous homes (see the
// forwarding shells at the call sites) so PlaybackCoordinator can call them without
// routing through Session/StageMediaHost.

void ensurePreviewSfxRuntimePrepared(RuntimeContext::State& state)
{
    // A reload already in flight counts as prepared-in-progress. A chart change
    // invalidates readiness, then its background data warm-up posts the atomic
    // path+asset load on the worker (see StageMediaHost::applyPreviewSfxWarmupResult).
    // The completion handler in sections/frame/MainWindow.FrameBootstrap.cpp flips
    // previewSfxRuntimePrepared_. Without the sequence check below, pressing play (or
    // scrubbing) inside that window re-posted a SECOND full engine + sample + BGM
    // load, and its newer assetGeneration invalidated the first reload's completion —
    // so the redundant work also delayed the ready edge it was racing. The completion
    // handler clears the sequence on failure too, so a failed reload still gets
    // retried by the next call here.
    if (state.previewSfxRuntime_ == nullptr
        || state.previewSfxRuntimePrepared_
        || state.previewSfxRuntimePreparationSequence_ != 0) {
        return;
    }
    QElapsedTimer initTimer;
    initTimer.start();
    const bool hasCurrentWarmupPaths =
        state.previewSfxWarmupAppliedGeneration_ == state.previewWarmupGeneration_
        && state.previewSfxWarmupChartPath_ == state.currentFilePath_;
    const QString trackPath = hasCurrentWarmupPaths
        ? state.previewSfxWarmupTrackPath_
        : state.lastTrackPath_;
    const QString sfxDir = hasCurrentWarmupPaths
        ? state.previewSfxWarmupSfxDir_
        : miacode::preview_sfx::resolveSfxDirectory();
    const QtPreviewSfxRuntime::AssetSubmission reload =
        state.previewSfxRuntime_->reloadAssetsForChartWithWarmupPaths(
            state.currentFilePath_, trackPath, sfxDir, state.previewAudioSettings_);
    state.previewSfxRuntimePrepared_ = false;
    state.previewSfxRuntimePreparationAssetGeneration_ = reload.post.accepted
        ? reload.identity.assetGeneration
        : 0;
    state.previewSfxRuntimePreparationSequence_ = reload.post.accepted
        ? reload.identity.sequence
        : 0;
    state.previewSfxRuntime_->setBackgroundTrackPlaybackRate(state.previewPlaybackRate_);
    const qint64 elapsedMs = initTimer.elapsed();
    appendStartupTimingStage("mainwindow/preview_sfx_runtime_prepare_on_demand", elapsedMs, elapsedMs);
}

void applyPreviewStageMediaRouteVisualSettings(RuntimeContext::State& state)
{
    // While the export-preview dialog is up, PV/BG stays visible regardless of the
    // pause-hide option so the user previews exactly what the exported video shows.
    // Holding Alt while paused inverts the pause-hide option (same effective flag as
    // effectivePreviewOutlineVariant) so judge area <-> PV/BG flip together.
    const bool forceJudgeAreaWhenPaused =
        state.previewForceLabeledJudgeLineWhenPaused_ != state.pauseDisplayAltHoldActive_;
    const bool mediaVisible = !forceJudgeAreaWhenPaused
        || state.playing_
        || state.exportPreviewActive_;
    if (state.previewStageMediaHost_ != nullptr) {
        state.previewStageMediaHost_->setBackgroundScaleMode(state.previewBackgroundScaleMode_);
        state.previewStageMediaHost_->setLayoutSquareScale(state.previewLayoutSquareScale_);
        state.previewStageMediaHost_->setMediaVisible(mediaVisible);
    }
    if (state.scene_ != nullptr) {
        const bool stageMediaVisible =
            mediaVisible
            && state.previewStageMediaHost_ != nullptr
            && state.previewStageMediaHost_->hasResolvedMedia();
        state.scene_->setStageMediaAvailable(stageMediaVisible);
    }
}

void applyPreviewStageMediaRoutePlaybackRate(RuntimeContext::State& state, double rate, const char* site)
{
    char buf[260];
    std::snprintf(buf, sizeof(buf),
        "preview/rate/route_apply tid=0x%llx site=%s rate=%.3f host=%d has_video=%d",
        static_cast<unsigned long long>(reinterpret_cast<quintptr>(QThread::currentThreadId())),
        site != nullptr ? site : "(unspecified)",
        rate,
        state.previewStageMediaHost_ != nullptr ? 1 : 0,
        state.previewStageMediaHost_ != nullptr && state.previewStageMediaHost_->hasVideoMedia() ? 1 : 0);
    miacode::oplog::appendStartupBeaconLine(buf);
    if (state.previewStageMediaHost_ != nullptr) {
        state.previewStageMediaHost_->setPlaybackRate(rate);
    }
}

void refreshQuickShellPreviewCompositeSurfaceState(RuntimeContext::State& state, Session& session)
{
    // Inlines StageMediaHost::ensureQuickShellPreviewCompositeSurfaceInitialized()
    // (construct-on-demand, then unconditionally re-bind runtime/media-host) — that
    // helper is a StageMediaHost member and not reachable from here. The redundant
    // setRuntime/setMediaHost pair right after is not a mistake: it matches the
    // original StageMediaHost::refreshQuickShellPreviewCompositeSurfaceState body,
    // which called both.
    if (state.quickShellPreviewCompositeSurface_ == nullptr) {
        state.quickShellPreviewCompositeSurface_ = new QuickShellPreviewCompositeSurface(&session);
    }
    state.quickShellPreviewCompositeSurface_->setRuntime(state.scene_);
    state.quickShellPreviewCompositeSurface_->setMediaHost(state.previewStageMediaHost_);

    // StageMediaHost::quickShellPreviewUsesSeparateSurface() is a hardcoded
    // `return false;` (not state-dependent, not virtual) — folded in here.
    const bool nextActive = false;
    state.quickShellPreviewCompositeSurface_->setRuntime(state.scene_);
    state.quickShellPreviewCompositeSurface_->setMediaHost(state.previewStageMediaHost_);
    state.quickShellPreviewCompositeSurface_->setActive(nextActive);

    if (state.quickShellPreviewCompositeSurfaceActive_ == nextActive) {
        return;
    }

    state.quickShellPreviewCompositeSurfaceActive_ = nextActive;
    if (state.runtimeDebugOutputEnabled_) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/stage_media"),
            QString("action=presentation_mode mode=%1")
                .arg(nextActive ? QStringLiteral("separate_surface") : QStringLiteral("inline")));
    }
}

void refreshPreviewStageMediaRouteDebugState(RuntimeContext::State& state, bool requestUpdate)
{
    if (state.scene_ == nullptr) {
        return;
    }
    miacode::preview::scene::PreviewExternalStageMediaType mediaType =
        miacode::preview::scene::PreviewExternalStageMediaType::None;
    bool videoPlaybackActive = false;
    double playbackSecond = 0.0;
    double clockDeltaSeconds = 0.0;
    qint64 videoFrameAgeMs = -1;
    qint64 videoFrameCountTotal = 0;
    double videoFrameRate = 0.0;
    double videoFrameIntervalAvgMs = 0.0;
    double videoFrameIntervalMaxMs = 0.0;
    qint64 videoFrameStallCount = 0;
    bool videoFrameStalled = false;
    bool hasResolvedMedia = false;
    bool hasVideoMedia = false;
    QString mediaTypeName = QStringLiteral("none");
    // StageMediaHost::previewUsesStageMediaHostRoute() is a hardcoded `return true;`
    // (not state-dependent, not virtual) — folded into the guard below.
    if (state.previewStageMediaHost_ != nullptr) {
        hasResolvedMedia = state.previewStageMediaHost_->hasResolvedMedia();
        hasVideoMedia = state.previewStageMediaHost_->hasVideoMedia();
        if (state.previewStageMediaHost_->hasVideoMedia()) {
            mediaType = miacode::preview::scene::PreviewExternalStageMediaType::Video;
            mediaTypeName = QStringLiteral("video");
        } else if (state.previewStageMediaHost_->hasResolvedMedia()) {
            mediaType = miacode::preview::scene::PreviewExternalStageMediaType::Image;
            mediaTypeName = QStringLiteral("image");
        }
        videoPlaybackActive = state.previewStageMediaHost_->videoPlaybackActive();
        playbackSecond = state.previewStageMediaHost_->currentPlaybackSecond();
        clockDeltaSeconds = state.previewStageMediaHost_->clockDeltaSeconds();
        videoFrameAgeMs = state.previewStageMediaHost_->videoFrameAgeMs();
        videoFrameCountTotal = state.previewStageMediaHost_->videoFrameCountTotal();
        videoFrameRate = state.previewStageMediaHost_->videoFrameRateEstimate();
        videoFrameIntervalAvgMs = state.previewStageMediaHost_->videoFrameIntervalAvgMs();
        videoFrameIntervalMaxMs = state.previewStageMediaHost_->videoFrameIntervalMaxMs();
        videoFrameStallCount = state.previewStageMediaHost_->videoFrameStallCount();
        videoFrameStalled = state.previewStageMediaHost_->videoFrameStalled();
    }
    state.scene_->setExternalStageMediaProfileSummary(
        // StageMediaHost::quickShellPreviewUsesSeparateSurface() is a hardcoded
        // `return false;` — folded in here.
        false,
        hasResolvedMedia,
        hasVideoMedia,
        mediaTypeName,
        videoFrameCountTotal,
        videoFrameRate,
        videoFrameIntervalAvgMs,
        videoFrameIntervalMaxMs,
        videoFrameStallCount
    );
    state.scene_->setExternalStageMediaDebugState(
        mediaType,
        videoPlaybackActive,
        playbackSecond,
        clockDeltaSeconds,
        videoFrameAgeMs,
        videoFrameStalled,
        requestUpdate
    );
}

void setPreviewFixedTimerHighResolutionActive(RuntimeContext::State& state, bool active)
{
#ifdef Q_OS_WIN
    const bool envRequested = miacode::debug_options::previewFixedTimerHighResolutionEnabled();
    const bool desired =
        active && envRequested;
    if (active && state.scene_ != nullptr) {
        state.scene_->noteFixedTimerHighResolutionRequest(envRequested);
    }
    if (state.qtPreviewFixedTimerHighResResolutionActive_ == desired) {
        if (active) {
            appendPreviewFramePacingStatusLog(
                QStringLiteral("fixed_timer_high_res_requested"),
                QStringLiteral("env=%1 active=%2 already_active=%3")
                    .arg(envRequested ? 1 : 0)
                    .arg(active ? 1 : 0)
                    .arg(state.qtPreviewFixedTimerHighResResolutionActive_ ? 1 : 0)
            );
        }
        return;
    }
    if (desired) {
        appendPreviewFramePacingStatusLog(
            QStringLiteral("fixed_timer_high_res_requested"),
            QStringLiteral("env=1 active=1 already_active=0")
        );
        const MMRESULT result = timeBeginPeriod(1);
        if (result == TIMERR_NOERROR) {
            state.qtPreviewFixedTimerHighResResolutionActive_ = true;
            if (state.scene_ != nullptr) {
                state.scene_->noteFixedTimerHighResolutionBeginResult(true);
            }
            appendPreviewFramePacingStatusLog(
                QStringLiteral("fixed_timer_high_res_enabled"),
                QStringLiteral("result=0")
            );
            return;
        }
        if (state.scene_ != nullptr) {
            state.scene_->noteFixedTimerHighResolutionBeginResult(false);
        }
        appendPreviewFramePacingStatusLog(
            QStringLiteral("fixed_timer_high_res_failed"),
            QStringLiteral("result=%1").arg(static_cast<unsigned int>(result))
        );
        return;
    }
    const bool activeAtStop = state.qtPreviewFixedTimerHighResResolutionActive_;
    if (!activeAtStop) {
        return;
    }
    if (state.scene_ != nullptr) {
        state.scene_->noteFixedTimerHighResolutionStopState(true);
    }
    timeEndPeriod(1);
    state.qtPreviewFixedTimerHighResResolutionActive_ = false;
    appendPreviewFramePacingStatusLog(
        QStringLiteral("fixed_timer_high_res_disabled")
    );
#else
    Q_UNUSED(state);
    Q_UNUSED(active);
#endif
}

QString standardPreviewSkinDirectoryName()
{
    return QStringLiteral("skinSD");
}

QString dxPreviewSkinDirectoryName()
{
    return QStringLiteral("skinDX");
}

QString normalizePreviewSkinDirectoryName(QString name)
{
    name = name.trimmed();
    return name.compare(legacyStandardPreviewSkinDirectoryName(), Qt::CaseInsensitive) == 0
        ? standardPreviewSkinDirectoryName()
        : name;
}

bool hasCorePreviewSkinAssets(const QString& directory)
{
    if (directory.isEmpty()) {
        return false;
    }
    const QDir dir(directory);
    return QFileInfo::exists(dir.filePath(QStringLiteral("tap.png")))
        && QFileInfo::exists(dir.filePath(QStringLiteral("hold.png")))
        && QFileInfo::exists(dir.filePath(QStringLiteral("star.png")));
}

QString resolvePreviewSkinRootDir()
{
    return miacode::assets::assetPath(QStringLiteral("skin"));
}

QString resolvePreviewCustomOutlineDir()
{
    return miacode::assets::customOutlineRootPath();
}

QStringList availablePreviewSkinDirectoryNames()
{
    const QString root = resolvePreviewSkinRootDir();
    if (root.isEmpty()) {
        return {};
    }

    QStringList names;
    const QStringList builtInNames{
        standardPreviewSkinDirectoryName(),
        dxPreviewSkinDirectoryName(),
    };
    const QDir rootDir(root);
    for (const QString& name : builtInNames) {
        if (hasCorePreviewSkinAssets(rootDir.filePath(name))) {
            names.append(name);
        }
    }

    const QFileInfoList entries = rootDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
    for (const QFileInfo& entry : entries) {
        const QString name = entry.fileName();
        bool isBuiltIn = false;
        for (const QString& builtInName : builtInNames) {
            if (name.compare(builtInName, Qt::CaseInsensitive) == 0) {
                isBuiltIn = true;
                break;
            }
        }
        if (!isBuiltIn && hasCorePreviewSkinAssets(entry.absoluteFilePath())) {
            names.append(name);
        }
    }
    return names;
}

QString previewSkinDisplayName(const QString& directoryName)
{
    const QString normalized = normalizePreviewSkinDirectoryName(directoryName);
    if (normalized.compare(standardPreviewSkinDirectoryName(), Qt::CaseInsensitive) == 0) {
        return UiText::text(QStringLiteral("dialog.render_settings.video.skin.standard"));
    }
    if (normalized.compare(dxPreviewSkinDirectoryName(), Qt::CaseInsensitive) == 0) {
        return UiText::text(QStringLiteral("dialog.render_settings.video.skin.dx"));
    }
    return normalized;
}

}  // namespace miacode::runtime::shared
