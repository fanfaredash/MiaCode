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
    return 0;
}
