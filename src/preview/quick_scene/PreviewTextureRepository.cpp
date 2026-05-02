#include "preview/quick_scene/PreviewTextureRepository.h"

#include <QHashFunctions>
#include <QQuickWindow>
#include <QSGTexture>

namespace {

constexpr qsizetype kPreviewCachedTextureEntryLimit = 96;
constexpr qint64 kPreviewCachedTextureByteLimit = 96LL * 1024 * 1024;
// Safety cap on the L1 cacheKey -> fingerprint mapping. Each entry is only ~24 bytes, but
// in long-running sessions where many short-lived QImage instances dedup to a small set of
// fingerprints we still want a bound to keep lookup costs predictable.
constexpr qsizetype kPreviewCachedFastKeyEntryLimit = 8192;

// Content-fingerprint key for the L2 texture cache. Two QImage instances with different
// cacheKey()s but identical pixel content (e.g. when sprite atlases re-decode the same PNG
// into a fresh QImage every frame) collapse to the same fingerprint and therefore share a
// single GPU texture. The fingerprint mixes the raw pixel bytes with width/height/format
// so resized or reformatted variants stay distinct.
quint64 computeImageContentFingerprint(const QImage& image)
{
    if (image.isNull()) {
        return 0;
    }
    const qsizetype byteCount = image.sizeInBytes();
    const uchar* bytes = image.constBits();
    quint64 hash = 0;
    if (bytes != nullptr && byteCount > 0) {
        hash = static_cast<quint64>(qHashBits(bytes, static_cast<size_t>(byteCount), /*seed=*/0));
    }
    // Mix in metadata so two images with coincidentally identical byte counts but different
    // dimensions / pixel format don't alias each other.
    hash ^= static_cast<quint64>(image.width()) * 0x9E3779B185EBCA87ULL;
    hash ^= static_cast<quint64>(image.height()) * 0xC2B2AE3D27D4EB4FULL;
    hash ^= static_cast<quint64>(image.format()) * 0x165667B19E3779F9ULL;
    if (hash == 0) {
        // Reserve 0 as a "no fingerprint" sentinel.
        hash = 1;
    }
    return hash;
}

PreviewTextureLayerStats& ensureLayerStats(
    QVector<PreviewTextureLayerStats>* layerStats,
    const char* layerName
)
{
    Q_ASSERT(layerStats != nullptr);

    const QString name = QString::fromLatin1(layerName != nullptr ? layerName : "");
    for (PreviewTextureLayerStats& layerStat : *layerStats) {
        if (layerStat.name == name) {
            return layerStat;
        }
    }

    layerStats->append(PreviewTextureLayerStats{name});
    return layerStats->last();
}

}  // namespace

PreviewTextureRepository::~PreviewTextureRepository()
{
    clear();
}

void PreviewTextureRepository::setWindow(QQuickWindow* window)
{
    if (window_ == window) {
        return;
    }
    clear();
    window_ = window;
}

void PreviewTextureRepository::beginFrame()
{
    if (cachedTextureFlushPending_
        || cachedTextures_.size() > kPreviewCachedTextureEntryLimit
        || cachedTextureBytes_ > kPreviewCachedTextureByteLimit
        || cachedKeyToFingerprint_.size() > kPreviewCachedFastKeyEntryLimit) {
        clearCachedTextures();
    }
    qDeleteAll(transientTextures_);
    transientTextures_.clear();
    stats_ = PreviewTextureStats();
}

QSGTexture* PreviewTextureRepository::textureForImage(const QImage& image, bool cacheable)
{
    if (window_ == nullptr || image.isNull()) {
        return nullptr;
    }

    const quint64 fastKey = static_cast<quint64>(image.cacheKey());
    if (cacheable) {
        // L1: same QImage instance we've already seen this run -- skip the fingerprint hash.
        if (auto fpIt = cachedKeyToFingerprint_.constFind(fastKey);
            fpIt != cachedKeyToFingerprint_.constEnd()) {
            if (QSGTexture* existing = cachedTextures_.value(*fpIt, nullptr); existing != nullptr) {
                stats_.cachedHitCount += 1;
                return existing;
            }
            // Stale L1 entry pointing at a fingerprint that has already been evicted; drop it
            // and fall through to recompute the fingerprint and possibly re-create the texture.
            cachedKeyToFingerprint_.erase(fpIt);
        }

        // L2: content fingerprint -- collapses different QImage instances that hold identical
        // pixel content (the typical sprite-atlas case where a fresh QImage is decoded each
        // frame from the same source).
        const quint64 fingerprint = computeImageContentFingerprint(image);
        if (QSGTexture* existing = cachedTextures_.value(fingerprint, nullptr); existing != nullptr) {
            cachedKeyToFingerprint_.insert(fastKey, fingerprint);
            stats_.cachedHitCount += 1;
            return existing;
        }

        // beta20-fix2 — REVERTED to no-mip for parity with the
        // DComp-side revert (Intel iGPU driver crash on runtime mip
        // generation). Qt's createTextureFromImage uses the platform
        // RHI backend (D3D11 on Windows by default), so requesting
        // mips here would route through the same driver path that
        // crashed igd10um64xe.DLL on the DComp side. Pre-generated
        // asset mips are the planned follow-up.
        QSGTexture* texture = window_->createTextureFromImage(image);
        cachedTextures_.insert(fingerprint, texture);
        cachedKeyToFingerprint_.insert(fastKey, fingerprint);
        const qint64 imageBytes = qMax<qint64>(1, image.sizeInBytes());
        cachedTextureBytesByKey_.insert(fingerprint, imageBytes);
        cachedTextureBytes_ += imageBytes;
        if (cachedTextures_.size() > kPreviewCachedTextureEntryLimit
            || cachedTextureBytes_ > kPreviewCachedTextureByteLimit) {
            cachedTextureFlushPending_ = true;
        }
        stats_.cachedCreateCount += 1;
        return texture;
    }
    if (QSGTexture* existing = transientTextures_.value(fastKey, nullptr); existing != nullptr) {
        stats_.transientHitCount += 1;
        return existing;
    }
    QSGTexture* texture = createOwnedTexture(image);
    transientTextures_.insert(fastKey, texture);
    stats_.transientCreateCount += 1;
    return texture;
}

QSGTexture* PreviewTextureRepository::retainedTexture(const QString& slotName) const
{
    const auto it = retainedTextures_.constFind(slotName);
    if (it == retainedTextures_.constEnd()) {
        return nullptr;
    }
    return it->texture;
}

QSGTexture* PreviewTextureRepository::retainedTextureForImage(const QString& slotName, const QImage& image)
{
    if (window_ == nullptr || image.isNull() || slotName.isEmpty()) {
        return nullptr;
    }

    const quint64 key = static_cast<quint64>(image.cacheKey());
    PreviewRetainedTextureEntry& entry = retainedTextures_[slotName];
    if (entry.texture != nullptr && entry.key == key) {
        return entry.texture;
    }

    delete entry.texture;
    entry.texture = createOwnedTexture(image);
    entry.key = key;
    return entry.texture;
}

void PreviewTextureRepository::releaseRetainedTexture(const QString& slotName)
{
    auto it = retainedTextures_.find(slotName);
    if (it == retainedTextures_.end()) {
        return;
    }

    delete it->texture;
    retainedTextures_.erase(it);
}

QSGTexture* PreviewTextureRepository::createOwnedTexture(const QImage& image) const
{
    if (window_ == nullptr || image.isNull()) {
        return nullptr;
    }
    // beta20-fix2 — no mip request, see cached path comment above.
    return window_->createTextureFromImage(image);
}

void PreviewTextureRepository::noteSpriteBatchStats(
    const char* layerName,
    const PreviewSpriteBatchFrameProfile& profile
)
{
    stats_.spriteCount += profile.spriteCount;
    stats_.spriteBatchCount += profile.batchCount;
    if (layerName != nullptr && *layerName != '\0') {
        PreviewTextureLayerStats& layerStat = ensureLayerStats(&stats_.layerStats, layerName);
        layerStat.spriteCount += profile.spriteCount;
        layerStat.spriteBatchCount += profile.batchCount;
        layerStat.spriteRunCount += profile.runCount;
        layerStat.spriteGeometryReallocCount += profile.geometryReallocCount;
        layerStat.spriteVerticesRequired += profile.verticesRequired;
        layerStat.spriteVerticesCapacity += profile.verticesCapacity;
        layerStat.spriteScratchReserveGrowCount += profile.scratchReserveGrowCount;
    }
}

void PreviewTextureRepository::setStageBackgroundFrameProfile(const PreviewStageBackgroundFrameProfile& profile)
{
    stats_.stageBackground = profile;
}

PreviewTextureStats PreviewTextureRepository::stats() const
{
    return stats_;
}

void PreviewTextureRepository::clearCachedTextures()
{
    qDeleteAll(cachedTextures_);
    cachedTextures_.clear();
    cachedKeyToFingerprint_.clear();
    cachedTextureBytesByKey_.clear();
    cachedTextureBytes_ = 0;
    cachedTextureFlushPending_ = false;
}

void PreviewTextureRepository::clear()
{
    clearCachedTextures();
    qDeleteAll(transientTextures_);
    transientTextures_.clear();
    for (auto it = retainedTextures_.begin(); it != retainedTextures_.end(); ++it) {
        delete it->texture;
    }
    retainedTextures_.clear();
    stats_ = PreviewTextureStats();
}
