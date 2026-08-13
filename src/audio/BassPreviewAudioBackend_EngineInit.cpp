#include "BassPreviewAudioBackend.h"

#include "BassPreviewDebugLogRouting.h"
#include "BassPreviewRetainedState.h"
#include "PreviewBassEmergencyPause.h"
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
#include <mutex>

#ifdef MIACODE_HAS_BASS_AUDIO
#ifdef Q_OS_WIN
#include <windows.h>
#include <mmdeviceapi.h>
#include <objbase.h>
#elif defined(Q_OS_MACOS)
#include <dlfcn.h>
#endif

#include "bass.h"
#include "bassmix.h"
#endif

#include "BassPreviewAudioBackendImpl.h"
#include "BassPreviewAudioBackendSample.h"

using namespace miacode::audio::bass_detail;

#if defined(MIACODE_HAS_BASS_AUDIO) && defined(Q_OS_WIN)
namespace {

struct DefaultBassEndpoint {
    int bassDeviceIndex = -1;
    QString endpointId;
    HRESULT comResult = E_FAIL;
    HRESULT endpointResult = E_FAIL;
};

struct ConcreteEndpointConfig {
    std::once_flag once;
    bool disabledDefaultDevice = false;
    int errorCode = BASS_OK;
};

ConcreteEndpointConfig& concreteEndpointConfig()
{
    static ConcreteEndpointConfig config;
    return config;
}

bool disableBassDefaultDeviceEntry(int* errorCode)
{
    // BASS_CONFIG_DEV_DEFAULT can only be changed before BASS has enumerated or
    // initialized a device. BASS_Free does not reopen that configuration window,
    // so a physical-output rebuild must reuse the result of the first attempt
    // rather than call BASS_SetConfig again.
    ConcreteEndpointConfig& config = concreteEndpointConfig();
    std::call_once(config.once, [&config] {
        config.disabledDefaultDevice = BASS_SetConfig(BASS_CONFIG_DEV_DEFAULT, FALSE) != FALSE;
        if (!config.disabledDefaultDevice) {
            config.errorCode = static_cast<int>(BASS_ErrorGetCode());
        }
    });
    if (errorCode != nullptr) {
        *errorCode = config.errorCode;
    }
    return config.disabledDefaultDevice;
}

DefaultBassEndpoint resolveDefaultBassEndpoint()
{
    DefaultBassEndpoint result;
    result.comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool ownsCom = SUCCEEDED(result.comResult);
    if (FAILED(result.comResult) && result.comResult != RPC_E_CHANGED_MODE) {
        return result;
    }

    IMMDeviceEnumerator* enumerator = nullptr;
    result.endpointResult = CoCreateInstance(
        __uuidof(MMDeviceEnumerator),
        nullptr,
        CLSCTX_ALL,
        __uuidof(IMMDeviceEnumerator),
        reinterpret_cast<void**>(&enumerator));
    if (SUCCEEDED(result.endpointResult) && enumerator != nullptr) {
        IMMDevice* endpoint = nullptr;
        result.endpointResult = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint);
        if (SUCCEEDED(result.endpointResult) && endpoint != nullptr) {
            LPWSTR endpointId = nullptr;
            result.endpointResult = endpoint->GetId(&endpointId);
            if (SUCCEEDED(result.endpointResult) && endpointId != nullptr) {
                result.endpointId = QString::fromWCharArray(endpointId);
                CoTaskMemFree(endpointId);
                BASS_DEVICEINFO deviceInfo{};
                for (DWORD index = 1; BASS_GetDeviceInfo(index, &deviceInfo); ++index) {
                    const QString driverId = deviceInfo.driver != nullptr
                        ? QString::fromLatin1(deviceInfo.driver)
                        : QString();
                    if ((deviceInfo.flags & BASS_DEVICE_ENABLED) != 0
                        && driverId == result.endpointId) {
                        result.bassDeviceIndex = static_cast<int>(index);
                        break;
                    }
                }
            }
            endpoint->Release();
        }
        enumerator->Release();
    }
    if (ownsCom) {
        CoUninitialize();
    }
    return result;
}

}  // namespace
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
#ifdef Q_OS_WIN
    // BASS_Init(-1) means "follow the Windows default device". Disable that mode
    // once, before the first BASS enumeration/initialization, and bind every engine
    // lifetime to the concrete Core Audio endpoint instead. A device rebuild happens
    // after BASS_Free, when this BASS setting is deliberately no longer mutable.
    int errorCode = BASS_OK;
    if (!disableBassDefaultDeviceEntry(&errorCode)) {
        lastNativeErrorCode_ = errorCode;
        appendAudioDebugLog(QString("bass_endpoint_bind_failed reason=disable_default err=%1")
                                .arg(errorCode));
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
            QString("reused=0 elapsed_ms=%1 ok=0 reason=disable_default err=%2")
                .arg(timer.elapsed())
                .arg(errorCode),
            true);
        return false;
    }
    const DefaultBassEndpoint endpoint = resolveDefaultBassEndpoint();
    if (endpoint.bassDeviceIndex < 0) {
        appendAudioDebugLog(
            QString("bass_endpoint_bind_failed reason=resolve_default com_hr=0x%1 endpoint_hr=0x%2 endpoint=%3")
                .arg(static_cast<quint32>(endpoint.comResult), 8, 16, QLatin1Char('0'))
                .arg(static_cast<quint32>(endpoint.endpointResult), 8, 16, QLatin1Char('0'))
                .arg(endpoint.endpointId.isEmpty() ? QStringLiteral("(none)") : endpoint.endpointId));
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
            QString("reused=0 elapsed_ms=%1 ok=0 reason=resolve_default")
                .arg(timer.elapsed()),
            true);
        return false;
    }
    const auto currentDevice = static_cast<miacode::preview_audio::BassDeviceLeaseApi::DeviceId>(
        BASS_GetDevice());
    if (currentDevice != miacode::preview_audio::BassDeviceLeaseApi::kNoDevice
        && currentDevice != static_cast<miacode::preview_audio::BassDeviceLeaseApi::DeviceId>(
            endpoint.bassDeviceIndex)) {
        appendAudioDebugLog(
            QString("bass_endpoint_bind_failed reason=process_device_conflict current_index=%1 target_index=%2 target_endpoint=%3")
                .arg(currentDevice)
                .arg(endpoint.bassDeviceIndex)
                .arg(endpoint.endpointId));
        return false;
    }
    bassOutputDeviceIndex_ = endpoint.bassDeviceIndex;
    bassOutputEndpointId_ = endpoint.endpointId;
#endif
    if (!ensureBassFxLoaded()) {
        _mc_op_.fail(QStringLiteral("bass_fx load failed"));
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
            QString("reused=0 elapsed_ms=%1 ok=0 reason=bass_fx").arg(timer.elapsed()),
            true);
        return false;
    }
    if (!bassDeviceLease_.acquired()) {
        bassDeviceLease_ = miacode::preview_audio::PreviewBassDeviceLease::acquire({
            [] { return static_cast<miacode::preview_audio::BassDeviceLeaseApi::DeviceId>(BASS_GetDevice()); },
            [this] {
#ifdef Q_OS_WIN
                return BASS_Init(
                           bassOutputDeviceIndex_,
                           static_cast<int>(deviceSampleRate_),
                           0,
                           nullptr,
                           nullptr) != FALSE;
#else
                return BASS_Init(-1, static_cast<int>(deviceSampleRate_), 0, nullptr, nullptr) != FALSE;
#endif
            },
            [] { BASS_Free(); },
        });
        if (!bassDeviceLease_.acquired()) {
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
        bassDeviceLease_.release();
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
        bassDeviceLease_.release();
        appendBassDebugLog(
            miacode::preview_audio::bass::BassDebugOperation::InitializeAudioEngine,
            QString("reused=0 elapsed_ms=%1 ok=0 reason=master_play").arg(timer.elapsed()),
            true);
        return false;
    }
    engineInitialized_ = true;
#ifdef Q_OS_WIN
    // Publish only a fully initialized concrete endpoint. The native device callback
    // can now pause this output immediately without touching worker-owned streams.
    miacode::preview_audio::PreviewBassEmergencyPause::arm(bassOutputDeviceIndex_);
#endif
    // Started after engineInitialized_, so the sampler never queries a half-built engine.
    startAudioHealthSampler();
    appendAudioDebugLog(
        QString("bass_engine_ready sample_rate=%1 output_index=%2 output_endpoint=%3")
            .arg(deviceSampleRate_)
            .arg(bassOutputDeviceIndex_)
            .arg(bassOutputEndpointId_.isEmpty() ? QStringLiteral("(default)")
                                                  : bassOutputEndpointId_));
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

void BassPreviewAudioBackend::invalidateOutputDevice()
{
    MC_OP("BassPreviewAudioBackend::invalidateOutputDevice");
#ifdef MIACODE_HAS_BASS_AUDIO
    // DeviceChangePause runs on the backend-owning worker, after the transport has
    // already been paused and its SFX voices stopped.  Destroying every stream here
    // is intentional: retaining a stream that lost its endpoint allows BASS/Windows
    // to continue its decoded tail when another default endpoint becomes available.
    const int previousDeviceIndex = bassOutputDeviceIndex_;
    const QString previousEndpointId = bassOutputEndpointId_;
    // Serializes against an in-flight Core Audio emergency pause before BASS_Free.
    miacode::preview_audio::PreviewBassEmergencyPause::disarm();
    invalidateRetainedPlaybackState(QStringLiteral("output_device_change"));
    preparedPlayback_ = PreparedPlaybackState();
    audioHealthPlaybackRunning_.store(false, std::memory_order_release);
    stopAudioHealthSampler();
    resetAssets();
    if (masterMixer_ != 0) {
        if (!BASS_StreamFree(masterMixer_)) {
            lastNativeErrorCode_ = static_cast<int>(BASS_ErrorGetCode());
        }
        masterMixer_ = 0;
    }
    unloadOptionalPlugins();
    bassDeviceLease_.release();
    engineInitialized_ = false;
    bassOutputDeviceIndex_ = -1;
    bassOutputEndpointId_.clear();
    outputDeviceRebuildRequired_ = true;
    appendAudioDebugLog(
        QString("bass_output_invalidated previous_index=%1 previous_endpoint=%2 next_play_rebuild=1")
            .arg(previousDeviceIndex)
            .arg(previousEndpointId.isEmpty() ? QStringLiteral("(none)") : previousEndpointId));
#endif
}
