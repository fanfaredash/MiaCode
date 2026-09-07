#include "PreviewAudioDeviceWatcher.h"

#include "common/DebugLog.h"
#include "common/DebugOptions.h"
#include "common/UiHangWatchdog.h"

#include <QAudioDevice>
#include <QElapsedTimer>
#include <QMetaObject>
#include <QMediaDevices>

#include <chrono>

#ifdef Q_OS_WIN
#include <mmdeviceapi.h>
#include <objbase.h>

#include <atomic>
#include <mutex>
#endif

namespace {

using miacode::preview_audio::device_change::OutputSnapshot;
using miacode::preview_audio::device_change::makeOutputSnapshot;

OutputSnapshot currentOutputSnapshot(const char* source)
{
    // This is only reached by the Qt fallback. Mark and time its synchronous
    // Qt Multimedia enumeration so the watchdog and the log can distinguish that
    // call from a GUI backlog ahead of it.
    MIACODE_HANG_PHASE("audio/device_qt_snapshot", QString::fromUtf8(source));
    QElapsedTimer elapsed;
    const bool trace = miacode::debug_options::audioDebugOutputEnabled();
    if (trace) {
        elapsed.start();
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/audio_device"),
            QStringLiteral("action=qt_snapshot_begin source=%1").arg(QLatin1String(source)));
    }
    QStringList outputIds;
    const QList<QAudioDevice> outputs = QMediaDevices::audioOutputs();
    outputIds.reserve(outputs.size());
    for (const QAudioDevice& output : outputs) {
        outputIds.append(QString::fromUtf8(output.id()));
    }
    OutputSnapshot snapshot = makeOutputSnapshot(
        std::move(outputIds), QString::fromUtf8(QMediaDevices::defaultAudioOutput().id()));
    if (trace) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/audio_device"),
            QStringLiteral("action=qt_snapshot_end source=%1 elapsed_ms=%2 outputs=%3 default_output=%4")
                .arg(QLatin1String(source))
                .arg(elapsed.elapsed())
                .arg(snapshot.outputIds.size())
                .arg(snapshot.defaultOutputId.isEmpty() ? QStringLiteral("(none)")
                                                        : snapshot.defaultOutputId));
    }
    return snapshot;
}

qint64 monotonicNowNs()
{
    return static_cast<qint64>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
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

    // Atomic because this object exists specifically to be called from another thread: the
    // Windows audio service invokes IMMNotificationClient on its own MTA thread, and it
    // AddRef/Releases across that boundary. A plain ULONG here is a torn refcount away from
    // either a leak or a double free, and the failure would land on the device-switch path
    // that is already the hardest one to reproduce.
    ULONG STDMETHODCALLTYPE AddRef() override
    {
        return references_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    ULONG STDMETHODCALLTYPE Release() override
    {
        // acq_rel so the decrement that reaches zero happens-after every other thread's
        // use of this object, which is what makes the delete below safe.
        const ULONG references = references_.fetch_sub(1, std::memory_order_acq_rel) - 1;
        if (references == 0) {
            delete this;
        }
        return references;
    }

    // Severs the back-pointer before the owner finishes destructing.
    //
    // UnregisterEndpointNotificationCallback stops FUTURE callbacks but does not promise
    // that an OnDefaultDeviceChanged already executing has returned. That callback holds a
    // raw pointer to the PreviewAudioDeviceWatcher and calls a method on it, so without
    // this the teardown path has a real -- if narrow -- use-after-free, of the kind that
    // only ever shows up as an unreproducible exit crash.
    //
    // Taking the same mutex the callback holds makes this a barrier, not just a store: it
    // blocks until an in-flight callback has finished with owner_, and every later one
    // sees nullptr. Safe against deadlock because the callback does no blocking work under
    // the mutex -- handleNativeDefaultOutputChanged only posts a queued invocation, which
    // returns immediately rather than waiting on the owner's thread.
    void detachOwner()
    {
        const std::lock_guard<std::mutex> lock(ownerMutex_);
        owner_ = nullptr;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override
    {
        notifyOwner(PreviewAudioDeviceWatcher::Change::OutputListChanged);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override
    {
        notifyOwner(PreviewAudioDeviceWatcher::Change::OutputListChanged);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override
    {
        notifyOwner(PreviewAudioDeviceWatcher::Change::OutputListChanged);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(
        EDataFlow flow,
        ERole role,
        LPCWSTR) override
    {
        if (flow != eRender || (role != eConsole && role != eMultimedia)) {
            return S_OK;
        }
        notifyOwner(PreviewAudioDeviceWatcher::Change::DefaultOutputChanged);
        return S_OK;
    }

private:
    void notifyOwner(PreviewAudioDeviceWatcher::Change change)
    {
        // Held across the call, not just across the read: the owner may start destructing
        // between a bare null check and the dereference. See detachOwner().
        const std::lock_guard<std::mutex> lock(ownerMutex_);
        if (owner_ != nullptr) {
            owner_->handleNativeOutputChanged(change);
        }
    }

    ~PreviewAudioNativeEndpointNotificationClient() = default;

    std::mutex ownerMutex_;
    PreviewAudioDeviceWatcher* owner_ = nullptr;
    std::atomic<ULONG> references_{1};
};

#endif

PreviewAudioDeviceWatcher::PreviewAudioDeviceWatcher(QObject* parent)
    : QObject(parent)
{
#ifdef Q_OS_WIN
    // Sentinel for "this step was never reached", so the log can tell "CoCreateInstance
    // never ran" from "CoCreateInstance failed". Facility 0x7ff is unassigned, so no real
    // API returns it.
    constexpr HRESULT kStepNotAttempted = static_cast<HRESULT>(0xFFFFFFFFu);
    HRESULT enumeratorResult = kStepNotAttempted;
    HRESULT registerResult = kStepNotAttempted;

    // APARTMENTTHREADED, not MULTITHREADED: this constructor runs on the GUI thread, and
    // the GUI thread's apartment is not ours to choose. Qt initialises it as an STA on
    // Windows because OLE drag-and-drop requires one.
    //
    // Asking for MULTITHREADED here was relying on an unasserted ordering. In practice Qt
    // has already run, so the call returns RPC_E_CHANGED_MODE and is harmlessly ignored --
    // a field capture confirms exactly that (`com_hr=0x80010106`). But the day some
    // construction-order change lets this run first, it would put the GUI thread into an
    // MTA, silently breaking drag-and-drop and shell dialogs with no log to explain it.
    //
    // Requesting the apartment the thread should already have removes that failure mode:
    // it returns S_FALSE when Qt got there first (success, refcount incremented, so the
    // CoUninitialize below stays balanced) and yields the correct STA if it did not.
    // IMMDeviceEnumerator and IMMNotificationClient are both agile, so the notification
    // still arrives on the audio service's own MTA thread either way -- which is precisely
    // why the client's refcount and back-pointer have to be thread-safe.
    const HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
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

    // IMMNotificationClient is authoritative once registration succeeds. In particular,
    // never create QMediaDevices on that path: its synchronous output snapshot has been
    // observed blocking the GUI thread in AudioSes during a hotplug. The fallback remains
    // available on Core Audio registration failure.
    if (nativeEndpointNotificationClient_ == nullptr) {
        enableQtFallback();
    }

    // Without this line a capture that contains no `action=native_default_output_changed`
    // is ambiguous: the user may simply never have switched devices, or this listener may
    // never have armed. Recording the registration outcome itself removes that ambiguity.
    if (miacode::debug_options::audioDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/audio_device"),
            QStringLiteral("action=native_registration com_hr=0x%1 com_owned=%2 "
                           "enumerator_hr=0x%3 register_hr=0x%4 registered=%5 source=%6 "
                           "fallback_outputs=%7 fallback_default_output=%8")
                .arg(static_cast<quint32>(comResult), 8, 16, QLatin1Char('0'))
                .arg(nativeComInitialized_ ? 1 : 0)
                .arg(static_cast<quint32>(enumeratorResult), 8, 16, QLatin1Char('0'))
                .arg(static_cast<quint32>(registerResult), 8, 16, QLatin1Char('0'))
                .arg(nativeEndpointNotificationClient_ != nullptr ? 1 : 0)
                .arg(nativeEndpointNotificationClient_ != nullptr
                         ? QStringLiteral("core_audio")
                         : QStringLiteral("qt_fallback"))
                .arg(snapshot_.outputIds.size())
                .arg(snapshot_.defaultOutputId.isEmpty() ? QStringLiteral("(none)")
                                                         : snapshot_.defaultOutputId));
    }
#else
    enableQtFallback();
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

void PreviewAudioDeviceWatcher::enableQtFallback()
{
    if (mediaDevices_ != nullptr) {
        return;
    }

    mediaDevices_ = new QMediaDevices(this);
    snapshot_ = currentOutputSnapshot("qt_fallback_constructor");
    // Use AutoConnection so a notification already delivered on the GUI thread enters
    // the snapshot comparison immediately instead of waiting behind other queued GUI
    // work. If Qt raises it from another thread, Qt still queues it to this object's
    // thread, keeping the consumer's BASS/timeline pause path off the notifier stack.
    connect(mediaDevices_, &QMediaDevices::audioOutputsChanged,
            this, &PreviewAudioDeviceWatcher::handleAudioOutputsChanged,
            Qt::AutoConnection);
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
        // Order matters, and all three steps are required:
        //   1. Unregister  -- stops new callbacks being dispatched,
        //   2. detachOwner -- waits out any callback already running and blinds later ones,
        //   3. Release     -- drops our reference; the audio service may still hold one.
        // Step 2 is unconditional: if CoCreateInstance failed above we could not
        // unregister, which makes severing the back-pointer more important, not less.
        client->detachOwner();
        client->Release();
        nativeEndpointNotificationClient_ = nullptr;
    }
    if (nativeComInitialized_) {
        CoUninitialize();
    }
#endif
    // On Windows detachOwner() above is a barrier for an in-flight native callback;
    // on other platforms this simply releases the fallback handler during teardown.
    setDirectCutoffHandler({});
}

void PreviewAudioDeviceWatcher::setDirectCutoffHandler(DirectCutoffHandler handler)
{
    const std::lock_guard<std::mutex> lock(directCutoffHandlerMutex_);
    directCutoffHandler_ = std::move(handler);
}

PreviewAudioDeviceWatcher::DeviceCutoff PreviewAudioDeviceWatcher::requestDirectCutoff(Change change)
{
    DirectCutoffHandler handler;
    {
        const std::lock_guard<std::mutex> lock(directCutoffHandlerMutex_);
        handler = directCutoffHandler_;
    }
    DeviceCutoff cutoff;
    cutoff.change = change;
    if (handler) {
        cutoff = handler(change);
        cutoff.change = change;
    }
    return cutoff;
}

void PreviewAudioDeviceWatcher::handleNativeOutputChanged(Change change)
{
#ifdef Q_OS_WIN
    // Core Audio calls on its MTA. Queue the worker cutoff before posting GUI work:
    // the operating system is allowed to move a default-following stream meanwhile.
    const DeviceCutoff cutoff = requestDirectCutoff(change);
    QMetaObject::invokeMethod(this, [this, cutoff] { deliverNativeOutputChange(cutoff); },
                              Qt::QueuedConnection);
#endif
}

void PreviewAudioDeviceWatcher::deliverNativeOutputChange(const DeviceCutoff& cutoff)
{
    DeviceCutoff delivered = cutoff;
    delivered.guiDeliveryMonotonicNs = monotonicNowNs();
    if (miacode::debug_options::audioDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/audio_device"),
            QStringLiteral("action=native_output_event change=%1 cutoff_armed=%2 cutoff_posted=%3 "
                           "cutoff_second=%4 event_ns=%5 gui_delivery_ns=%6 gui_delay_ms=%7 "
                           "route_only=%8 emergency_attempted=%9 emergency_ok=%10 emergency_device=%11 "
                           "emergency_error=%12 emergency_begin_ns=%13 emergency_end_ns=%14 generation=%15 "
                           "sequence=%16 source=core_audio")
                .arg(QLatin1String(changeName(delivered.change)))
                .arg(delivered.armedPlaybackWasCut ? 1 : 0)
                .arg(delivered.post.accepted ? 1 : 0)
                .arg(delivered.cutoffSecond, 0, 'f', 6)
                .arg(delivered.eventMonotonicNs)
                .arg(delivered.guiDeliveryMonotonicNs)
                .arg(delivered.eventMonotonicNs > 0
                         ? static_cast<double>(delivered.guiDeliveryMonotonicNs - delivered.eventMonotonicNs) / 1'000'000.0
                         : -1.0,
                     0,
                     'f',
                     3)
                .arg(delivered.outputRouteInvalidationOnly ? 1 : 0)
                .arg(delivered.emergencyPauseAttempted ? 1 : 0)
                .arg(delivered.emergencyPauseSucceeded ? 1 : 0)
                .arg(delivered.emergencyPauseDeviceIndex)
                .arg(delivered.emergencyPauseError)
                .arg(delivered.emergencyPauseStartedNs)
                .arg(delivered.emergencyPauseFinishedNs)
                .arg(delivered.identity.generation)
                .arg(delivered.identity.sequence));
    }
    if (delivered.armedPlaybackWasCut) {
        emit deviceCutoffRequested(delivered);
    }
    emit outputConfigurationChanged(delivered.change);
}

void PreviewAudioDeviceWatcher::handleAudioOutputsChanged()
{
    const OutputSnapshot previous = snapshot_;
    OutputSnapshot current = currentOutputSnapshot("qt_signal");
    const Change change = compareSnapshots(previous, current);
    if (change == Change::None) {
        return;
    }
    snapshot_ = std::move(current);

    // Qt is used only on non-Windows or after Windows native registration failed.
    // It uses the same atomic cutoff protocol as the Core Audio source.
    DeviceCutoff cutoff = requestDirectCutoff(change);
    cutoff.guiDeliveryMonotonicNs = monotonicNowNs();
    if (miacode::debug_options::audioDebugOutputEnabled()) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Audio,
            QStringLiteral("preview/audio_device"),
            QStringLiteral("action=outputs_changed change=%1 outputs_before=%2 outputs_after=%3 "
                           "default_before=%4 default_after=%5 cutoff_armed=%6 cutoff_posted=%7 "
                           "cutoff_second=%8 event_ns=%9 gui_delivery_ns=%10 gui_delay_ms=%11 route_only=%12 "
                           "generation=%13 sequence=%14")
                .arg(QLatin1String(changeName(change)))
                .arg(previous.outputIds.size())
                .arg(snapshot_.outputIds.size())
                .arg(previous.defaultOutputId.isEmpty() ? QStringLiteral("(none)")
                                                        : previous.defaultOutputId)
                .arg(snapshot_.defaultOutputId.isEmpty() ? QStringLiteral("(none)")
                                                         : snapshot_.defaultOutputId)
                .arg(cutoff.armedPlaybackWasCut ? 1 : 0)
                .arg(cutoff.post.accepted ? 1 : 0)
                .arg(cutoff.cutoffSecond, 0, 'f', 6)
                .arg(cutoff.eventMonotonicNs)
                .arg(cutoff.guiDeliveryMonotonicNs)
                .arg(cutoff.eventMonotonicNs > 0
                         ? static_cast<double>(cutoff.guiDeliveryMonotonicNs - cutoff.eventMonotonicNs) / 1'000'000.0
                         : -1.0,
                     0,
                     'f',
                     3)
                .arg(cutoff.outputRouteInvalidationOnly ? 1 : 0)
                .arg(cutoff.identity.generation)
                .arg(cutoff.identity.sequence));
    }

    if (cutoff.armedPlaybackWasCut) {
        emit deviceCutoffRequested(cutoff);
    }
    emit outputConfigurationChanged(change);
}
