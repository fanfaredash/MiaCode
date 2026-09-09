#include "preview/quick_scene/PreviewQuickGraphicsInfo.h"

#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QQuickWindow>
#include <QSGRendererInterface>

#if __has_include(<QVulkanFunctions>) && __has_include(<QVulkanInstance>)
#include <QVulkanFunctions>
#include <QVulkanInstance>
#define MIACODE_HAS_QT_VULKAN_PROBE 1
#endif

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi.h>
#endif

#ifdef Q_OS_MACOS
#include <objc/message.h>
#include <objc/runtime.h>
#endif

namespace miacode::preview::quick_scene {

namespace {

QString graphicsApiName(QSGRendererInterface::GraphicsApi api)
{
    switch (api) {
    case QSGRendererInterface::Software:   return QStringLiteral("Software");
    case QSGRendererInterface::OpenGL:     return QStringLiteral("OpenGL");
    case QSGRendererInterface::Direct3D11: return QStringLiteral("Direct3D11");
    case QSGRendererInterface::Direct3D12: return QStringLiteral("Direct3D12");
    case QSGRendererInterface::Vulkan:     return QStringLiteral("Vulkan");
    case QSGRendererInterface::Metal:      return QStringLiteral("Metal");
    case QSGRendererInterface::Null:       return QStringLiteral("Null");
    case QSGRendererInterface::Unknown:
    default:                               return QStringLiteral("Unknown");
    }
}

#ifdef Q_OS_WIN
QString dxgiDriverVersion(IDXGIAdapter* adapter)
{
    LARGE_INTEGER version{};
    if (adapter == nullptr
        || FAILED(adapter->CheckInterfaceSupport(__uuidof(IDXGIDevice), &version))) {
        return QStringLiteral("(unavailable)");
    }
    return QStringLiteral("%1.%2.%3.%4")
        .arg(HIWORD(version.HighPart))
        .arg(LOWORD(version.HighPart))
        .arg(HIWORD(version.LowPart))
        .arg(LOWORD(version.LowPart));
}

bool populateDxgiAdapterInfo(
    IDXGIAdapter* adapter, const QString& apiName, QString featureLevel, QuickGraphicsInfo* info)
{
    if (adapter == nullptr || info == nullptr) {
        return false;
    }
    DXGI_ADAPTER_DESC desc{};
    if (FAILED(adapter->GetDesc(&desc))) {
        return false;
    }
    info->deviceName = QString::fromWCharArray(desc.Description).trimmed();
    info->deviceIdentified = !info->deviceName.isEmpty();
    const quint64 dedicatedMb =
        static_cast<quint64>(desc.DedicatedVideoMemory) / (1024ull * 1024ull);
    const quint64 sharedMb =
        static_cast<quint64>(desc.SharedSystemMemory) / (1024ull * 1024ull);
    const quint64 dedicatedSystemMb =
        static_cast<quint64>(desc.DedicatedSystemMemory) / (1024ull * 1024ull);
    info->logFields = QStringLiteral(
        "rhi_api=%1 adapter=\"%2\" vendor_id=0x%3 device_id=0x%4 "
        "subsys_id=0x%5 revision=%6 luid=0x%7:0x%8 dedicated_video_mb=%9 "
        "shared_system_mb=%10 dedicated_system_mb=%11 umd_driver_version=%12%13")
        .arg(apiName)
        .arg(info->deviceName)
        .arg(desc.VendorId, 4, 16, QLatin1Char('0'))
        .arg(desc.DeviceId, 4, 16, QLatin1Char('0'))
        .arg(desc.SubSysId, 8, 16, QLatin1Char('0'))
        .arg(desc.Revision)
        .arg(static_cast<quint32>(desc.AdapterLuid.HighPart), 0, 16)
        .arg(static_cast<quint32>(desc.AdapterLuid.LowPart), 0, 16)
        .arg(dedicatedMb)
        .arg(sharedMb)
        .arg(dedicatedSystemMb)
        .arg(dxgiDriverVersion(adapter))
        .arg(featureLevel.isEmpty() ? QString() : QStringLiteral(" feature_level=") + featureLevel);
    return true;
}

IDXGIAdapter* dxgiAdapterForLuid(const LUID& luid)
{
    IDXGIFactory1* factory = nullptr;
    if (FAILED(::CreateDXGIFactory1(
            __uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))
        || factory == nullptr) {
        return nullptr;
    }
    IDXGIAdapter1* match = nullptr;
    for (UINT index = 0; ; ++index) {
        IDXGIAdapter1* adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (adapter == nullptr) {
            break;
        }
        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))
            && desc.AdapterLuid.HighPart == luid.HighPart
            && desc.AdapterLuid.LowPart == luid.LowPart) {
            match = adapter;
            break;
        }
        adapter->Release();
    }
    factory->Release();
    return match;
}
#endif

}  // namespace

QuickGraphicsInfo queryQuickGraphicsInfo(QQuickWindow* window)
{
    QuickGraphicsInfo info;
    if (window == nullptr || window->rendererInterface() == nullptr) {
        info.apiName = QStringLiteral("Unknown");
        info.logFields = QStringLiteral("rhi_api=Unknown note=renderer_interface_unavailable");
        return info;
    }

    QSGRendererInterface* renderer = window->rendererInterface();
    const QSGRendererInterface::GraphicsApi api = renderer->graphicsApi();
    info.apiName = graphicsApiName(api);
    info.hardwareAccelerated = api != QSGRendererInterface::Software
        && api != QSGRendererInterface::Null
        && api != QSGRendererInterface::Unknown;

    if (api == QSGRendererInterface::Software) {
        info.deviceName = QStringLiteral("Qt Quick Software Renderer");
        info.deviceIdentified = true;
        info.logFields = QStringLiteral("rhi_api=Software renderer=\"Qt Quick Software Renderer\"");
        return info;
    }

#ifdef Q_OS_WIN
    if (api == QSGRendererInterface::Direct3D11) {
        auto* device = static_cast<ID3D11Device*>(
            renderer->getResource(window, QSGRendererInterface::DeviceResource));
        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        if (device != nullptr
            && SUCCEEDED(device->QueryInterface(
                __uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))
            && dxgiDevice != nullptr
            && SUCCEEDED(dxgiDevice->GetAdapter(&adapter))
            && adapter != nullptr) {
            const QString featureLevel = QStringLiteral("0x%1")
                .arg(static_cast<quint32>(device->GetFeatureLevel()), 4, 16, QLatin1Char('0'));
            populateDxgiAdapterInfo(adapter, info.apiName, featureLevel, &info);
        }
        if (adapter != nullptr) {
            adapter->Release();
        }
        if (dxgiDevice != nullptr) {
            dxgiDevice->Release();
        }
        if (info.deviceIdentified) {
            return info;
        }
    }

    if (api == QSGRendererInterface::Direct3D12) {
        auto* device = static_cast<ID3D12Device*>(
            renderer->getResource(window, QSGRendererInterface::DeviceResource));
        if (device != nullptr) {
            IDXGIAdapter* adapter = dxgiAdapterForLuid(device->GetAdapterLuid());
            if (adapter != nullptr) {
                populateDxgiAdapterInfo(adapter, info.apiName, QString(), &info);
                adapter->Release();
            }
        }
        if (info.deviceIdentified) {
            return info;
        }
    }
#endif

    if (api == QSGRendererInterface::OpenGL) {
        QOpenGLContext* context = QOpenGLContext::currentContext();
        QOpenGLFunctions* functions = context != nullptr ? context->functions() : nullptr;
        if (functions != nullptr) {
            const GLubyte* rendererName = functions->glGetString(GL_RENDERER);
            const GLubyte* vendorName = functions->glGetString(GL_VENDOR);
            info.deviceName = rendererName != nullptr
                ? QString::fromLatin1(reinterpret_cast<const char*>(rendererName)).trimmed()
                : QString();
            info.deviceIdentified = !info.deviceName.isEmpty();
            info.logFields = QStringLiteral("rhi_api=OpenGL gl_vendor=\"%1\" gl_renderer=\"%2\"")
                .arg(vendorName != nullptr
                    ? QString::fromLatin1(reinterpret_cast<const char*>(vendorName))
                    : QStringLiteral("(unknown)"))
                .arg(info.deviceIdentified ? info.deviceName : QStringLiteral("(unknown)"));
            return info;
        }
    }

#ifdef Q_OS_MACOS
    if (api == QSGRendererInterface::Metal) {
        void* device = renderer->getResource(window, QSGRendererInterface::DeviceResource);
        if (device != nullptr) {
            using ObjectMessage = void* (*)(void*, SEL);
            using Utf8Message = const char* (*)(void*, SEL);
            void* name = reinterpret_cast<ObjectMessage>(objc_msgSend)(
                device, sel_registerName("name"));
            const char* utf8Name = name != nullptr
                ? reinterpret_cast<Utf8Message>(objc_msgSend)(name, sel_registerName("UTF8String"))
                : nullptr;
            if (utf8Name != nullptr) {
                info.deviceName = QString::fromUtf8(utf8Name).trimmed();
                info.deviceIdentified = !info.deviceName.isEmpty();
            }
        }
        info.logFields = QStringLiteral("rhi_api=Metal device=\"%1\"")
            .arg(info.deviceIdentified ? info.deviceName : QStringLiteral("(unknown)"));
        return info;
    }
#endif

#ifdef MIACODE_HAS_QT_VULKAN_PROBE
    if (api == QSGRendererInterface::Vulkan) {
        auto* instance = static_cast<QVulkanInstance*>(
            renderer->getResource(window, QSGRendererInterface::VulkanInstanceResource));
        auto* physicalDevice = static_cast<VkPhysicalDevice*>(
            renderer->getResource(window, QSGRendererInterface::PhysicalDeviceResource));
        if (instance != nullptr && physicalDevice != nullptr && *physicalDevice != VK_NULL_HANDLE) {
            VkPhysicalDeviceProperties properties{};
            instance->functions()->vkGetPhysicalDeviceProperties(*physicalDevice, &properties);
            info.deviceName = QString::fromUtf8(properties.deviceName).trimmed();
            info.deviceIdentified = !info.deviceName.isEmpty();
            info.logFields = QStringLiteral(
                "rhi_api=Vulkan device=\"%1\" vendor_id=0x%2 device_id=0x%3")
                .arg(info.deviceIdentified ? info.deviceName : QStringLiteral("(unknown)"))
                .arg(properties.vendorID, 4, 16, QLatin1Char('0'))
                .arg(properties.deviceID, 4, 16, QLatin1Char('0'));
            return info;
        }
    }
#endif

    info.logFields = QStringLiteral("rhi_api=%1 note=device_probe_unavailable")
        .arg(info.apiName);
    return info;
}

}  // namespace miacode::preview::quick_scene
