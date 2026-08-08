#include "BassPreviewAudioBackend.h"

#include "BassPreviewDebugLogRouting.h"
#include "BassPreviewRetainedState.h"
#include "common/ChartAssetPaths.h"
#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/FileContentStamp.h"
#include "common/OperationLog.h"
#include "common/PreviewAudioMixConfig.h"
#include "common/PreviewSfxAssets.h"
#include "common/PreviewSfxTimeline.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QtMath>

#include <cstdio>   // G1 Commit 8 followup: std::snprintf for startup-beacon lines

#ifdef MIACODE_HAS_BASS_AUDIO
#include "bass.h"
#include "bassmix.h"
#endif

#include "BassPreviewAudioBackendImpl.h"
#include "BassPreviewAudioBackendSample.h"

using namespace miacode::audio::bass_detail;

BassPreviewAudioBackend::BassPreviewAudioBackend(QObject* parent)
    : QObject(parent)
{
    appendAudioDebugLog("BassPreviewAudioBackend created");
}

BassPreviewAudioBackend::~BassPreviewAudioBackend()
{
    appendAudioDebugLog("BassPreviewAudioBackend destroying");
    shuttingDown_.store(true, std::memory_order_release);
#ifdef MIACODE_HAS_BASS_AUDIO
    stopPlaybackSession();
    resetAssets();
    unloadOptionalPlugins();
    if (masterMixer_ != 0) {
        BASS_StreamFree(masterMixer_);
        noteBassErr("dtor/master_stream_free");
        masterMixer_ = 0;
    }
    unloadBassFx();
    if (registeredBassDeviceRef_ && gBassDeviceRefCount > 0) {
        --gBassDeviceRefCount;
        registeredBassDeviceRef_ = false;
    }
    if (engineInitialized_ && gBassDeviceRefCount == 0) {
        BASS_Stop();
        noteBassErr("dtor/bass_stop");
        BASS_Free();
        noteBassErr("dtor/bass_free");
        engineInitialized_ = false;
    } else if (engineInitialized_) {
        engineInitialized_ = false;
    }
#endif
}

QString BassPreviewAudioBackend::backendId() const
{
    return QStringLiteral("bass");
}

QString BassPreviewAudioBackend::resolveTrackPath(const QString& chartPath) const
{
    // Resolved-path warmup cache removed (2026-06-03). It keyed on the chart-path
    // string only and never re-validated the track file, so a same-named track
    // with new content — or a track that appeared/changed after warmup — was
    // shadowed by a stale (or empty) cached path, silently dropping the BGM.
    // The live resolver is cheap and exists-checked; the warmup worker still
    // byte-prefetches the file into the OS cache, which was warmup's real value.
    return miacode::chart_assets::resolveTrackPath(chartPath);
}

QString BassPreviewAudioBackend::resolveSfxDir() const
{
    return miacode::preview_sfx::resolveSfxDirectory();
}

bool BassPreviewAudioBackend::runtimeLibrariesPresent() const
{
#if defined(Q_OS_WIN)
    return runtimeLibraryExists(QStringLiteral("bass.dll"))
        && runtimeLibraryExists(QStringLiteral("bassmix.dll"))
        && runtimeLibraryExists(QStringLiteral("bass_fx.dll"));
#elif defined(Q_OS_MACOS) && defined(MIACODE_HAS_BASS_AUDIO)
    return runtimeLibraryExists(QStringLiteral("libbass.dylib"))
        && runtimeLibraryExists(QStringLiteral("libbassmix.dylib"))
        && runtimeLibraryExists(QStringLiteral("libbass_fx.dylib"))
        && runtimeLibraryExists(QStringLiteral("libbassopus.dylib"));
#elif defined(Q_OS_LINUX) && defined(MIACODE_HAS_BASS_AUDIO)
    return runtimeLibraryExists(QStringLiteral("libbass.so"))
        && runtimeLibraryExists(QStringLiteral("libbassmix.so"))
        && runtimeLibraryExists(QStringLiteral("libbass_fx.so"));
#else
    return false;
#endif
}

bool BassPreviewAudioBackend::canBePrimary(QString* reason) const
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (!runtimeLibrariesPresent()) {
        if (reason != nullptr) {
            *reason = QStringLiteral("missing bundled BASS runtime libraries");
        }
        return false;
    }
    if (reason != nullptr) {
        *reason = QStringLiteral("bundled BASS runtime libraries are available");
    }
    return true;
#else
    if (reason != nullptr) {
        *reason = QStringLiteral("BASS preview backend is unavailable in this build");
    }
    return false;
#endif
}

void BassPreviewAudioBackend::setWarmupResolvedPaths(const QString& chartPath, const QString& trackPath, const QString& sfxDir)
{
    MC_OP("BassPreviewAudioBackend::setWarmupResolvedPaths");
    // No-op: the resolved-path cache was removed (see resolveTrackPath). Path
    // resolution is now always live so a same-named track with new content is
    // picked up. Kept as a no-op to preserve the backend interface; the warmup
    // worker still byte-prefetches the files into the OS cache.
    Q_UNUSED(chartPath);
    Q_UNUSED(trackPath);
    Q_UNUSED(sfxDir);
}
