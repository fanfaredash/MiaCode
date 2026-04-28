#include "render/backend_d3d11/PreviewDCompTextureCache.h"

#include "common/DebugLog.h"

#include <QImage>
#include <QString>

namespace miacode::preview::dcomp {

namespace {

#ifdef Q_OS_WIN
void logCache(const char* action, const QString& extra = QString())
{
    QString payload = QStringLiteral("action=%1").arg(QString::fromLatin1(action));
    if (!extra.isEmpty()) {
        payload += QStringLiteral(" ") + extra;
    }
    miacode::debug_log::appendLine(
        miacode::debug_log::Channel::Runtime,
        QStringLiteral("render/backend_d3d11/texture_cache"),
        payload);
}
#endif

}  // namespace

PreviewDCompTextureCache::PreviewDCompTextureCache() = default;

PreviewDCompTextureCache::~PreviewDCompTextureCache()
{
    clear();
}

#ifdef Q_OS_WIN

ID3D11ShaderResourceView* PreviewDCompTextureCache::lookupOrCreate(
    const QImage* image, ID3D11Device* device, bool cacheable)
{
    if (image == nullptr || image->isNull() || device == nullptr) {
        logCache("reject_null",
                 QStringLiteral("img_ptr=%1 isnull=%2 device_ptr=%3")
                     .arg(reinterpret_cast<quintptr>(image), 0, 16)
                     .arg(image && image->isNull() ? 1 : 0)
                     .arg(reinterpret_cast<quintptr>(device), 0, 16));
        return nullptr;
    }
    if (image->width() <= 0 || image->height() <= 0) {
        logCache("reject_size",
                 QStringLiteral("w=%1 h=%2 cacheable=%3 format=%4 cache_key=0x%5")
                     .arg(image->width())
                     .arg(image->height())
                     .arg(cacheable ? 1 : 0)
                     .arg(static_cast<int>(image->format()))
                     .arg(static_cast<quint64>(image->cacheKey()), 0, 16));
        return nullptr;
    }

    const qint64 key = image->cacheKey();
    if (cacheable) {
        if (auto it = cacheable_.constFind(key); it != cacheable_.constEnd()) {
            return *it;
        }
    } else {
        if (auto it = transient_.constFind(key); it != transient_.constEnd()) {
            return *it;
        }
    }

    // Convert to a D3D11-friendly format. R8G8B8A8_UNORM with premultiplied
    // alpha matches the swap chain's DXGI_ALPHA_MODE_PREMULTIPLIED so DComp
    // composites correctly with the QML scene below.
    const QImage normalized =
        (image->format() == QImage::Format_RGBA8888_Premultiplied)
            ? *image
            : image->convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (normalized.isNull()) {
        logCache("convert_failed",
                 QStringLiteral("key=0x%1 src_format=%2")
                     .arg(static_cast<quint64>(key), 0, 16)
                     .arg(static_cast<int>(image->format())));
        return nullptr;
    }

    D3D11_TEXTURE2D_DESC td{};
    td.Width = static_cast<UINT>(normalized.width());
    td.Height = static_cast<UINT>(normalized.height());
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA srd{};
    srd.pSysMem = normalized.constBits();
    srd.SysMemPitch = static_cast<UINT>(normalized.bytesPerLine());

    Microsoft::WRL::ComPtr<ID3D11Texture2D> texture;
    HRESULT hr = device->CreateTexture2D(&td, &srd, texture.GetAddressOf());
    if (FAILED(hr)) {
        logCache("create_texture_failed",
                 QStringLiteral("hr=0x%1 key=0x%2 w=%3 h=%4")
                     .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'))
                     .arg(static_cast<quint64>(key), 0, 16)
                     .arg(normalized.width()).arg(normalized.height()));
        return nullptr;
    }

    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
    hr = device->CreateShaderResourceView(texture.Get(), nullptr,
                                           srv.GetAddressOf());
    if (FAILED(hr)) {
        logCache("create_srv_failed",
                 QStringLiteral("hr=0x%1 key=0x%2")
                     .arg(static_cast<unsigned long>(hr), 8, 16, QChar('0'))
                     .arg(static_cast<quint64>(key), 0, 16));
        return nullptr;
    }

    // Manual AddRef so the cache holds an owning ref to the SRV. Local
    // ComPtr `srv` releases its ref when this function returns; the
    // cache's stored raw pointer keeps the SRV alive via this AddRef.
    ID3D11ShaderResourceView* raw = srv.Get();
    raw->AddRef();
    if (cacheable) {
        cacheable_.insert(key, raw);
        if ((cacheable_.size() % 32) == 0) {
            logCache("cacheable_growth",
                     QStringLiteral("count=%1").arg(cacheable_.size()));
        }
        // Phase 3.6 — cache cap mirroring PreviewTextureRepository's 96
        // entry / 96MB triggers (PreviewTextureRepository.cpp:9-10).
        // When the cap is exceeded we flush the entire cacheable cache
        // and rebuild lazily on the next lookup. The current entry
        // (`raw`) was just added and stays via the lookupOrCreate
        // return; only the older entries are dropped. Without a cap a
        // long edit session that loads many distinct sprite skins
        // could grow unbounded.
        if (cacheable_.size() > kCacheableEntryCap) {
            // Save the entry we just inserted, drop everything else,
            // then re-insert. That way the caller keeps a valid SRV.
            cacheable_.remove(key);
            for (auto it = cacheable_.cbegin(); it != cacheable_.cend(); ++it) {
                if (*it != nullptr) (*it)->Release();
            }
            cacheable_.clear();
            cacheable_.insert(key, raw);
            logCache("cacheable_flush",
                     QStringLiteral("reason=entry_cap cap=%1 retained_key=0x%2")
                         .arg(kCacheableEntryCap)
                         .arg(static_cast<quint64>(key), 0, 16));
        }
    } else {
        transient_.insert(key, raw);
    }
    return raw;
}

void PreviewDCompTextureCache::evictCacheableKey(qint64 key)
{
    auto it = cacheable_.find(key);
    if (it == cacheable_.end()) return;
    if (*it != nullptr) {
        (*it)->Release();
    }
    cacheable_.erase(it);
}

void PreviewDCompTextureCache::endFrame()
{
    // Wipe the per-frame transient compartment. Each Release pairs with
    // the AddRef in lookupOrCreate; SRV + texture are freed once both
    // the immediate context's binding refs and our manual ref drop.
    for (auto it = transient_.cbegin(); it != transient_.cend(); ++it) {
        if (*it != nullptr) {
            (*it)->Release();
        }
    }
    transient_.clear();
}

void PreviewDCompTextureCache::clear()
{
    const int total = cacheable_.size() + transient_.size();
    if (total > 0) {
        logCache("clear",
                 QStringLiteral("cacheable=%1 transient=%2")
                     .arg(cacheable_.size()).arg(transient_.size()));
    }
    for (auto it = cacheable_.cbegin(); it != cacheable_.cend(); ++it) {
        if (*it != nullptr) {
            (*it)->Release();
        }
    }
    for (auto it = transient_.cbegin(); it != transient_.cend(); ++it) {
        if (*it != nullptr) {
            (*it)->Release();
        }
    }
    cacheable_.clear();
    transient_.clear();
}

#endif  // Q_OS_WIN

}  // namespace miacode::preview::dcomp
