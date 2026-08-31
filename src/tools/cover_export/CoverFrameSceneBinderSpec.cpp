#include "tools/cover_export/CoverFrameSceneBinder.h"

#include "core/scene/PreviewFrameState.h"

#include <QCoreApplication>
#include <QTextStream>

using miacode::cover_export::CoverFrameSceneBinder;

namespace {

bool require(bool condition, const QString& message, QTextStream& out)
{
    if (!condition) {
        out << "FAIL: " << message << '\n';
        return false;
    }
    return true;
}

bool testIdentityAndDetach(QTextStream& out)
{
    CoverFrameSceneBinder binder;
    QObject first;
    QObject second;
    miacode::preview::scene::PreviewFrameState state;

    binder.setFrameState(&state);
    binder.bindLiveChartScene(&first);
    if (!require(binder.liveChartScene() == &first && binder.liveChartSceneBound(),
                 QStringLiteral("first scene binds with a frame state"), out)) return false;

    binder.bindLiveChartScene(&second);
    binder.unbindLiveChartScene(&first);
    if (!require(binder.liveChartScene() == &second && binder.liveChartSceneBound(),
                 QStringLiteral("old scene unbind cannot clear the replacement"), out)) return false;

    binder.setFrameState(nullptr);
    if (!require(!binder.liveChartSceneBound(),
                 QStringLiteral("clearing the borrowed frame state disables the binding"), out)) return false;

    binder.setFrameState(&state);
    binder.detachLiveChartScene();
    return require(binder.liveChartScene() == nullptr && !binder.liveChartSceneBound(),
                   QStringLiteral("detach clears the borrowed scene safely"), out);
}

bool testDestroyedScene(QTextStream& out)
{
    CoverFrameSceneBinder binder;
    miacode::preview::scene::PreviewFrameState state;
    int boundChanges = 0;
    QObject::connect(&binder, &CoverFrameSceneBinder::liveChartSceneBoundChanged,
                     [&boundChanges] { ++boundChanges; });
    binder.setFrameState(&state);
    auto* scene = new QObject;
    binder.bindLiveChartScene(scene);
    delete scene;
    return require(binder.liveChartScene() == nullptr && !binder.liveChartSceneBound()
                       && boundChanges == 2,
                   QStringLiteral("destroyed scene clears the binding and notifies QML"), out);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    const bool ok = testIdentityAndDetach(out) && testDestroyedScene(out);
    if (ok) {
        out << "CoverFrameSceneBinder spec passed\n";
        return 0;
    }
    return 1;
}
