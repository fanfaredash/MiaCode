#include "tools/cover_export/CoverCompositionState.h"
#include "tools/cover_export/CoverLayoutModel.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QTextStream>

using miacode::cover_export::CoverCompositionState;
using miacode::cover_export::CoverLayer;
using miacode::cover_export::CoverLayoutModel;

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool testMultiFrameModel(QTextStream& err)
{
    CoverLayoutModel model;
    CoverLayer* first = model.addChartFrameLayer(12.34);
    CoverLayer* second = model.addChartFrameLayer(65.2);
    if (!require(first != nullptr && second != nullptr, QStringLiteral("adds chart-frame layers"), err)) return false;
    if (!require(first->key() != second->key(), QStringLiteral("chart-frame keys are unique"), err)) return false;
    if (!require(model.chartFrameLayers().size() == 2, QStringLiteral("tracks all chart-frame layers"), err)) return false;
    if (!require(model.visibleChartFrameLayers().size() == 2, QStringLiteral("tracks visible chart-frame layers"), err)) return false;

    second->setFrameBgEnabled(false);
    second->setFrameBgBrightness(0.42);
    second->setOpacity(0.55);
    CoverLayer* copy = model.duplicateLayer(second->key());
    if (!require(copy != nullptr, QStringLiteral("duplicates frame layer"), err)) return false;
    if (!require(copy->frameSeconds() == second->frameSeconds(), QStringLiteral("duplicate keeps frame time"), err)) return false;
    if (!require(copy->frameBgEnabled() == second->frameBgEnabled(), QStringLiteral("duplicate keeps frame bg toggle"), err)) return false;
    if (!require(qAbs(copy->frameBgBrightness() - 0.42) < 0.001, QStringLiteral("duplicate keeps brightness"), err)) return false;
    if (!require(qAbs(copy->opacity() - 0.55) < 0.001, QStringLiteral("duplicate keeps opacity"), err)) return false;

    if (!require(!model.removeLayer(CoverLayoutModel::cardKey()), QStringLiteral("card cannot be removed"), err)) return false;
    if (!require(model.removeLayer(first->key()), QStringLiteral("frame can be removed"), err)) return false;
    if (!require(model.chartFrameLayers().size() == 2, QStringLiteral("removal updates chart-frame list"), err)) return false;
    return true;
}

bool testRoundTrip(QTextStream& err)
{
    CoverLayoutModel model;
    CoverLayer* frame = model.addChartFrameLayer(7.5);
    frame->setNx(0.25);
    frame->setNy(0.75);
    frame->setFrameBgBrightness(0.33);
    frame->setOpacity(0.66);

    CoverLayoutModel restored;
    restored.fromJson(model.toJson());
    const QList<CoverLayer*> frames = restored.chartFrameLayers();
    if (!require(frames.size() == 1, QStringLiteral("round-trip restores one frame"), err)) return false;
    CoverLayer* restoredFrame = frames.constFirst();
    if (!require(qAbs(restoredFrame->frameSeconds() - 7.5) < 0.001, QStringLiteral("round-trip keeps frame time"), err)) return false;
    if (!require(qAbs(restoredFrame->frameBgBrightness() - 0.33) < 0.001, QStringLiteral("round-trip keeps brightness"), err)) return false;
    if (!require(qAbs(restoredFrame->opacity() - 0.66) < 0.001, QStringLiteral("round-trip keeps opacity"), err)) return false;
    return true;
}

bool testZOrder(QTextStream& err)
{
    CoverLayoutModel model;
    CoverLayer* first = model.addChartFrameLayer(1.0);
    CoverLayer* second = model.addChartFrameLayer(2.0);
    CoverLayer* third = model.addChartFrameLayer(3.0);
    if (!require(first != nullptr && second != nullptr && third != nullptr, QStringLiteral("z-order setup creates frames"), err)) return false;

    model.lowerLayer(model.indexOfKey(third->key()));
    if (!require(third->z() < second->z(), QStringLiteral("lowerLayer moves selected layer down"), err)) return false;

    model.raiseLayer(model.indexOfKey(third->key()));
    if (!require(third->z() > second->z(), QStringLiteral("raiseLayer moves selected layer up"), err)) return false;

    if (!require(model.moveLayerBefore(third->key(), first->key()), QStringLiteral("moveLayerBefore succeeds"), err)) return false;
    if (!require(third->z() < first->z(), QStringLiteral("moveLayerBefore rewrites z order"), err)) return false;

    if (!require(model.moveLayerAfter(third->key(), second->key()), QStringLiteral("moveLayerAfter succeeds"), err)) return false;
    if (!require(third->z() > second->z(), QStringLiteral("moveLayerAfter rewrites z order"), err)) return false;
    return true;
}

bool testV1Migration(QTextStream& err)
{
    QJsonObject layer;
    layer.insert(QStringLiteral("key"), QStringLiteral("chartFrame"));
    layer.insert(QStringLiteral("nx"), 0.4);
    layer.insert(QStringLiteral("ny"), 0.6);
    layer.insert(QStringLiteral("sizeFraction"), 0.7);
    layer.insert(QStringLiteral("visible"), true);
    layer.insert(QStringLiteral("frameSeconds"), 21.0);

    QJsonArray layers;
    layers.append(layer);
    QJsonObject layout;
    layout.insert(QStringLiteral("layers"), layers);

    QJsonObject chartFrame;
    chartFrame.insert(QStringLiteral("innerBackground"), false);
    chartFrame.insert(QStringLiteral("innerBrightness"), 0.25);

    QJsonObject root;
    root.insert(QStringLiteral("kind"), QStringLiteral("miacode-cover-composition"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("layout"), layout);
    root.insert(QStringLiteral("chartFrame"), chartFrame);

    CoverCompositionState state;
    if (!require(CoverCompositionState::fromJson(root, &state), QStringLiteral("v1 composition migrates"), err)) return false;

    CoverLayoutModel model;
    model.fromJson(state.layout);
    const QList<CoverLayer*> frames = model.chartFrameLayers();
    if (!require(frames.size() == 1, QStringLiteral("v1 chartFrame becomes one frame layer"), err)) return false;
    CoverLayer* frameLayer = frames.constFirst();
    if (!require(!frameLayer->frameBgEnabled(), QStringLiteral("v1 innerBackground migrates"), err)) return false;
    if (!require(qAbs(frameLayer->frameBgBrightness() - 0.25) < 0.001, QStringLiteral("v1 brightness migrates"), err)) return false;
    return true;
}

bool testSparseV1Migration(QTextStream& err)
{
    QJsonObject chartFrame;
    chartFrame.insert(QStringLiteral("innerBackground"), false);
    chartFrame.insert(QStringLiteral("innerBrightness"), 0.35);

    QJsonObject root;
    root.insert(QStringLiteral("kind"), QStringLiteral("miacode-cover-composition"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("chartFrame"), chartFrame);

    CoverCompositionState state;
    if (!require(CoverCompositionState::fromJson(root, &state), QStringLiteral("sparse v1 composition migrates"), err)) return false;

    CoverLayoutModel model;
    model.fromJson(state.layout);
    if (!require(model.layer(CoverLayoutModel::cardKey()) != nullptr, QStringLiteral("sparse v1 keeps card layer"), err)) return false;
    const QList<CoverLayer*> frames = model.chartFrameLayers();
    if (!require(frames.size() == 1, QStringLiteral("sparse v1 creates one frame layer"), err)) return false;
    if (!require(!frames.constFirst()->frameBgEnabled(), QStringLiteral("sparse v1 migrates inner bg toggle"), err)) return false;
    if (!require(qAbs(frames.constFirst()->frameBgBrightness() - 0.35) < 0.001, QStringLiteral("sparse v1 migrates brightness"), err)) return false;
    return true;
}

bool testDeleteSelectsNeighbour(QTextStream& err)
{
    CoverLayoutModel model;
    CoverLayer* f1 = model.addChartFrameLayer(1.0);   // z = 2
    CoverLayer* f2 = model.addChartFrameLayer(2.0);   // z = 3
    CoverLayer* f3 = model.addChartFrameLayer(3.0);   // z = 4
    if (!require(f1 != nullptr && f2 != nullptr && f3 != nullptr,
                 QStringLiteral("delete-neighbour setup creates frames"), err)) return false;

    // List view order is z descending: f3, f2, f1, card. Deleting a row selects the
    // next row below; the bottom-most frame falls through to the stable card.
    if (!require(model.selectionAfterRemoval(f3->key()) == f2->key(),
                 QStringLiteral("delete top frame selects the next"), err)) return false;
    if (!require(model.selectionAfterRemoval(f2->key()) == f1->key(),
                 QStringLiteral("delete middle frame selects the next"), err)) return false;
    if (!require(model.selectionAfterRemoval(f1->key()) == CoverLayoutModel::cardKey(),
                 QStringLiteral("delete bottom frame selects the card"), err)) return false;

    // Hold-Delete: the selection walks down f3 → f2 → f1 → card, clearing the frames.
    const QString k1 = f1->key();
    const QString k2 = f2->key();
    const QString k3 = f3->key();
    QString sel = model.selectionAfterRemoval(k3);
    model.removeLayer(k3);
    if (!require(sel == k2, QStringLiteral("hold-delete step 1 → next frame"), err)) return false;
    sel = model.selectionAfterRemoval(k2);
    model.removeLayer(k2);
    if (!require(sel == k1, QStringLiteral("hold-delete step 2 → next frame"), err)) return false;
    sel = model.selectionAfterRemoval(k1);
    model.removeLayer(k1);
    if (!require(sel == CoverLayoutModel::cardKey(), QStringLiteral("hold-delete step 3 → card"), err)) return false;
    if (!require(model.chartFrameLayers().isEmpty(), QStringLiteral("hold-delete cleared all frames"), err)) return false;
    return true;
}

bool testBackgroundBrightnessRoundTrip(QTextStream& err)
{
    CoverCompositionState state;
    state.size = QSize(1080, 1080);
    QJsonObject bg;
    bg.insert(QStringLiteral("mode"), QStringLiteral("jacket"));
    bg.insert(QStringLiteral("brightness"), 0.7);
    state.background = bg;

    CoverCompositionState restored;
    if (!require(CoverCompositionState::fromJson(state.toJson(), &restored),
                 QStringLiteral("background-brightness composition parses"), err)) return false;
    if (!require(qAbs(restored.background.value(QStringLiteral("brightness")).toDouble(-1.0) - 0.7) < 0.001,
                 QStringLiteral("background.brightness round-trips"), err)) return false;

    // Old layouts predate the key; the composition still parses (the 0.45 default
    // is applied by the inspector when restoring, not stored).
    QJsonObject legacy = state.toJson();
    QJsonObject legacyBg = legacy.value(QStringLiteral("background")).toObject();
    legacyBg.remove(QStringLiteral("brightness"));
    legacy.insert(QStringLiteral("background"), legacyBg);
    CoverCompositionState legacyState;
    if (!require(CoverCompositionState::fromJson(legacy, &legacyState),
                 QStringLiteral("legacy composition without brightness parses"), err)) return false;
    if (!require(!legacyState.background.contains(QStringLiteral("brightness")),
                 QStringLiteral("legacy composition keeps no brightness key"), err)) return false;
    return true;
}

bool testMoveByViewRows(QTextStream& err)
{
    CoverLayoutModel model;
    CoverLayer* f1 = model.addChartFrameLayer(1.0);   // z = 2
    CoverLayer* f2 = model.addChartFrameLayer(2.0);   // z = 3
    CoverLayer* f3 = model.addChartFrameLayer(3.0);   // z = 4
    CoverLayer* card = model.layer(CoverLayoutModel::cardKey());
    if (!require(f1 != nullptr && f2 != nullptr && f3 != nullptr && card != nullptr,
                 QStringLiteral("move-by-view-rows setup"), err)) return false;

    // View order is z descending: rows [f3, f2, f1, card]. Drag the bottom frame
    // f1 (row 2) to the front (row 0) — z must invert relative to the view rows.
    model.moveByViewRows(2, 0);
    if (!require(f1->z() > f3->z() && f3->z() > f2->z() && f2->z() > card->z(),
                 QStringLiteral("moveByViewRows brings the dragged row to the front"), err)) return false;

    // Drag f1 (now the front row) past the end → it sinks below the card.
    model.moveByViewRows(0, 99);
    if (!require(card->z() > f1->z(),
                 QStringLiteral("moveByViewRows can send a layer to the back"), err)) return false;
    return true;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    if (!testMultiFrameModel(err)) return 1;
    if (!testRoundTrip(err)) return 1;
    if (!testZOrder(err)) return 1;
    if (!testV1Migration(err)) return 1;
    if (!testSparseV1Migration(err)) return 1;
    if (!testDeleteSelectsNeighbour(err)) return 1;
    if (!testBackgroundBrightnessRoundTrip(err)) return 1;
    if (!testMoveByViewRows(err)) return 1;
    return 0;
}
