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
    // Look up or create the ID3D11ShaderResourceView for `image`. When
    // `cacheable` is true the SRV is retained in the persistent cache
    // keyed on QImage::cacheKey(); when false it is held only for the
    // current frame and freed on the next endFrame() call. The legacy
    // QSG path (PreviewTextureRepository) uses the same split because
    // sprite layers re-decode QImages per frame for animated content,
    // each producing a distinct cacheKey — without per-frame eviction
    // the GPU runs out of memory in seconds and the driver removes the
    // device (DXGI_ERROR_DEVICE_REMOVED).
    //
    // Returns nullptr if image is null, the QImage has zero pixel size,
    // or D3D11 texture creation fails. Caller must use `device` only
    // from the render thread (D3D11Device::Create* are thread-safe but
    // the cache maps themselves are single-threaded).
    ID3D11ShaderResourceView* lookupOrCreate(const QImage* image,
                                              ID3D11Device* device,
                                              bool cacheable);

    // Free the transient SRVs created with cacheable=false during the
    // frame just rendered. Call from the render thread once per frame,
    // after all draw calls referencing those SRVs have been issued.
    void endFrame();

    // Drop all cached textures (cacheable + transient). Call from the
    // render thread before the associated D3D11 device is released.
    void clear();

    int cacheableSize() const { return cacheable_.size(); }
    int transientSize() const { return transient_.size(); }
#else
    void* lookupOrCreate(const QImage*, void*, bool) { return nullptr; }
    void endFrame() {}
    void clear() {}
    int cacheableSize() const { return 0; }
    int transientSize() const { return 0; }
#endif

private:
#ifdef Q_OS_WIN
    // Phase 3.6 — entry cap mirroring PreviewTextureRepository's 96
    // limit. When exceeded, the cacheable compartment is fully
    // flushed (the just-inserted entry survives so the current
    // lookup stays valid).
    static constexpr int kCacheableEntryCap = 96;

    // Storage holds raw SRV pointers with manual AddRef/Release. We
    // *cannot* store Microsoft::WRL::ComPtr directly here: QHash's
    // rehash on growth relocates entries, and the resulting move
    // sequence interacts badly with ComPtr's move constructor — under
    // observation, lookups after a rehash returned a hash entry whose
    // ComPtr had ptr_==nullptr (logged as `hit_null_inner`). Manual
    // AddRef in lookupOrCreate, Release in endFrame()/clear() avoids
    // that whole class of issue: QHash only relocates pointer-sized
    // PODs, which it handles correctly.
    QHash<qint64, ID3D11ShaderResourceView*> cacheable_;
    QHash<qint64, ID3D11ShaderResourceView*> transient_;
#endif
};

}  // namespace miacode::preview::dcomp
