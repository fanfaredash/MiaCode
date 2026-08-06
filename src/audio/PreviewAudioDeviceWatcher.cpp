#include "PreviewAudioDeviceWatcher.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"

#include <QAudioDevice>
#include <QMetaObject>
#include <QMediaDevices>

#ifdef Q_OS_WIN
#include <mmdeviceapi.h>
#include <objbase.h>
#endif

namespace {

using miacode::preview_audio::device_change::OutputSnapshot;
using miacode::preview_audio::device_change::makeOutputSnapshot;

OutputSnapshot currentOutputSnapshot()
{
    QStringList outputIds;
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    outputIds.reserve(outputs.size());
    for (const QAudioDevice& output : outputs) {
        outputIds.append(QString::fromUtf8(output.id()));
    }
    return makeOutputSnapshot(std::move(outputIds),
                              QString::fromUtf8(QMediaDevices::defaultAudioOutput().id()));
}

}  // namespace

#ifdef Q_OS_WIN

class PreviewAudioNativeEndpointNotificationClient final : public IMMNotificationClient
{
public:
    explicit PreviewAudioNativeEndpointNotificationClient(PreviewAudioDeviceWatcher* owner)
        : owner_(owner)
    {
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** object) override
    {
        if (object == nullptr) {
            return E_POINTER;
        }
        *object = nullptr;
        if (riid == IID_IUnknown || riid == __uuidof(IMMNotificationClient)) {
            *object = static_cast<IMMNotificationClient*>(this);
            AddRef();
            return S_OK;
        }
        return E_NOINTERFACE;
    }

    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return ++references_;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        const ULONG references = --references_;
        if (references == 0) {
            delete this;
        }
        return references;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow flow,
        ERole role,
        LPCWSTR) override
    {
        if (flow == eRender && role == eConsole && owner_ != nullptr) {
            owner_->handleNativeDefaultOutputChanged();
        }
        return S_OK;
    }

private:
    ~PreviewAudioNativeEndpointNotificationClient() = default;

    PreviewAudioDeviceWatcher* owner_ = nullptr;
    ULONG references_ = 1;
};

#endif

PreviewAudioDeviceWatcher::PreviewAudioDeviceWatcher(QObject* parent)
    : QObject(parent)
    , mediaDevices_(new QMediaDevices(this))
    , snapshot_(currentOutputSnapshot())
{
    // Use AutoConnection so a notification already delivered on the GUI thread enters
    // the snapshot comparison immediately instead of waiting behind other queued GUI
    // work. If Qt raises it from another thread, Qt still queues it to this object's
    // thread, keeping the consumer's BASS/timeline pause path off the notifier stack.
    connect(mediaDevices_, &QMediaDevices::audioOutputsChanged,
            this, &PreviewAudioDeviceWatcher::handleAudioOutputsChanged,
            Qt::AutoConnection);

#ifdef Q_OS_WIN
    // Sentinel for "this step was never reached", so the log can tell "CoCreateInstance
    // never ran" from "CoCreateInstance failed". Facility 0x7ff is unassigned, so no real
    // API returns it.
    constexpr HRESULT kStepNotAttempted = static_cast<HRESULT>(0xFFFFFFFFu);
    HRESULT enumeratorResult = kStepNotAttempted;
    HRESULT registerResult = kStepNotAttempted;

    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    // Only undo a COM initialization performed by this object.  On
    // RPC_E_CHANGED_MODE the thread already has an incompatible apartment;
    // COM can still be usable, but calling CoUninitialize here would undo
    // somebody else's initialization.
    nativeComInitialized_ = SUCCEEDED(comResult);
    if (SUCCEEDED(comResult) || comResult == RPC_E_CHANGED_MODE) {
        IMMDeviceEnumerator* enumerator = nullptr;
        enumeratorResult = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator));
        if (SUCCEEDED(enumeratorResult) && enumerator != nullptr) {
            auto* client = new PreviewAudioNativeEndpointNotificationClient(this);
            registerResult = enumerator->RegisterEndpointNotificationCallback(client);
            if (SUCCEEDED(registerResult)) {
                nativeEndpointNotificationClient_ = client;
            } else {
                client->Release();
            }
            enumerator->Release();
        }
    }

    // Without this line a capture that contains no `action=native_default_output_changed`
    // is ambiguous: the user may simply never have switched devices, or this listener may
    // never have armed. Mirrors `action=registration` in
    // src/app/WindowsIdleEventDiagnostics.cpp, which records the same thing for the
    // Windows session/power notifications.
    if (miacode::debug_options::audioDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/audio_device"),
            QStringLiteral("action=native_registration com_hr=0x%1 com_owned=%2 "
                           "enumerator_hr=0x%3 register_hr=0x%4 registered=%5 outputs=%6 "
                           "default_output=%7")
                .arg(static_cast<quint32>(comResult), 8, 16, QLatin1Char('0'))
                .arg(nativeComInitialized_ ? 1 : 0)
                .arg(static_cast<quint32>(enumeratorResult), 8, 16, QLatin1Char('0'))
                .arg(static_cast<quint32>(registerResult), 8, 16, QLatin1Char('0'))
                .arg(nativeEndpointNotificationClient_ != nullptr ? 1 : 0)
                .arg(snapshot_.outputIds.size())
                .arg(snapshot_.defaultOutputId.isEmpty() ? QStringLiteral("(none)")
                                                         : snapshot_.defaultOutputId));
    }
#else
    // There is no native endpoint listener off Windows — QMediaDevices is the only
    // source of device-change notifications here. Emitted so a non-Windows capture reads
    // as "not supported on this platform" rather than "armed and never fired".
    if (miacode::debug_options::audioDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/audio_device"),
            QStringLiteral("action=native_registration supported=0"));
    }
#endif
}

PreviewAudioDeviceWatcher::~PreviewAudioDeviceWatcher()
{
#ifdef Q_OS_WIN
    auto* client = static_cast<PreviewAudioNativeEndpointNotificationClient*>(
        nativeEndpointNotificationClient_);
    if (client != nullptr) {
        IMMDeviceEnumerator* enumerator = nullptr;
        const HRESULT result = CoCreateInstance(
            __uuidof(MMDeviceEnumerator),
            nullptr,
            CLSCTX_ALL,
            __uuidof(IMMDeviceEnumerator),
            reinterpret_cast<void**>(&enumerator));
        if (SUCCEEDED(result) && enumerator != nullptr) {
            enumerator->UnregisterEndpointNotificationCallback(client);
            enumerator->Release();
        }
        client->Release();
        nativeEndpointNotificationClient_ = nullptr;
    }
    if (nativeComInitialized_) {
        CoUninitialize();
    }
#endif
}

void PreviewAudioDeviceWatcher::handleNativeDefaultOutputChanged()
{
#ifdef Q_OS_WIN
    QMetaObject::invokeMethod(this, [this]() {
        if (miacode::debug_options::audioDebugOutputEnabled()) {
            miacode::debug_log::appendLine(
                miacode::debug_log::Channel::Audio,
                QStringLiteral("preview/audio_device"),
                QStringLiteral("action=native_default_output_changed"));
        }
        emit outputConfigurationChanged(Change::DefaultOutputChanged);
    }, Qt::QueuedConnection);
#endif
}

void PreviewAudioDeviceWatcher::handleAudioOutputsChanged()
{
    const OutputSnapshot previous = snapshot_;
    OutputSnapshot current = currentOutputSnapshot();
    const Change change = compareSnapshots(previous, current);
    if (change == Change::None) {
        return;
    }
    snapshot_ = std::move(current);

    // Audio channel, so this lands in miacode_audio_debug.log next to the
    // `preview/playback pause_exact` it causes and the `bass_status` / `bgm_delta_ms`
    // rows used to judge the desync.
    if (miacode::debug_options::audioDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/audio_device"),
            QStringLiteral("action=outputs_changed change=%1 outputs_before=%2 outputs_after=%3 "
                           "default_before=%4 default_after=%5")
                .arg(QLatin1String(changeName(change)))
                .arg(previous.outputIds.size())
                .arg(snapshot_.outputIds.size())
                .arg(previous.defaultOutputId.isEmpty() ? QStringLiteral("(none)")
                                                        : previous.defaultOutputId)
                .arg(snapshot_.defaultOutputId.isEmpty() ? QStringLiteral("(none)")
                                                         : snapshot_.defaultOutputId));
    }

    emit outputConfigurationChanged(change);
}
