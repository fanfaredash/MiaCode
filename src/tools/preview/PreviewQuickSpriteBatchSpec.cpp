#include <QCoreApplication>
#include <QHash>
#include <QTextStream>
#include <QVector>

#include "preview/quick_scene/PreviewQuickSpriteBatchPolicy.h"

namespace {

struct VertexStub {
    int x = 0;
    int y = 0;
    int u = 0;
    int v = 0;
    int opacity = 0;
    int effect = 0;

    bool operator==(const VertexStub& other) const
    {
        return x == other.x
            && y == other.y
            && u == other.u
            && v == other.v
            && opacity == other.opacity
            && effect == other.effect;
    }
};

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyOrderedRunGrouping(QTextStream& err)
{
    const QVector<miacode::preview::quick_scene::SpriteFamilyKey> familyKeys = {1, 1, 2, 2, 1, 3, 3};
    const auto runs = miacode::preview::quick_scene::buildPreviewQuickOrderedRunRanges(familyKeys);
    if (!require(runs.size() == 4, QStringLiteral("ordered runs split on non-contiguous family transitions"), err)) {
        return false;
    }
    if (!require(runs.at(0).familyKey == 1 && runs.at(0).startSpriteIndex == 0 && runs.at(0).spriteCount == 2, QStringLiteral("run 0 matches expected family and count"), err)) {
        return false;
    }
    if (!require(runs.at(1).familyKey == 2 && runs.at(1).startSpriteIndex == 2 && runs.at(1).spriteCount == 2, QStringLiteral("run 1 matches expected family and count"), err)) {
        return false;
    }
    if (!require(runs.at(2).familyKey == 1 && runs.at(2).startSpriteIndex == 4 && runs.at(2).spriteCount == 1, QStringLiteral("run 2 preserves later re-entry as a new run"), err)) {
        return false;
    }
    if (!require(runs.at(3).familyKey == 3 && runs.at(3).startSpriteIndex == 5 && runs.at(3).spriteCount == 2, QStringLiteral("run 3 matches expected family and count"), err)) {
        return false;
    }
    return true;
}

bool verifyReserveHintsAndCapacityBuckets(QTextStream& err)
{
    QHash<miacode::preview::quick_scene::SpriteFamilyKey, int> reserveHints;
    if (!require(
            miacode::preview::quick_scene::previewQuickSpriteInitialReserveHint(reserveHints, 1) == 96,
            QStringLiteral("missing family hint falls back to one 96-vertex bucket"),
            err)) {
        return false;
    }

    reserveHints.insert(7, 150);
    if (!require(
            miacode::preview::quick_scene::previewQuickSpriteInitialReserveHint(reserveHints, 7) == 150,
            QStringLiteral("existing family hint is reused across frames"),
            err)) {
        return false;
    }

    if (!require(
            miacode::preview::quick_scene::previewQuickSpriteAlignedVertexCapacity(1) == 96,
            QStringLiteral("capacity bucket rounds tiny requests up to 96 vertices"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview::quick_scene::previewQuickSpriteAlignedVertexCapacity(96) == 96,
            QStringLiteral("exact bucket edge stays unchanged"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview::quick_scene::previewQuickSpriteAlignedVertexCapacity(97) == 192,
            QStringLiteral("capacity bucket rounds 97 vertices up to 192"),
            err)) {
        return false;
    }

    return true;
}

bool verifyShrinkPolicy(QTextStream& err)
{
    if (!require(
            miacode::preview::quick_scene::previewQuickSpriteNextUnderuseFrameCount(120, 384, 0) == 1,
            QStringLiteral("underuse counter increments when required is at most half capacity"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview::quick_scene::previewQuickSpriteNextUnderuseFrameCount(220, 384, 12) == 0,
            QStringLiteral("underuse counter resets once usage climbs above half capacity"),
            err)) {
        return false;
    }
    if (!require(
            !miacode::preview::quick_scene::previewQuickSpriteShouldShrink(120, 384, 59),
            QStringLiteral("shrink does not trigger before the 60-frame threshold"),
            err)) {
        return false;
    }
    if (!require(
            miacode::preview::quick_scene::previewQuickSpriteShouldShrink(120, 384, 60),
            QStringLiteral("shrink triggers after 60 underused frames"),
            err)) {
        return false;
    }
    if (!require(
            !miacode::preview::quick_scene::previewQuickSpriteShouldShrink(200, 192, 80),
            QStringLiteral("shrink does not trigger when already at the aligned bucket"),
            err)) {
        return false;
    }
    return true;
}

bool verifyDegenerateTailFillPreservesPrefix(QTextStream& err)
{
    QVector<VertexStub> vertices(12);
    for (int index = 0; index < 6; ++index) {
        vertices[index] = VertexStub{index, index + 1, index + 2, index + 3, 255, index};
    }
    const QVector<VertexStub> expectedPrefix = vertices.first(6);
    const VertexStub filler{99, 88, 77, 66, 0, 0};
    miacode::preview::quick_scene::fillPreviewQuickDegenerateVertexTail(vertices.data(), 6, vertices.size(), filler);

    for (int index = 0; index < 6; ++index) {
        if (!require(vertices[index] == expectedPrefix[index], QStringLiteral("degenerate tail fill preserves valid prefix"), err)) {
            return false;
        }
    }
    for (int index = 6; index < vertices.size(); ++index) {
        if (!require(vertices[index] == filler, QStringLiteral("degenerate tail fill writes the requested filler to the tail"), err)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyOrderedRunGrouping(err)) {
        return 1;
    }
    if (!verifyReserveHintsAndCapacityBuckets(err)) {
        return 1;
    }
    if (!verifyShrinkPolicy(err)) {
        return 1;
    }
    if (!verifyDegenerateTailFillPreservesPrefix(err)) {
        return 1;
    }

    out << "preview_quick_sprite_batch_spec ok" << Qt::endl;
    return 0;
}
