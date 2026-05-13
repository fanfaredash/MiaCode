#include "preview/ipc/PreviewFrameStateProjector.h"

#include "common/DebugLog.h"
#include "timeline/TimelineData.h"

#include <QByteArray>
#include <QPointF>
#include <QString>
#include <QVector>

#include <cstring>

namespace miacode::preview::ipc {

namespace {

// Append `text` (UTF-8) into `out.stringBlob` starting at `out.stringBlobUsedBytes`.
// Returns the SerialStringRef pointing at the appended bytes. On overflow,
// returns an empty ref (length=0) and increments the dropped-bytes counter
// in the runtime log.
SerialStringRef appendToStringBlob(PreviewFrameStateSerial& out, const QString& text)
{
    SerialStringRef ref;
    if (text.isEmpty()) {
        return ref;
    }
    const QByteArray utf8 = text.toUtf8();
    const quint32 needed = static_cast<quint32>(utf8.size());
    if (out.stringBlobUsedBytes + needed > static_cast<quint32>(kSerialStringBlobBytes)) {
        // Out of room. Log once per projection rather than per overflow so
        // a chart with hundreds of long strings doesn't spam the log.
        return ref;
    }
    ref.offset = out.stringBlobUsedBytes;
    ref.length = needed;
    std::memcpy(out.stringBlob.data() + out.stringBlobUsedBytes, utf8.constData(), needed);
    out.stringBlobUsedBytes += needed;
    return ref;
}

quint32 packMarkerFlags(const TimelineNoteMarker& marker)
{
    quint32 flags = 0;
    if (marker.isEach)              flags |= SerialSpriteFlags::kIsEach;
    if (marker.isBreak)             flags |= SerialSpriteFlags::kIsBreak;
    if (marker.isEx)                flags |= SerialSpriteFlags::kIsEx;
    if (marker.isFirework)          flags |= SerialSpriteFlags::kIsFirework;
    if (marker.onSlide)             flags |= SerialSpriteFlags::kOnSlide;
    if (marker.slideHead)           flags |= SerialSpriteFlags::kSlideHead;
    if (marker.sameHeadSlide)       flags |= SerialSpriteFlags::kSameHeadSlide;
    if (marker.beforeSlide)         flags |= SerialSpriteFlags::kBeforeSlide;
    if (marker.afterSlide)          flags |= SerialSpriteFlags::kAfterSlide;
    if (marker.headEach)            flags |= SerialSpriteFlags::kHeadEach;
    if (marker.headBreak)           flags |= SerialSpriteFlags::kHeadBreak;
    if (marker.headEx)              flags |= SerialSpriteFlags::kHeadEx;
    if (marker.trackBreak)          flags |= SerialSpriteFlags::kTrackBreak;
    if (marker.hasHeadStar)         flags |= SerialSpriteFlags::kHasHeadStar;
    if (marker.headlessImmediate)   flags |= SerialSpriteFlags::kHeadlessImmediate;
    if (marker.tapUsesStarMaterial) flags |= SerialSpriteFlags::kTapUsesStarMaterial;
    if (marker.tapStarDouble)       flags |= SerialSpriteFlags::kTapStarDouble;
    if (marker.tailOnSlideHead)     flags |= SerialSpriteFlags::kTailOnSlideHead;
    if (marker.slideEach)           flags |= SerialSpriteFlags::kSlideEach;
    if (marker.slideHeadUsesTapMaterial) flags |= SerialSpriteFlags::kSlideHeadUsesTapMaterial;
    return flags;
}

}  // namespace

int projectAssetPathsToSerial(const QString& skinDirectory,
                              PreviewFrameStateSerial& out)
{
    const quint32 before = out.stringBlobUsedBytes;
    out.skinDirectory = appendToStringBlob(out, skinDirectory);
    return static_cast<int>(out.stringBlobUsedBytes - before);
}

int projectMediaPathsToSerial(const QString& mediaImagePath,
                              const QString& mediaVideoPath,
                              int mediaKind,
                              bool mediaVisible,
                              quint64 mediaSerial,
                              PreviewFrameStateSerial& out)
{
    const quint32 before = out.stringBlobUsedBytes;
    out.mediaImagePath = appendToStringBlob(out, mediaImagePath);
    out.mediaVideoPath = appendToStringBlob(out, mediaVideoPath);
    out.mediaKind = static_cast<quint32>(mediaKind);
    out.mediaVisible = mediaVisible ? 1u : 0u;
    out.mediaSerial = mediaSerial;
    return static_cast<int>(out.stringBlobUsedBytes - before);
}


namespace {

QString readBlobString(const PreviewFrameStateSerial& snapshot, const SerialStringRef& ref)
{
    if (ref.length == 0) {
        return QString();
    }
    if (ref.offset + ref.length > static_cast<quint32>(snapshot.stringBlob.size())) {
        return QString();
    }
    return QString::fromUtf8(snapshot.stringBlob.data() + ref.offset,
                             static_cast<int>(ref.length));
}

QString markerTypeStringFromKind(SerialSpriteTypeKind kind, const QString& fallback)
{
    switch (kind) {
    case SerialSpriteTypeKind::Tap:       return QStringLiteral("tap");
    case SerialSpriteTypeKind::Hold:      return QStringLiteral("hold");
    case SerialSpriteTypeKind::Slide:     return QStringLiteral("slide");
    case SerialSpriteTypeKind::Wifi:      return QStringLiteral("wifi");
    case SerialSpriteTypeKind::Touch:     return QStringLiteral("touch");
    case SerialSpriteTypeKind::TouchHold: return QStringLiteral("touch_hold");
    case SerialSpriteTypeKind::Unknown:
    default:
        return fallback;
    }
}

// ---- Slide geometry blob packer ----
//
// Layout (little-endian, no padding):
//
//   SlideGeometryPayload {
//     u32 magic = 0x53434C49;            // 'SCLI' — sanity check on read
//     u32 segmentPointsOuter;            // number of slide segments
//     [segmentPointsOuter × {
//       u32 innerCount;                  // points per segment
//       [innerCount × { f32 x, f32 y }]
//     }]
//     u32 segmentAnglesOuter;
//     [segmentAnglesOuter × {
//       u32 innerCount;
//       [innerCount × f32 angle]
//     }]
//     u32 segmentDurationCount;
//     [segmentDurationCount × f32 duration]
//     u32 segmentShootCount;
//     [segmentShootCount × f32 shootSecond]
//     u32 segmentCriticalCount;
//     [segmentCriticalCount × f32 criticalProportion]
//     u32 trackAreaOuter;                // number of slide segments (== segmentPointsOuter typically)
//     [trackAreaOuter × {
//       u32 middleCount;                 // number of polygons per segment
//       [middleCount × {
//         u32 innerCount;                // vertices per polygon
//         [innerCount × { f32 x, f32 y }]
//       }]
//     }]
//   }
//
// QPointF (qreal=double) coordinates are downcast to float — the worker
// renders at logical-pixel precision and the visible slide curves don't
// exceed float's ~24-bit mantissa given practical chart sizes.

inline constexpr quint32 kSlideGeometryMagic = 0x53434C49u;  // 'SCLI'
inline constexpr quint32 kMuriReportMagic    = 0x4D555249u;  // 'MURI'

bool appendBytes(PreviewFrameStateSerial& out, const void* data, size_t bytes)
{
    if (out.markerGeometryBlobUsedBytes + bytes >
        static_cast<quint32>(out.markerGeometryBlob.size())) {
        return false;
    }
    std::memcpy(out.markerGeometryBlob.data() + out.markerGeometryBlobUsedBytes,
                data, bytes);
    out.markerGeometryBlobUsedBytes += static_cast<quint32>(bytes);
    return true;
}

bool appendU32(PreviewFrameStateSerial& out, quint32 v) { return appendBytes(out, &v, 4); }
bool appendF32(PreviewFrameStateSerial& out, float v)   { return appendBytes(out, &v, 4); }

bool appendPoints(PreviewFrameStateSerial& out, const QVector<QPointF>& pts)
{
    if (!appendU32(out, static_cast<quint32>(pts.size()))) return false;
    for (const QPointF& p : pts) {
        if (!appendF32(out, static_cast<float>(p.x()))) return false;
        if (!appendF32(out, static_cast<float>(p.y()))) return false;
    }
    return true;
}

bool appendDoubles(PreviewFrameStateSerial& out, const QVector<double>& vs)
{
    if (!appendU32(out, static_cast<quint32>(vs.size()))) return false;
    for (double v : vs) {
        if (!appendF32(out, static_cast<float>(v))) return false;
    }
    return true;
}

// Pack a marker's slide geometry into the blob. Returns the blob ref
// (offset / length in bytes) for the caller to embed in `SerialSpriteEntry`.
// Returns empty ref if the marker has no slide geometry, or if the blob is
// out of room.
SerialBlobRef packSlideGeometry(const TimelineNoteMarker& marker,
                                 PreviewFrameStateSerial& out)
{
    SerialBlobRef ref;
    if (marker.slideSegmentPoints.isEmpty()
        && marker.slideTrackAreaPoints.isEmpty()
        && marker.wifiLanePoints.isEmpty()
        && marker.wifiTrackAreaPoints.isEmpty()) {
        return ref;  // nothing to project
    }

    const quint32 startOffset = out.markerGeometryBlobUsedBytes;

    if (!appendU32(out, kSlideGeometryMagic)) return SerialBlobRef{};

    // segmentPoints: QVector<QVector<QPointF>>
    if (!appendU32(out, static_cast<quint32>(marker.slideSegmentPoints.size())))
        return SerialBlobRef{};
    for (const QVector<QPointF>& seg : marker.slideSegmentPoints) {
        if (!appendPoints(out, seg)) return SerialBlobRef{};
    }

    // segmentAngles: QVector<QVector<double>>
    if (!appendU32(out, static_cast<quint32>(marker.slideSegmentAngles.size())))
        return SerialBlobRef{};
    for (const QVector<double>& seg : marker.slideSegmentAngles) {
        if (!appendDoubles(out, seg)) return SerialBlobRef{};
    }

    // Flat scalar arrays
    if (!appendDoubles(out, marker.slideSegmentDurations)) return SerialBlobRef{};
    if (!appendDoubles(out, marker.slideSegmentShootSeconds)) return SerialBlobRef{};
    if (!appendDoubles(out, marker.slideSegmentCriticalProportions)) return SerialBlobRef{};

    // trackAreaPoints: QVector<QVector<QVector<QPointF>>> (3D nested)
    if (!appendU32(out, static_cast<quint32>(marker.slideTrackAreaPoints.size())))
        return SerialBlobRef{};
    for (const QVector<QVector<QPointF>>& mid : marker.slideTrackAreaPoints) {
        if (!appendU32(out, static_cast<quint32>(mid.size()))) return SerialBlobRef{};
        for (const QVector<QPointF>& inner : mid) {
            if (!appendPoints(out, inner)) return SerialBlobRef{};
        }
    }

    // Layout v4 — slide track-area metadata that controls per-area
    // sprite orientation + threshold/checkpoint gating. Without these
    // the track layer left rotations at 0 (sprites unrotated) and the
    // checkpoint logic ran against empty arrays — producing visibly
    // broken track-arc rendering even though slideTrackAreaPoints was
    // populated. All four arrays mirror the structure of trackAreaPoints
    // (3D nested) plus thresholds (2D nested per segment / area).
    //
    // trackAreaRotations: QVector<QVector<QVector<double>>> (3D nested,
    // mirrors trackAreaPoints — one rotation per point)
    if (!appendU32(out, static_cast<quint32>(marker.slideTrackAreaRotations.size())))
        return SerialBlobRef{};
    for (const QVector<QVector<double>>& mid : marker.slideTrackAreaRotations) {
        if (!appendU32(out, static_cast<quint32>(mid.size()))) return SerialBlobRef{};
        for (const QVector<double>& inner : mid) {
            if (!appendDoubles(out, inner)) return SerialBlobRef{};
        }
    }

    // trackAreaThresholds: QVector<QVector<double>> (per-segment)
    if (!appendU32(out, static_cast<quint32>(marker.slideTrackAreaThresholds.size())))
        return SerialBlobRef{};
    for (const QVector<double>& seg : marker.slideTrackAreaThresholds) {
        if (!appendDoubles(out, seg)) return SerialBlobRef{};
    }

    // trackAreaCheckpoints: QVector<QVector<QVector<double>>>
    if (!appendU32(out, static_cast<quint32>(marker.slideTrackAreaCheckpoints.size())))
        return SerialBlobRef{};
    for (const QVector<QVector<double>>& mid : marker.slideTrackAreaCheckpoints) {
        if (!appendU32(out, static_cast<quint32>(mid.size()))) return SerialBlobRef{};
        for (const QVector<double>& inner : mid) {
            if (!appendDoubles(out, inner)) return SerialBlobRef{};
        }
    }

    // trackAreaCutIndices: QVector<QVector<QVector<int>>>
    if (!appendU32(out, static_cast<quint32>(marker.slideTrackAreaCutIndices.size())))
        return SerialBlobRef{};
    for (const QVector<QVector<int>>& mid : marker.slideTrackAreaCutIndices) {
        if (!appendU32(out, static_cast<quint32>(mid.size()))) return SerialBlobRef{};
        for (const QVector<int>& inner : mid) {
            if (!appendU32(out, static_cast<quint32>(inner.size()))) return SerialBlobRef{};
            for (int v : inner) {
                if (!appendU32(out, static_cast<quint32>(v))) return SerialBlobRef{};
            }
        }
    }

    // Layout v5 — wifi-specific geometry. Wifi notes have a different
    // shape than slides: lane points/angles are 2D nested (one entry per
    // lane offset), track-area arrays are 2D (segment-less). Without
    // these, wifi notes hit the `wifiTrackAreaPoints.isEmpty()` gate at
    // PreviewTrackLayerState.cpp:502 and never render their tracks; the
    // SlideMotionLayer also gates on `wifiLanePoints.isEmpty()`.
    //
    // wifiLanePoints: QVector<QVector<QPointF>>
    if (!appendU32(out, static_cast<quint32>(marker.wifiLanePoints.size())))
        return SerialBlobRef{};
    for (const QVector<QPointF>& lane : marker.wifiLanePoints) {
        if (!appendPoints(out, lane)) return SerialBlobRef{};
    }
    // wifiLaneAngles: QVector<QVector<double>>
    if (!appendU32(out, static_cast<quint32>(marker.wifiLaneAngles.size())))
        return SerialBlobRef{};
    for (const QVector<double>& lane : marker.wifiLaneAngles) {
        if (!appendDoubles(out, lane)) return SerialBlobRef{};
    }
    // wifiTrackAreaPoints: QVector<QVector<QPointF>>
    if (!appendU32(out, static_cast<quint32>(marker.wifiTrackAreaPoints.size())))
        return SerialBlobRef{};
    for (const QVector<QPointF>& area : marker.wifiTrackAreaPoints) {
        if (!appendPoints(out, area)) return SerialBlobRef{};
    }
    // wifiTrackAreaRotations: QVector<QVector<double>>
    if (!appendU32(out, static_cast<quint32>(marker.wifiTrackAreaRotations.size())))
        return SerialBlobRef{};
    for (const QVector<double>& area : marker.wifiTrackAreaRotations) {
        if (!appendDoubles(out, area)) return SerialBlobRef{};
    }
    // wifiTrackAreaImageIndices: QVector<QVector<int>>
    if (!appendU32(out, static_cast<quint32>(marker.wifiTrackAreaImageIndices.size())))
        return SerialBlobRef{};
    for (const QVector<int>& area : marker.wifiTrackAreaImageIndices) {
        if (!appendU32(out, static_cast<quint32>(area.size()))) return SerialBlobRef{};
        for (int v : area) {
            if (!appendU32(out, static_cast<quint32>(v))) return SerialBlobRef{};
        }
    }
    // wifiTrackAreaThresholds: QVector<double>
    if (!appendDoubles(out, marker.wifiTrackAreaThresholds)) return SerialBlobRef{};
    // wifiTrackAreaCheckpoints: QVector<QVector<double>>
    if (!appendU32(out, static_cast<quint32>(marker.wifiTrackAreaCheckpoints.size())))
        return SerialBlobRef{};
    for (const QVector<double>& area : marker.wifiTrackAreaCheckpoints) {
        if (!appendDoubles(out, area)) return SerialBlobRef{};
    }

    ref.offset = startOffset;
    ref.length = out.markerGeometryBlobUsedBytes - startOffset;
    return ref;
}

// Reader cursor over the packed blob. Bounds-checked against `end`.
struct BlobCursor {
    const char* p;
    const char* end;
    bool ok() const { return p != nullptr && p <= end; }
    bool fail() { p = nullptr; return false; }
    bool readU32(quint32& out) {
        if (p == nullptr || end - p < 4) return fail();
        std::memcpy(&out, p, 4); p += 4; return true;
    }
    bool readF32(float& out) {
        if (p == nullptr || end - p < 4) return fail();
        std::memcpy(&out, p, 4); p += 4; return true;
    }
    bool readPoints(QVector<QPointF>& out) {
        quint32 n = 0; if (!readU32(n)) return false;
        out.resize(static_cast<int>(n));
        for (quint32 i = 0; i < n; ++i) {
            float x = 0, y = 0;
            if (!readF32(x) || !readF32(y)) return false;
            out[static_cast<int>(i)] = QPointF(x, y);
        }
        return true;
    }
    bool readDoubles(QVector<double>& out) {
        quint32 n = 0; if (!readU32(n)) return false;
        out.resize(static_cast<int>(n));
        for (quint32 i = 0; i < n; ++i) {
            float v = 0;
            if (!readF32(v)) return false;
            out[static_cast<int>(i)] = v;
        }
        return true;
    }
    bool readInts(QVector<int>& out) {
        quint32 n = 0; if (!readU32(n)) return false;
        out.resize(static_cast<int>(n));
        for (quint32 i = 0; i < n; ++i) {
            quint32 v = 0;
            if (!readU32(v)) return false;
            out[static_cast<int>(i)] = static_cast<int>(v);
        }
        return true;
    }
};

bool unpackSlideGeometry(const PreviewFrameStateSerial& snapshot,
                          const SerialBlobRef& ref,
                          TimelineNoteMarker* outMarker)
{
    if (outMarker == nullptr || ref.length == 0) return false;
    const quint32 cap = static_cast<quint32>(snapshot.markerGeometryBlob.size());
    if (ref.offset >= cap || ref.offset + ref.length > cap) return false;
    BlobCursor c{ snapshot.markerGeometryBlob.data() + ref.offset,
                   snapshot.markerGeometryBlob.data() + ref.offset + ref.length };

    quint32 magic = 0;
    if (!c.readU32(magic) || magic != kSlideGeometryMagic) return false;

    quint32 outerN = 0;
    if (!c.readU32(outerN)) return false;
    outMarker->slideSegmentPoints.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        if (!c.readPoints(outMarker->slideSegmentPoints[static_cast<int>(i)])) return false;
    }

    if (!c.readU32(outerN)) return false;
    outMarker->slideSegmentAngles.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        if (!c.readDoubles(outMarker->slideSegmentAngles[static_cast<int>(i)])) return false;
    }

    if (!c.readDoubles(outMarker->slideSegmentDurations)) return false;
    if (!c.readDoubles(outMarker->slideSegmentShootSeconds)) return false;
    if (!c.readDoubles(outMarker->slideSegmentCriticalProportions)) return false;

    if (!c.readU32(outerN)) return false;
    outMarker->slideTrackAreaPoints.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        quint32 mid = 0;
        if (!c.readU32(mid)) return false;
        outMarker->slideTrackAreaPoints[static_cast<int>(i)].resize(static_cast<int>(mid));
        for (quint32 j = 0; j < mid; ++j) {
            if (!c.readPoints(
                    outMarker->slideTrackAreaPoints[static_cast<int>(i)][static_cast<int>(j)])) {
                return false;
            }
        }
    }

    // Layout v4 — slide track-area metadata. Stop on partial-read so
    // older snapshots (no metadata) still produce valid markers with
    // empty rotation/threshold/checkpoint/cut-index arrays.
    if (!c.readU32(outerN)) return true;
    outMarker->slideTrackAreaRotations.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        quint32 mid = 0;
        if (!c.readU32(mid)) return false;
        outMarker->slideTrackAreaRotations[static_cast<int>(i)].resize(static_cast<int>(mid));
        for (quint32 j = 0; j < mid; ++j) {
            if (!c.readDoubles(
                    outMarker->slideTrackAreaRotations[static_cast<int>(i)][static_cast<int>(j)])) {
                return false;
            }
        }
    }

    if (!c.readU32(outerN)) return true;
    outMarker->slideTrackAreaThresholds.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        if (!c.readDoubles(outMarker->slideTrackAreaThresholds[static_cast<int>(i)])) return false;
    }

    if (!c.readU32(outerN)) return true;
    outMarker->slideTrackAreaCheckpoints.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        quint32 mid = 0;
        if (!c.readU32(mid)) return false;
        outMarker->slideTrackAreaCheckpoints[static_cast<int>(i)].resize(static_cast<int>(mid));
        for (quint32 j = 0; j < mid; ++j) {
            if (!c.readDoubles(
                    outMarker->slideTrackAreaCheckpoints[static_cast<int>(i)][static_cast<int>(j)])) {
                return false;
            }
        }
    }

    if (!c.readU32(outerN)) return true;
    outMarker->slideTrackAreaCutIndices.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        quint32 mid = 0;
        if (!c.readU32(mid)) return false;
        outMarker->slideTrackAreaCutIndices[static_cast<int>(i)].resize(static_cast<int>(mid));
        for (quint32 j = 0; j < mid; ++j) {
            if (!c.readInts(
                    outMarker->slideTrackAreaCutIndices[static_cast<int>(i)][static_cast<int>(j)])) {
                return false;
            }
        }
    }

    // Layout v5 — wifi geometry. Stop on partial-read so older snapshots
    // still produce valid markers.
    if (!c.readU32(outerN)) return true;
    outMarker->wifiLanePoints.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        if (!c.readPoints(outMarker->wifiLanePoints[static_cast<int>(i)])) return false;
    }
    if (!c.readU32(outerN)) return true;
    outMarker->wifiLaneAngles.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        if (!c.readDoubles(outMarker->wifiLaneAngles[static_cast<int>(i)])) return false;
    }
    if (!c.readU32(outerN)) return true;
    outMarker->wifiTrackAreaPoints.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        if (!c.readPoints(outMarker->wifiTrackAreaPoints[static_cast<int>(i)])) return false;
    }
    if (!c.readU32(outerN)) return true;
    outMarker->wifiTrackAreaRotations.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        if (!c.readDoubles(outMarker->wifiTrackAreaRotations[static_cast<int>(i)])) return false;
    }
    if (!c.readU32(outerN)) return true;
    outMarker->wifiTrackAreaImageIndices.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        if (!c.readInts(outMarker->wifiTrackAreaImageIndices[static_cast<int>(i)])) return false;
    }
    if (!c.readDoubles(outMarker->wifiTrackAreaThresholds)) return true;
    if (!c.readU32(outerN)) return true;
    outMarker->wifiTrackAreaCheckpoints.resize(static_cast<int>(outerN));
    for (quint32 i = 0; i < outerN; ++i) {
        if (!c.readDoubles(outMarker->wifiTrackAreaCheckpoints[static_cast<int>(i)])) return false;
    }
    return true;
}

}  // namespace

TimelineNoteMarker inflateSerialSpriteToMarker(const SerialSpriteEntry& entry,
                                                const PreviewFrameStateSerial& snapshot)
{
    TimelineNoteMarker m;
    m.second = entry.startSeconds;
    m.endSecond = entry.endSeconds;
    const QString typeFallback = readBlobString(snapshot, entry.typeText);
    m.type = markerTypeStringFromKind(entry.typeKind, typeFallback);
    m.lane = entry.lane;
    m.endLane = entry.endLane;
    m.slideTrackKey = readBlobString(snapshot, entry.slideTrackKey);
    m.slideDisplayKey = readBlobString(snapshot, entry.slideDisplayKey);
    m.touchPad = readBlobString(snapshot, entry.touchPad);
    // Layout v8 — restore slideSegmentKeys from the '\n'-joined blob
    // string so PreviewChartReviewLayerState can pick the LAST
    // segment for slide-judge sprite selection. Empty string → empty
    // list, falling back to slideTrackKey (for v7-and-earlier
    // snapshots, layoutVersion mismatch is checked separately so we
    // shouldn't see those at runtime, but split-on-empty is safe).
    const QString slideSegmentKeysJoined = readBlobString(snapshot, entry.slideSegmentKeysJoined);
    m.slideSegmentKeys = slideSegmentKeysJoined.isEmpty()
        ? QStringList()
        : slideSegmentKeysJoined.split(QChar('\n'), Qt::SkipEmptyParts);
    m.touchPoint = QPointF(entry.touchPointX, entry.touchPointY);
    m.parseOrder = entry.parseOrder;
    m.eachGroupId = entry.eachGroupId;
    m.sourceLine = entry.sourceLine;
    m.sourceCol = entry.sourceCol;
    m.wifiCriticalProportion = entry.wifiCriticalProportion;
    m.slideNativeTrackLength = entry.slideNativeTrackLength;
    m.slideRuntimeTrackLength = entry.slideRuntimeTrackLength;
    m.hsMultiplier = entry.hsMultiplier;

    // Flag bitmap → boolean fields. Mirror image of `packMarkerFlags` in
    // the projector path.
    const quint32 flags = entry.flagsBitmap;
    m.isEach              = (flags & SerialSpriteFlags::kIsEach) != 0;
    m.isBreak             = (flags & SerialSpriteFlags::kIsBreak) != 0;
    m.isEx                = (flags & SerialSpriteFlags::kIsEx) != 0;
    m.isFirework          = (flags & SerialSpriteFlags::kIsFirework) != 0;
    m.onSlide             = (flags & SerialSpriteFlags::kOnSlide) != 0;
    m.slideHead           = (flags & SerialSpriteFlags::kSlideHead) != 0;
    m.sameHeadSlide       = (flags & SerialSpriteFlags::kSameHeadSlide) != 0;
    m.beforeSlide         = (flags & SerialSpriteFlags::kBeforeSlide) != 0;
    m.afterSlide          = (flags & SerialSpriteFlags::kAfterSlide) != 0;
    m.headEach            = (flags & SerialSpriteFlags::kHeadEach) != 0;
    m.headBreak           = (flags & SerialSpriteFlags::kHeadBreak) != 0;
    m.headEx              = (flags & SerialSpriteFlags::kHeadEx) != 0;
    m.trackBreak          = (flags & SerialSpriteFlags::kTrackBreak) != 0;
    m.hasHeadStar         = (flags & SerialSpriteFlags::kHasHeadStar) != 0;
    m.headlessImmediate   = (flags & SerialSpriteFlags::kHeadlessImmediate) != 0;
    m.tapUsesStarMaterial = (flags & SerialSpriteFlags::kTapUsesStarMaterial) != 0;
    m.tapStarDouble       = (flags & SerialSpriteFlags::kTapStarDouble) != 0;
    m.tailOnSlideHead     = (flags & SerialSpriteFlags::kTailOnSlideHead) != 0;
    m.slideEach           = (flags & SerialSpriteFlags::kSlideEach) != 0;
    m.slideHeadUsesTapMaterial =
        (flags & SerialSpriteFlags::kSlideHeadUsesTapMaterial) != 0;

    // Slide visibility scalars. Default -1.0 in TimelineNoteMarker —
    // without these the track layer skips the slide via `availableSecond
    // < 0` and the past-trace gate.
    m.slideTraceSecond = entry.slideTraceSecond;
    m.availableSecond = entry.availableSecond;

    // Slide geometry — packed in `markerGeometryBlob` for slide-like
    // markers; empty otherwise. Failure to unpack leaves the slide
    // segment fields default (head-only render fallback).
    if (entry.slideGeometry.length > 0) {
        unpackSlideGeometry(snapshot, entry.slideGeometry, &m);
    }

    return m;
}

int inflateActiveSpritesToMarkers(const PreviewFrameStateSerial& snapshot,
                                   QVector<TimelineNoteMarker>* outMarkers)
{
    if (outMarkers == nullptr) {
        return 0;
    }
    const quint32 limit = qMin(snapshot.spriteCount,
                               static_cast<quint32>(kMaxSerializedSpriteCount));
    outMarkers->clear();
    outMarkers->reserve(static_cast<int>(limit));
    for (quint32 i = 0; i < limit; ++i) {
        outMarkers->append(inflateSerialSpriteToMarker(snapshot.sprites[i], snapshot));
    }
    return outMarkers->size();
}

SerialSpriteTypeKind classifyMarkerType(const QString& typeText)
{
    // Order by frequency in typical charts: tap > slide > hold > touch > others.
    if (typeText == QLatin1String("tap"))        return SerialSpriteTypeKind::Tap;
    if (typeText == QLatin1String("slide"))      return SerialSpriteTypeKind::Slide;
    if (typeText == QLatin1String("hold"))       return SerialSpriteTypeKind::Hold;
    if (typeText == QLatin1String("touch"))      return SerialSpriteTypeKind::Touch;
    if (typeText == QLatin1String("touch_hold")) return SerialSpriteTypeKind::TouchHold;
    if (typeText == QLatin1String("wifi"))       return SerialSpriteTypeKind::Wifi;
    return SerialSpriteTypeKind::Unknown;
}

void projectScalarsToSerial(const miacode::preview::scene::PreviewFrameState& state,
                            PreviewFrameStateSerial& out)
{
    out.playheadSeconds = state.playheadSeconds;
    out.fpsDisplay = state.fpsDisplay;
    out.tickFpsDisplay = state.tickFpsDisplay;
    out.updateRequestFpsDisplay = state.updateRequestFpsDisplay;
    out.presentMaxMsDisplay = state.presentMaxMsDisplay;
    out.tickMaxMsDisplay = state.tickMaxMsDisplay;
    out.updateRequestMaxMsDisplay = state.updateRequestMaxMsDisplay;
    out.presentStutterCountDisplay = static_cast<qint32>(state.presentStutterCountDisplay);
    out.tickStutterCountDisplay = static_cast<qint32>(state.tickStutterCountDisplay);
    out.updateRequestStutterCountDisplay = static_cast<qint32>(state.updateRequestStutterCountDisplay);
    out.tickCount = state.tickCount;
    out.updateRequestCount = state.updateRequestCount;
    out.presentedFrameCount = state.presentedFrameCount;
    out.framePacingTargetFps = state.framePacingTargetFps;
    out.displayRefreshRate = state.displayRefreshRate;
    out.framePacingUsesDisplayRefresh = state.framePacingUsesDisplayRefresh ? 1u : 0u;

    // muriRenderOptions — gates Muri / chart-review layers + feeds the
    // prepared-cache key.
    const auto& muri = state.muriRenderOptions;
    out.muriRenderModeKind = static_cast<quint32>(muri.renderMode);
    quint32 muriBits = 0;
    if (muri.showSlideTracks)                  muriBits |= MuriRenderFlags::kShowSlideTracks;
    if (muri.showJudgeMarkers)                 muriBits |= MuriRenderFlags::kShowJudgeMarkers;
    if (muri.showTouchTrail)                   muriBits |= MuriRenderFlags::kShowTouchTrail;
    if (muri.showChartReviewSlideJudgeOverlay) muriBits |= MuriRenderFlags::kShowChartReviewSlideJudgeOverlay;
    if (muri.showChartReviewTapJudgeOverlay)   muriBits |= MuriRenderFlags::kShowChartReviewTapJudgeOverlay;
    if (muri.showChartReviewTouchJudgeOverlay) muriBits |= MuriRenderFlags::kShowChartReviewTouchJudgeOverlay;
    if (muri.wifiNeedC)                        muriBits |= MuriRenderFlags::kWifiNeedC;
    if (muri.excludeTouchFromMultiTouch)       muriBits |= MuriRenderFlags::kExcludeTouchFromMultiTouch;
    out.muriRenderFlagsBitmap = muriBits;

    // PreviewRenderState — drives sprite scroll cadence (flow speeds),
    // backdrop scale, dim shader brightness, HUD content toggles.
    const auto& render = state.render;
    out.tapFlowSpeed = render.tapFlowSpeed;
    out.touchFlowSpeed = render.touchFlowSpeed;
    out.backgroundBrightnessOuter = render.backgroundBrightnessOuter;
    out.backgroundBrightnessInner = render.backgroundBrightnessInner;
    out.layoutSquareScale = render.layoutSquareScale;
    out.backgroundScaleModeKind = static_cast<quint32>(render.backgroundScaleMode);
    quint32 renderBits = 0;
    if (render.smoothBrightness)               renderBits |= RenderFlags::kSmoothBrightness;
    if (render.slideEarlierSecondAndTextOnTop) renderBits |= RenderFlags::kSlideEarlierSecondAndTextOnTop;
    if (render.showDebugInfo)                  renderBits |= RenderFlags::kShowDebugInfo;
    if (render.showTimestamp)                  renderBits |= RenderFlags::kShowTimestamp;
    if (render.showObjectStatsHud)             renderBits |= RenderFlags::kShowObjectStatsHud;
    out.renderFlagsBitmap = renderBits;
}

int projectActiveSpritesToSerial(const miacode::preview::scene::PreviewFrameState& state,
                                  PreviewFrameStateSerial& out)
{
    const double playhead = state.playheadSeconds;
    const double windowStart = playhead - kSpriteProjectionLookbackSeconds;
    const double windowEnd = playhead + kSpriteProjectionLookaheadSeconds;

    out.spriteCount = 0;
    out.stringBlobUsedBytes = 0;
    int considered = 0;
    int droppedSpriteCap = 0;
    int droppedBlobCap = 0;

    for (const TimelineNoteMarker& marker : state.noteMarkers) {
        ++considered;
        const double markerStart = marker.second;
        // `endSecond < 0` is the sentinel for "no extent" (tap, touch).
        // Treat the marker as a point at `markerStart` for window overlap.
        const double markerEnd = (marker.endSecond > 0.0) ? marker.endSecond : markerStart;

        // Reject if entirely past or entirely future relative to the window.
        if (markerEnd < windowStart) continue;
        if (markerStart > windowEnd) continue;

        if (out.spriteCount >= static_cast<quint32>(kMaxSerializedSpriteCount)) {
            ++droppedSpriteCap;
            continue;
        }

        SerialSpriteEntry& dst = out.sprites[out.spriteCount];
        dst.startSeconds = markerStart;
        dst.endSeconds = marker.endSecond;
        dst.typeKind = classifyMarkerType(marker.type);
        dst.lane = marker.lane;
        dst.endLane = marker.endLane;
        dst.flagsBitmap = packMarkerFlags(marker);

        // Touch geometry + ordering metadata + slide scalars.
        dst.touchPointX = static_cast<float>(marker.touchPoint.x());
        dst.touchPointY = static_cast<float>(marker.touchPoint.y());
        dst.parseOrder = marker.parseOrder;
        dst.eachGroupId = marker.eachGroupId;
        dst.sourceLine = marker.sourceLine;
        dst.sourceCol = marker.sourceCol;
        dst.wifiCriticalProportion = static_cast<float>(marker.wifiCriticalProportion);
        dst.slideNativeTrackLength = static_cast<float>(marker.slideNativeTrackLength);
        dst.slideRuntimeTrackLength = static_cast<float>(marker.slideRuntimeTrackLength);
        dst.hsMultiplier = static_cast<float>(marker.hsMultiplier);
        // Slide visibility gates — both default -1.0; without projection
        // every slide is hidden by `availableSecond < 0` in the track layer.
        dst.slideTraceSecond = marker.slideTraceSecond;
        dst.availableSecond = marker.availableSecond;

        // Slide track key drives skin selection on the worker side. Empty
        // for non-slide-like markers; appendToStringBlob short-circuits.
        const quint32 blobBefore = out.stringBlobUsedBytes;
        dst.slideTrackKey = appendToStringBlob(out, marker.slideTrackKey);
        if (!marker.slideTrackKey.isEmpty() && dst.slideTrackKey.length == 0) {
            ++droppedBlobCap;
        }
        dst.slideDisplayKey = appendToStringBlob(out, marker.slideDisplayKey);
        if (!marker.slideDisplayKey.isEmpty() && dst.slideDisplayKey.length == 0) {
            ++droppedBlobCap;
        }
        dst.touchPad = appendToStringBlob(out, marker.touchPad);
        if (!marker.touchPad.isEmpty() && dst.touchPad.length == 0) {
            ++droppedBlobCap;
        }
        // Layout v8 — slide segment chain keys, '\n'-joined. Empty for
        // non-slide markers; for chain slides this carries the per-
        // segment keys so the worker's
        // PreviewChartReviewLayerState can reach
        // slideSegmentKeys.constLast() and pick the right
        // just_str / just_curv / just_wifi sprite. Pre-v8 snapshots
        // dropped this field, so the worker always selected just_str
        // (it fell back to slideTrackKey, which holds the FIRST
        // segment's key for chain slides).
        const QString slideSegmentKeysJoined =
            marker.slideSegmentKeys.isEmpty()
            ? QString()
            : marker.slideSegmentKeys.join(QChar('\n'));
        dst.slideSegmentKeysJoined = appendToStringBlob(out, slideSegmentKeysJoined);
        if (!slideSegmentKeysJoined.isEmpty() && dst.slideSegmentKeysJoined.length == 0) {
            ++droppedBlobCap;
        }

        // Raw type text fallback only when we couldn't classify.
        if (dst.typeKind == SerialSpriteTypeKind::Unknown && !marker.type.isEmpty()) {
            dst.typeText = appendToStringBlob(out, marker.type);
            if (dst.typeText.length == 0) {
                ++droppedBlobCap;
                // Roll back ALL string writes for this entry — partial
                // entries are confusing, drop the whole sprite entry.
                out.stringBlobUsedBytes = blobBefore;
                continue;
            }
        }

        // Slide / wifi geometry — only pack for slide-like markers; tap /
        // hold / touch never carry segment curves so the lookup short-
        // circuits. Failure (out of geometry blob) leaves the sprite
        // entry's slideGeometry empty; the worker falls back to head-only
        // slide rendering rather than dropping the whole entry.
        if (dst.typeKind == SerialSpriteTypeKind::Slide
            || dst.typeKind == SerialSpriteTypeKind::Wifi) {
            const quint32 geomBefore = out.markerGeometryBlobUsedBytes;
            dst.slideGeometry = packSlideGeometry(marker, out);
            if (dst.slideGeometry.length == 0
                && (!marker.slideSegmentPoints.isEmpty()
                    || !marker.slideTrackAreaPoints.isEmpty())) {
                // Pack truncated mid-write — roll back the geometry bytes
                // and continue without slide curves for this marker.
                out.markerGeometryBlobUsedBytes = geomBefore;
                ++droppedBlobCap;
            }
        }

        ++out.spriteCount;
    }

    // Log periodically (1/60 of frames) so the trace doesn't dominate the
    // log file but operators can still see what's being projected.
    static thread_local int s_projectLogCounter = 0;
    if (++s_projectLogCounter % 60 == 0
        || droppedSpriteCap > 0 || droppedBlobCap > 0) {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Runtime,
            QStringLiteral("preview/sprite_projection"),
            QStringLiteral("considered=%1 packed=%2 dropped_sprite_cap=%3 dropped_blob_cap=%4 blob_used=%5 geom_blob_used=%6 playhead=%7")
                .arg(considered)
                .arg(out.spriteCount)
                .arg(droppedSpriteCap)
                .arg(droppedBlobCap)
                .arg(out.stringBlobUsedBytes)
                .arg(out.markerGeometryBlobUsedBytes)
                .arg(state.playheadSeconds, 0, 'f', 3));
    }

    return static_cast<int>(out.spriteCount);
}

// ---- Muri report blob (v5) ----------------------------------------------
//
// All writes go through these helpers to keep cap-checking centralised.
// The blob format mirrors `MuriAnalysisReport`'s render-relevant fields:
// padWindows, actionTrails, judgeSpriteEvents, and the visible-marker
// subset of markerStates. Strings are encoded inline as length-prefix +
// UTF-8 bytes (no shared string blob — keeps the muri blob self-contained).

namespace {

bool muriAppendBytes(PreviewFrameStateSerial& out, const void* data, size_t bytes)
{
    if (out.muriReportBlobUsedBytes + bytes >
        static_cast<quint32>(out.muriReportBlob.size())) {
        return false;
    }
    std::memcpy(out.muriReportBlob.data() + out.muriReportBlobUsedBytes,
                data, bytes);
    out.muriReportBlobUsedBytes += static_cast<quint32>(bytes);
    return true;
}

bool muriAppendU32(PreviewFrameStateSerial& out, quint32 v) { return muriAppendBytes(out, &v, 4); }
bool muriAppendF64(PreviewFrameStateSerial& out, double v)  { return muriAppendBytes(out, &v, 8); }
bool muriAppendI32(PreviewFrameStateSerial& out, qint32 v)  { return muriAppendBytes(out, &v, 4); }

bool muriAppendString(PreviewFrameStateSerial& out, const QString& s)
{
    const QByteArray utf8 = s.toUtf8();
    const quint32 len = static_cast<quint32>(utf8.size());
    if (!muriAppendU32(out, len)) return false;
    if (len > 0) {
        return muriAppendBytes(out, utf8.constData(), len);
    }
    return true;
}

bool muriAppendStringList(PreviewFrameStateSerial& out, const QStringList& strings)
{
    if (!muriAppendU32(out, static_cast<quint32>(strings.size()))) return false;
    for (const QString& s : strings) {
        if (!muriAppendString(out, s)) return false;
    }
    return true;
}

bool muriAppendDoubles(PreviewFrameStateSerial& out, const QVector<double>& values)
{
    if (!muriAppendU32(out, static_cast<quint32>(values.size()))) return false;
    for (double v : values) {
        if (!muriAppendF64(out, v)) return false;
    }
    return true;
}

bool muriAppendCheckpointStates(PreviewFrameStateSerial& out,
                                 const QVector<MuriCheckpointState>& checkpoints)
{
    if (!muriAppendU32(out, static_cast<quint32>(checkpoints.size()))) return false;
    for (const MuriCheckpointState& cp : checkpoints) {
        if (!muriAppendF64(out, cp.proportion)) return false;
        if (!muriAppendF64(out, cp.second)) return false;
        if (!muriAppendStringList(out, cp.pads)) return false;
        if (!muriAppendString(out, cp.causeMarkerKey)) return false;
        if (!muriAppendString(out, cp.causeType)) return false;
        if (!muriAppendU32(out, static_cast<quint32>(cp.cause))) return false;
        if (!muriAppendU32(out, cp.skipped ? 1u : 0u)) return false;
    }
    return true;
}

bool muriAppendMarkerState(PreviewFrameStateSerial& out, const MarkerMuriState& s)
{
    if (!muriAppendString(out, s.markerKey)) return false;
    if (!muriAppendString(out, s.markerType)) return false;
    if (!muriAppendI32(out, s.line)) return false;
    if (!muriAppendI32(out, s.col)) return false;
    if (!muriAppendF64(out, s.second)) return false;
    if (!muriAppendF64(out, s.endSecond)) return false;
    if (!muriAppendU32(out, s.earlyCleared ? 1u : 0u)) return false;
    if (!muriAppendF64(out, s.flashSecond)) return false;

    // slideSegments
    if (!muriAppendU32(out, static_cast<quint32>(s.slideSegments.size()))) return false;
    for (const MuriSegmentState& seg : s.slideSegments) {
        if (!muriAppendU32(out, static_cast<quint32>(seg.areaCheckpoints.size()))) return false;
        for (const QVector<MuriCheckpointState>& areaCps : seg.areaCheckpoints) {
            if (!muriAppendCheckpointStates(out, areaCps)) return false;
        }
        if (!muriAppendF64(out, seg.completedSecond)) return false;
        if (!muriAppendF64(out, seg.expectedCompletedSecond)) return false;
        if (!muriAppendF64(out, seg.criticalSecond)) return false;
    }

    // wifiLaneAreas: QVector<QVector<QVector<MuriCheckpointState>>>
    if (!muriAppendU32(out, static_cast<quint32>(s.wifiLaneAreas.size()))) return false;
    for (const QVector<QVector<MuriCheckpointState>>& laneAreas : s.wifiLaneAreas) {
        if (!muriAppendU32(out, static_cast<quint32>(laneAreas.size()))) return false;
        for (const QVector<MuriCheckpointState>& areaCps : laneAreas) {
            if (!muriAppendCheckpointStates(out, areaCps)) return false;
        }
    }

    // wifiLaneProgressSeconds: QVector<QVector<double>>
    if (!muriAppendU32(out, static_cast<quint32>(s.wifiLaneProgressSeconds.size()))) return false;
    for (const QVector<double>& laneProgress : s.wifiLaneProgressSeconds) {
        if (!muriAppendDoubles(out, laneProgress)) return false;
    }

    if (!muriAppendF64(out, s.wifiCompletedSecond)) return false;
    if (!muriAppendF64(out, s.wifiExpectedCompletedSecond)) return false;
    if (!muriAppendF64(out, s.wifiPadCSecond)) return false;
    if (!muriAppendF64(out, s.wifiCriticalSecond)) return false;
    return true;
}

bool overlapsMuriWindow(double itemStart, double itemEnd,
                        double windowStart, double windowEnd)
{
    if (itemEnd < 0.0 && itemStart < 0.0) return false;
    const double effectiveEnd = (itemEnd >= 0.0) ? itemEnd : itemStart;
    if (effectiveEnd < windowStart) return false;
    if (itemStart > windowEnd) return false;
    return true;
}

}  // namespace

int projectMuriAnalysisReportToSerial(const miacode::preview::scene::PreviewFrameState& state,
                                       PreviewFrameStateSerial& out)
{
    out.muriReportBlobUsedBytes = 0;

    const MuriAnalysisReport& report = state.muriAnalysisReport;
    if (report.isEmpty()) {
        return 0;  // nothing to project — leave blob empty
    }

    const double playhead = state.playheadSeconds;
    const double windowStart = playhead - kSpriteProjectionLookbackSeconds;
    const double windowEnd = playhead + kSpriteProjectionLookaheadSeconds;

    if (!muriAppendU32(out, kMuriReportMagic)) {
        out.muriReportBlobUsedBytes = 0;
        return 0;
    }

    // padWindows — visible only.
    const auto windowStartBefore = out.muriReportBlobUsedBytes;
    if (!muriAppendU32(out, 0)) { out.muriReportBlobUsedBytes = 0; return 0; }
    quint32 padWindowsWritten = 0;
    for (const MuriPadWindow& w : report.padWindows) {
        if (!overlapsMuriWindow(w.startSecond, w.endSecond, windowStart, windowEnd)) continue;
        if (!muriAppendString(out, w.pad)
            || !muriAppendF64(out, w.startSecond)
            || !muriAppendF64(out, w.endSecond)
            || !muriAppendString(out, w.sourceMarkerKey)
            || !muriAppendString(out, w.sourceType)) {
            break;
        }
        ++padWindowsWritten;
    }
    std::memcpy(out.muriReportBlob.data() + windowStartBefore,
                &padWindowsWritten, sizeof(padWindowsWritten));

    // actionTrails — visible only.
    const auto trailsCountOffset = out.muriReportBlobUsedBytes;
    if (!muriAppendU32(out, 0)) return static_cast<int>(out.muriReportBlobUsedBytes);
    quint32 trailsWritten = 0;
    for (const MuriActionTrail& t : report.actionTrails) {
        if (!overlapsMuriWindow(t.startSecond, t.endSecond, windowStart, windowEnd)) continue;
        if (!muriAppendString(out, t.sourceMarkerKey)
            || !muriAppendString(out, t.sourceType)
            || !muriAppendF64(out, t.startSecond)
            || !muriAppendF64(out, t.endSecond)
            || !muriAppendF64(out, t.radius)
            || !muriAppendU32(out, static_cast<quint32>(t.points.size()))) {
            break;
        }
        bool ok = true;
        for (const QPointF& p : t.points) {
            if (!muriAppendF64(out, p.x()) || !muriAppendF64(out, p.y())) { ok = false; break; }
        }
        if (!ok) break;
        ++trailsWritten;
    }
    std::memcpy(out.muriReportBlob.data() + trailsCountOffset,
                &trailsWritten, sizeof(trailsWritten));

    // judgeSpriteEvents — visible only (gate on second).
    const auto eventsCountOffset = out.muriReportBlobUsedBytes;
    if (!muriAppendU32(out, 0)) return static_cast<int>(out.muriReportBlobUsedBytes);
    quint32 eventsWritten = 0;
    for (const MuriJudgeSpriteEvent& e : report.judgeSpriteEvents) {
        if (!overlapsMuriWindow(e.spawnSecond, e.second, windowStart, windowEnd)) continue;
        if (!muriAppendU32(out, static_cast<quint32>(e.kind))
            || !muriAppendU32(out, static_cast<quint32>(e.simpleEffect))
            || !muriAppendF64(out, e.second)
            || !muriAppendF64(out, e.spawnSecond)
            || !muriAppendString(out, e.markerKey)
            || !muriAppendString(out, e.pad)
            || !muriAppendI32(out, e.lane)) {
            break;
        }
        ++eventsWritten;
    }
    std::memcpy(out.muriReportBlob.data() + eventsCountOffset,
                &eventsWritten, sizeof(eventsWritten));

    // markerStates — visible markers only. Filter by `second` overlap with
    // the projection window, matching `projectActiveSpritesToSerial`'s gate
    // so worker-side TrackLayer's per-marker lookup finds entries for the
    // markers it's about to render.
    const auto statesCountOffset = out.muriReportBlobUsedBytes;
    if (!muriAppendU32(out, 0)) return static_cast<int>(out.muriReportBlobUsedBytes);
    quint32 statesWritten = 0;
    for (auto it = report.markerStates.constBegin(); it != report.markerStates.constEnd(); ++it) {
        const MarkerMuriState& s = it.value();
        if (!overlapsMuriWindow(s.second, s.endSecond, windowStart, windowEnd)) continue;
        if (!muriAppendString(out, it.key())) break;
        if (!muriAppendMarkerState(out, s)) break;
        ++statesWritten;
    }
    std::memcpy(out.muriReportBlob.data() + statesCountOffset,
                &statesWritten, sizeof(statesWritten));

    return static_cast<int>(out.muriReportBlobUsedBytes);
}

namespace {

// Cursor for parsing the muri blob. Mirrors BlobCursor but keeps the
// state.muriReportBlob bounds.
struct MuriCursor {
    const char* p;
    const char* end;
    bool readU32(quint32& out) {
        if (p == nullptr || end - p < 4) { p = nullptr; return false; }
        std::memcpy(&out, p, 4); p += 4; return true;
    }
    bool readI32(qint32& out) {
        if (p == nullptr || end - p < 4) { p = nullptr; return false; }
        std::memcpy(&out, p, 4); p += 4; return true;
    }
    bool readF64(double& out) {
        if (p == nullptr || end - p < 8) { p = nullptr; return false; }
        std::memcpy(&out, p, 8); p += 8; return true;
    }
    bool readString(QString& out) {
        quint32 len = 0; if (!readU32(len)) return false;
        if (len == 0) { out.clear(); return true; }
        if (p == nullptr || static_cast<quint32>(end - p) < len) { p = nullptr; return false; }
        out = QString::fromUtf8(p, static_cast<int>(len));
        p += len;
        return true;
    }
    bool readStringList(QStringList& out) {
        quint32 n = 0; if (!readU32(n)) return false;
        out.clear();
        out.reserve(static_cast<int>(n));
        for (quint32 i = 0; i < n; ++i) {
            QString s;
            if (!readString(s)) return false;
            out.append(s);
        }
        return true;
    }
    bool readDoubles(QVector<double>& out) {
        quint32 n = 0; if (!readU32(n)) return false;
        out.resize(static_cast<int>(n));
        for (quint32 i = 0; i < n; ++i) {
            double v = 0;
            if (!readF64(v)) return false;
            out[static_cast<int>(i)] = v;
        }
        return true;
    }
    bool readCheckpoints(QVector<MuriCheckpointState>& out) {
        quint32 n = 0; if (!readU32(n)) return false;
        out.resize(static_cast<int>(n));
        for (quint32 i = 0; i < n; ++i) {
            MuriCheckpointState cp;
            quint32 cause = 0;
            quint32 skipped = 0;
            if (!readF64(cp.proportion) || !readF64(cp.second)
                || !readStringList(cp.pads)
                || !readString(cp.causeMarkerKey) || !readString(cp.causeType)
                || !readU32(cause) || !readU32(skipped)) {
                return false;
            }
            cp.cause = static_cast<AreaJudgeCause>(cause);
            cp.skipped = (skipped != 0);
            out[static_cast<int>(i)] = cp;
        }
        return true;
    }
    bool readMarkerState(MarkerMuriState& s) {
        quint32 earlyCleared = 0;
        if (!readString(s.markerKey) || !readString(s.markerType)
            || !readI32(s.line) || !readI32(s.col)
            || !readF64(s.second) || !readF64(s.endSecond)
            || !readU32(earlyCleared) || !readF64(s.flashSecond)) {
            return false;
        }
        s.earlyCleared = (earlyCleared != 0);

        // slideSegments
        quint32 segN = 0;
        if (!readU32(segN)) return false;
        s.slideSegments.resize(static_cast<int>(segN));
        for (quint32 i = 0; i < segN; ++i) {
            MuriSegmentState seg;
            quint32 areaN = 0;
            if (!readU32(areaN)) return false;
            seg.areaCheckpoints.resize(static_cast<int>(areaN));
            for (quint32 j = 0; j < areaN; ++j) {
                if (!readCheckpoints(seg.areaCheckpoints[static_cast<int>(j)])) return false;
            }
            if (!readF64(seg.completedSecond)
                || !readF64(seg.expectedCompletedSecond)
                || !readF64(seg.criticalSecond)) return false;
            s.slideSegments[static_cast<int>(i)] = std::move(seg);
        }

        // wifiLaneAreas
        quint32 laneN = 0;
        if (!readU32(laneN)) return false;
        s.wifiLaneAreas.resize(static_cast<int>(laneN));
        for (quint32 i = 0; i < laneN; ++i) {
            quint32 areaN = 0;
            if (!readU32(areaN)) return false;
            s.wifiLaneAreas[static_cast<int>(i)].resize(static_cast<int>(areaN));
            for (quint32 j = 0; j < areaN; ++j) {
                if (!readCheckpoints(s.wifiLaneAreas[static_cast<int>(i)][static_cast<int>(j)])) {
                    return false;
                }
            }
        }

        // wifiLaneProgressSeconds
        quint32 progN = 0;
        if (!readU32(progN)) return false;
        s.wifiLaneProgressSeconds.resize(static_cast<int>(progN));
        for (quint32 i = 0; i < progN; ++i) {
            if (!readDoubles(s.wifiLaneProgressSeconds[static_cast<int>(i)])) return false;
        }

        if (!readF64(s.wifiCompletedSecond)
            || !readF64(s.wifiExpectedCompletedSecond)
            || !readF64(s.wifiPadCSecond)
            || !readF64(s.wifiCriticalSecond)) {
            return false;
        }
        return true;
    }
};

}  // namespace

bool unpackMuriAnalysisReport(const PreviewFrameStateSerial& snapshot,
                               miacode::preview::scene::PreviewFrameState* outState)
{
    if (outState == nullptr) return false;
    auto& report = outState->muriAnalysisReport;
    report = MuriAnalysisReport{};  // clear

    if (snapshot.muriReportBlobUsedBytes == 0) {
        return true;  // empty is fine
    }
    MuriCursor c{ snapshot.muriReportBlob.data(),
                  snapshot.muriReportBlob.data() + snapshot.muriReportBlobUsedBytes };
    quint32 magic = 0;
    if (!c.readU32(magic) || magic != kMuriReportMagic) return false;

    // padWindows
    quint32 nPad = 0;
    if (!c.readU32(nPad)) return false;
    report.padWindows.resize(static_cast<int>(nPad));
    for (quint32 i = 0; i < nPad; ++i) {
        MuriPadWindow w;
        if (!c.readString(w.pad)
            || !c.readF64(w.startSecond) || !c.readF64(w.endSecond)
            || !c.readString(w.sourceMarkerKey) || !c.readString(w.sourceType)) {
            report = MuriAnalysisReport{};
            return false;
        }
        report.padWindows[static_cast<int>(i)] = w;
    }

    // actionTrails
    quint32 nTrail = 0;
    if (!c.readU32(nTrail)) return false;
    report.actionTrails.resize(static_cast<int>(nTrail));
    for (quint32 i = 0; i < nTrail; ++i) {
        MuriActionTrail t;
        quint32 nPoint = 0;
        if (!c.readString(t.sourceMarkerKey) || !c.readString(t.sourceType)
            || !c.readF64(t.startSecond) || !c.readF64(t.endSecond) || !c.readF64(t.radius)
            || !c.readU32(nPoint)) {
            report = MuriAnalysisReport{};
            return false;
        }
        t.points.resize(static_cast<int>(nPoint));
        for (quint32 j = 0; j < nPoint; ++j) {
            double x = 0, y = 0;
            if (!c.readF64(x) || !c.readF64(y)) {
                report = MuriAnalysisReport{};
                return false;
            }
            t.points[static_cast<int>(j)] = QPointF(x, y);
        }
        report.actionTrails[static_cast<int>(i)] = std::move(t);
    }

    // judgeSpriteEvents
    quint32 nEvent = 0;
    if (!c.readU32(nEvent)) return false;
    report.judgeSpriteEvents.resize(static_cast<int>(nEvent));
    for (quint32 i = 0; i < nEvent; ++i) {
        MuriJudgeSpriteEvent e;
        quint32 kind = 0, simple = 0;
        if (!c.readU32(kind) || !c.readU32(simple)
            || !c.readF64(e.second) || !c.readF64(e.spawnSecond)
            || !c.readString(e.markerKey) || !c.readString(e.pad)
            || !c.readI32(e.lane)) {
            report = MuriAnalysisReport{};
            return false;
        }
        e.kind = static_cast<MuriJudgeSpriteKind>(kind);
        e.simpleEffect = static_cast<MuriSimpleJudgeEffect>(simple);
        report.judgeSpriteEvents[static_cast<int>(i)] = std::move(e);
    }

    // markerStates
    quint32 nState = 0;
    if (!c.readU32(nState)) return false;
    report.markerStates.reserve(static_cast<int>(nState));
    for (quint32 i = 0; i < nState; ++i) {
        QString key;
        MarkerMuriState s;
        if (!c.readString(key) || !c.readMarkerState(s)) {
            report = MuriAnalysisReport{};
            return false;
        }
        report.markerStates.insert(key, std::move(s));
    }

    return true;
}

}  // namespace miacode::preview::ipc
