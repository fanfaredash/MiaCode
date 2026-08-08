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
#ifdef Q_OS_WIN
#include <windows.h>
#elif defined(Q_OS_MACOS)
#include <dlfcn.h>
#endif

#include "bass.h"
#include "bassmix.h"
#endif

#include "BassPreviewAudioBackendImpl.h"
#include "BassPreviewAudioBackendSample.h"

using namespace miacode::audio::bass_detail;

// God-file split: the mutable file-static BASS device reference counter is
// shared across the BassPreviewAudioBackend translation units (declared
// `extern` in BassPreviewAudioBackendImpl.h). It is DEFINED here, exactly
// once, in the engine-init TU that owns BASS_Init / BASS_Free.
#ifdef MIACODE_HAS_BASS_AUDIO
namespace miacode {
namespace audio {
namespace bass_detail {
int gBassDeviceRefCount = 0;
}  // namespace bass_detail
}  // namespace audio
}  // namespace miacode
#endif

bool BassPreviewAudioBackend::ensureBassFxLoaded()
{
    MC_OP("BassPreviewAudioBackend::ensureBassFxLoaded");
    if (bassFxTempoCreate_ != nullptr) {
        return true;
    }
#ifdef Q_OS_WIN
    const QString libraryPath = runtimeFilePath(QStringLiteral("bass_fx.dll"));
    _mc_op_.note(QStringLiteral("path=%1").arg(libraryPath));
    const HMODULE module = LoadLibraryW(reinterpret_cast<LPCWSTR>(libraryPath.utf16()));
    if (module == nullptr) {
        const DWORD errorCode = ::GetLastError();
        lastNativeErrorCode_ = static_cast<int>(errorCode);
        _mc_op_.fail(
            QStringLiteral("LoadLibraryW failed err=%1").arg(static_cast<qulonglong>(errorCode)));
        appendAudioDebugLog(QString("bass_fx_load_failed path=%1").arg(libraryPath));
        return false;
    }
    FARPROC proc = GetProcAddress(module, "BASS_FX_TempoCreate");
    if (proc == nullptr) {
        const DWORD errorCode = ::GetLastError();
        lastNativeErrorCode_ = static_cast<int>(errorCode);
        FreeLibrary(module);
        _mc_op_.fail(QStringLiteral("GetProcAddress BASS_FX_TempoCreate missing err=%1")
                         .arg(static_cast<qulonglong>(errorCode)));
        appendAudioDebugLog(QString("bass_fx_symbol_missing path=%1").arg(libraryPath));
        return false;
    }
    bassFxModule_ = module;
    bassFxTempoCreate_ = reinterpret_cast<void*>(proc);
    return true;
#elif defined(Q_OS_MACOS) && defined(MIACODE_HAS_BASS_AUDIO)
    const QString libraryPath = runtimeFilePath(QStringLiteral("libbass_fx.dylib"));
    _mc_op_.note(QStringLiteral("path=%1").arg(libraryPath));
    const QByteArray encodedPath = QFile::encodeName(libraryPath);
    void* module = dlopen(encodedPath.constData(), RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr) {
        const char* error = dlerror();
        _mc_op_.fail(QStringLiteral("dlopen failed error=%1")
                         .arg(QString::fromLocal8Bit(error != nullptr ? error : "unknown")));
        appendAudioDebugLog(QString("bass_fx_load_failed path=%1").arg(libraryPath));
        return false;
    }
    dlerror();
    void* proc = dlsym(module, "BASS_FX_TempoCreate");
    const char* symbolError = dlerror();
    if (proc == nullptr || symbolError != nullptr) {
        dlclose(module);
        _mc_op_.fail(QStringLiteral("dlsym BASS_FX_TempoCreate missing error=%1")
                         .arg(QString::fromLocal8Bit(symbolError != nullptr ? symbolError : "unknown")));
        appendAudioDebugLog(QString("bass_fx_symbol_missing path=%1").arg(libraryPath));
        return false;
    }
    bassFxModule_ = module;
    bassFxTempoCreate_ = proc;
    return true;
#else
    _mc_op_.fail(QStringLiteral("BASS backend unavailable"));
    return false;
#endif
}

void BassPreviewAudioBackend::unloadBassFx()
{
#ifdef Q_OS_WIN
    if (bassFxModule_ != nullptr) {
        FreeLibrary(static_cast<HMODULE>(bassFxModule_));
    }
#elif defined(Q_OS_MACOS) && defined(MIACODE_HAS_BASS_AUDIO)
    if (bassFxModule_ != nullptr) {
        dlclose(bassFxModule_);
    }
#endif
    bassFxModule_ = nullptr;
    bassFxTempoCreate_ = nullptr;
}

void BassPreviewAudioBackend::loadOptionalPlugins()
{
#ifdef MIACODE_HAS_BASS_AUDIO
#ifdef Q_OS_WIN
    if (pluginAac_ == 0) {
        const QString aacPath = runtimeFilePath(QStringLiteral("bass_aac.dll"));
        if (QFileInfo::exists(aacPath)) {
            pluginAac_ = BASS_PluginLoad(reinterpret_cast<const WCHAR*>(aacPath.utf16()), 0);
        }
    }
    if (pluginOpus_ == 0) {
        const QString opusPath = runtimeFilePath(QStringLiteral("bassopus.dll"));
        if (QFileInfo::exists(opusPath)) {
            pluginOpus_ = BASS_PluginLoad(reinterpret_cast<const WCHAR*>(opusPath.utf16()), 0);
        }
    }
#elif defined(Q_OS_MACOS)
    if (pluginOpus_ == 0) {
        const QString opusPath = runtimeFilePath(QStringLiteral("libbassopus.dylib"));
        if (QFileInfo::exists(opusPath)) {
            const QByteArray encodedPath = QFile::encodeName(opusPath);
            pluginOpus_ = BASS_PluginLoad(encodedPath.constData(), 0);
            noteBassErr("plugin_load_opus");
        }
    }
#endif
#endif
}

void BassPreviewAudioBackend::unloadOptionalPlugins()
{
#ifdef MIACODE_HAS_BASS_AUDIO
    if (pluginAac_ != 0) {
        BASS_PluginFree(pluginAac_);
        noteBassErr("plugin_free_aac");
        pluginAac_ = 0;
    }
    if (pluginOpus_ != 0) {
        BASS_PluginFree(pluginOpus_);
        noteBassErr("plugin_free_opus");
        pluginOpus_ = 0;
    }
#endif
}

bool BassPreviewAudioBackend::initializeAudioEngine()
{
    MC_OP("BassPreviewAudioBackend::initializeAudioEngine");
#ifndef MIACODE_HAS_BASS_AUDIO
    _mc_op_.fail(QStringLiteral("BASS backend unavailable"));
    return false;
#else
    if (engineInitialized_ && masterMixer_ != 0) {
        return true;
    }
    QElapsedTimer timer;
    timer.start();
    _mc_op_.note(QStringLiteral("device_sr=%1").arg(deviceSampleRate_));
    if (!ensureBassFxLoaded()) {
        _mc_op_.fail(QStringLiteral("bass_fx load failed"));
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
            QString("reused=0 elapsed_ms=%1 ok=0 reason=bass_fx").arg(timer.elapsed()),
            true);
        return false;
    }
    if (!registeredBassDeviceRef_) {
        if (gBassDeviceRefCount == 0) {
            if (!BASS_Init(-1, static_cast<int>(deviceSampleRate_), 0, nullptr, nullptr)) {
                const int errorCode = static_cast<int>(BASS_ErrorGetCode());
                lastNativeErrorCode_ = errorCode;
                noteBassErrCode("engine_init/bass_init", errorCode);
                _mc_op_.fail(QStringLiteral("BASS_Init err=%1").arg(errorCode));
                appendAudioDebugLog(QString("bass_init_failed err=%1").arg(errorCode));
                appendBassDebugLog(
                    miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
                    QString("reused=0 elapsed_ms=%1 ok=0 reason=bass_init").arg(timer.elapsed()),
                    true);
                return false;
            }
        }
        ++gBassDeviceRefCount;
        registeredBassDeviceRef_ = true;
    }
    loadOptionalPlugins();
    masterMixer_ = BASS_Mixer_StreamCreate(
        deviceSampleRate_,
        miacode::preview_audio::kMixChannels,
        BASS_SAMPLE_FLOAT | BASS_MIXER_NONSTOP | BASS_MIXER_POSEX);
    if (masterMixer_ == 0) {
        const int errorCode = static_cast<int>(BASS_ErrorGetCode());
        lastNativeErrorCode_ = errorCode;
        noteBassErrCode("engine_init/master_mixer_create", errorCode);
        appendAudioDebugLog(QString("bass_master_mixer_failed err=%1").arg(errorCode));
        if (registeredBassDeviceRef_ && gBassDeviceRefCount > 0) {
            --gBassDeviceRefCount;
            registeredBassDeviceRef_ = false;
        }
        if (gBassDeviceRefCount == 0) {
            BASS_Free();
        }
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
            QString("reused=0 elapsed_ms=%1 ok=0 reason=master_mixer").arg(timer.elapsed()),
            true);
        return false;
    }
    BASS_ChannelSetAttribute(masterMixer_, BASS_ATTRIB_BUFFER, 0.0f);
    noteBassErr("engine_init/master_buffer_attr");
    BASS_ChannelSetAttribute(masterMixer_, BASS_ATTRIB_MIXER_THREADS, 8.0f);
    noteBassErr("engine_init/master_mixer_threads_attr");
    // G1 Commit 6: master mixer stays ACTIVE_PLAYING for the lifetime of the engine.
    // Pre-G1 we cycled it via BASS_ChannelPause / BASS_ChannelPlay / BASS_ChannelStop
    // at every session-state change; each cold-start churned BASS_FX SoundTouch
    // buffers and was the root cause of the multi-cycle audio-tearing bug per
    // PREVIEW_AUDIO_CLOCK_ALIGNMENT_HANDOFF_ZH.md §4.2. Sample audibility is now
    // gated entirely by the BASS_MIXER_CHAN_PAUSE flag on each per-sample source.
    if (!BASS_ChannelPlay(masterMixer_, FALSE)) {
        const int errorCode = static_cast<int>(BASS_ErrorGetCode());
        lastNativeErrorCode_ = errorCode;
        noteBassErrCode("engine_init/master_play_once", errorCode);
        BASS_StreamFree(masterMixer_);
        masterMixer_ = 0;
        if (registeredBassDeviceRef_ && gBassDeviceRefCount > 0) {
            --gBassDeviceRefCount;
            registeredBassDeviceRef_ = false;
        }
        if (gBassDeviceRefCount == 0) {
            BASS_Free();
        }
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
            QString("reused=0 elapsed_ms=%1 ok=0 reason=master_play").arg(timer.elapsed()),
            true);
        return false;
    }
    engineInitialized_ = true;
    // Started after engineInitialized_, so the sampler never queries a half-built engine.
    startAudioHealthSampler();
    appendAudioDebugLog(QString("bass_engine_ready sample_rate=%1").arg(deviceSampleRate_));
    appendBassDebugLog(
        miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
        QString("reused=0 elapsed_ms=%1 ok=1 sample_rate=%2")
            .arg(timer.elapsed())
            .arg(deviceSampleRate_),
        true);
    return true;
#endif
}


bool BassPreviewAudioBackend::audioEngineInitialized() const
{
    return engineInitialized_ && masterMixer_ != 0;
}
