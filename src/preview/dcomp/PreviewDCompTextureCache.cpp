#include "preview/dcomp/PreviewDCompTextureCache.h"

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
        QStringLiteral("preview/dcomp/texture_cache"),
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
    const QImage* image, ID3D11Device* device)
{
    if (image == nullptr || image->isNull() || device == nullptr) {
        return nullptr;
    }
    if (image->width() <= 0 || image->height() <= 0) {
        return nullptr;
    }

    const qint64 key = image->cacheKey();
    if (auto it = cache_.constFind(key); it != cache_.constEnd()) {
        return it->Get();
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

    cache_.insert(key, srv);
    if ((cache_.size() % 32) == 0) {
        // Sparse log so we can see cache growth without flooding when
        // chart sprites populate the cache for the first time.
        logCache("growth", QStringLiteral("count=%1").arg(cache_.size()));
    }
    return srv.Get();
}

void PreviewDCompTextureCache::clear()
{
    if (!cache_.isEmpty()) {
        logCache("clear", QStringLiteral("count=%1").arg(cache_.size()));
        cache_.clear();
    }
}

#endif  // Q_OS_WIN

}  // namespace miacode::preview::dcomp
