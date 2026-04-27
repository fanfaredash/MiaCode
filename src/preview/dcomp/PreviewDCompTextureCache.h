#pragma once

// Phase 3.3b of the DirectComposition preview path. Maps QImage instances
// to ID3D11ShaderResourceView (the D3D11 view object that VS+PS consume
// during a draw). The render thread is the sole user — single-threaded
// access throughout, no internal locking.
//
// Cache key: QImage::cacheKey(). This is Qt's per-instance identity for
// QImage; two QImages produced from the same source asset share a
// cacheKey (Qt deduplicates implicit-shared images), so a frame full of
// "all the same head image" sprites resolves to one D3D11 texture.
// Content-equivalent QImages with different cacheKeys (e.g. two
// independently-loaded copies of the same file) currently produce
// separate textures — Phase 3.4+ may add a content-fingerprint dedup
// layer if profiling shows it matters.
//
// Lifetime: cache holds GPU memory for every image it has ever seen.
// Reasonable for the preview's bounded asset set (~50-200 unique images
// per chart). If memory becomes a concern, Phase 3.4+ adds an LRU
// eviction policy keyed on per-frame use.

#include <QHash>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <wrl/client.h>
#endif

class QImage;

namespace miacode::preview::dcomp {

class PreviewDCompTextureCache
{
public:
    PreviewDCompTextureCache();
    ~PreviewDCompTextureCache();

    PreviewDCompTextureCache(const PreviewDCompTextureCache&) = delete;
    PreviewDCompTextureCache& operator=(const PreviewDCompTextureCache&) = delete;

#ifdef Q_OS_WIN
    // Look up or create the ID3D11ShaderResourceView for `image`.
    // Returns nullptr if image is null, the QImage has zero pixel size,
    // or D3D11 texture creation fails. Caller must use `device` only
    // from the render thread (D3D11Device::Create* are thread-safe but
    // we rely on the cache map being single-threaded).
    ID3D11ShaderResourceView* lookupOrCreate(const QImage* image,
                                              ID3D11Device* device);

    // Drop all cached textures. Call from the render thread before the
    // associated D3D11 device is released.
    void clear();

    int size() const { return cache_.size(); }
#else
    void* lookupOrCreate(const QImage*, void*) { return nullptr; }
    void clear() {}
    int size() const { return 0; }
#endif

private:
#ifdef Q_OS_WIN
    QHash<qint64, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> cache_;
#endif
};

}  // namespace miacode::preview::dcomp
