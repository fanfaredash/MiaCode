// Spec for the default-QSG-path window occlusion / visibility transition log.
//
// The observer itself needs a real QWindow and a windowing system, which a headless spec
// run does not have. What IS pinned here is the vocabulary and the payload contract that
// the freeze forensics depend on: an operator greps `occluded=1` / `minimized=1` and
// measures the freeze window from `occluded_for_ms`, so those keys must not drift.

#include <QString>
#include <QTextStream>
#include <QWindow>

#include "app/WindowVisibilityDiagnostics.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

}  // namespace

int main()
{
    namespace diag = miacode::app::window_diag;
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    // ---- vocabulary ------------------------------------------------------------
    ok &= require(diag::visibilityName(QWindow::Minimized) == QStringLiteral("Minimized"),
                  QStringLiteral("minimized visibility name"), err);
    ok &= require(diag::visibilityName(QWindow::Hidden) == QStringLiteral("Hidden"),
                  QStringLiteral("hidden visibility name"), err);
    ok &= require(diag::visibilityName(QWindow::Windowed) == QStringLiteral("Windowed"),
                  QStringLiteral("windowed visibility name"), err);
    ok &= require(diag::windowStatesText(Qt::WindowNoState) == QStringLiteral("NoState"),
                  QStringLiteral("empty window state renders as NoState"), err);
    ok &= require(diag::windowStatesText(Qt::WindowMinimized) == QStringLiteral("Minimized"),
                  QStringLiteral("minimized window state token"), err);
    ok &= require(diag::windowStatesText(Qt::WindowMaximized | Qt::WindowActive)
                      == QStringLiteral("Maximized|Active"),
                  QStringLiteral("combined window states are pipe-joined"), err);

    // Both "not presenting" cases count as occluded; a normal window does not.
    ok &= require(diag::isOccludedVisibility(QWindow::Minimized),
                  QStringLiteral("minimized counts as occluded"), err);
    ok &= require(diag::isOccludedVisibility(QWindow::Hidden),
                  QStringLiteral("hidden counts as occluded"), err);
    ok &= require(!diag::isOccludedVisibility(QWindow::Windowed),
                  QStringLiteral("windowed is not occluded"), err);
    ok &= require(!diag::isOccludedVisibility(QWindow::FullScreen),
                  QStringLiteral("fullscreen is not occluded"), err);

    // ---- payload contract ------------------------------------------------------
    const QString minimize = diag::visibilityTransitionPayload(
        QStringLiteral("root_window"), QWindow::Windowed, QWindow::Minimized,
        /*exposed=*/false, /*active=*/false, Qt::WindowMinimized, /*occludedForMs=*/-1);
    ok &= require(minimize.contains(QStringLiteral("surface=root_window"))
                      && minimize.contains(QStringLiteral("from=Windowed"))
                      && minimize.contains(QStringLiteral("to=Minimized")),
                  QStringLiteral("transition records the surface and both edges"), err);
    ok &= require(minimize.contains(QStringLiteral("exposed=0"))
                      && minimize.contains(QStringLiteral("minimized=1"))
                      && minimize.contains(QStringLiteral("occluded=1")),
                  QStringLiteral("minimize is flagged occluded"), err);
    ok &= require(!minimize.contains(QStringLiteral("occluded_for_ms")),
                  QStringLiteral("entering occlusion carries no duration yet"), err);

    // A visible-but-unexposed window (covered by another window) must still read as
    // occluded — that is exactly the "browser playing video on top of us" case.
    const QString covered = diag::visibilityTransitionPayload(
        QStringLiteral("preview_composite"), QWindow::Windowed, QWindow::Windowed,
        /*exposed=*/false, /*active=*/false, Qt::WindowNoState, /*occludedForMs=*/-1);
    ok &= require(covered.contains(QStringLiteral("occluded=1"))
                      && covered.contains(QStringLiteral("minimized=0")),
                  QStringLiteral("unexposed windowed surface counts as occluded"), err);

    const QString restore = diag::visibilityTransitionPayload(
        QStringLiteral("root_window"), QWindow::Minimized, QWindow::Windowed,
        /*exposed=*/true, /*active=*/true, Qt::WindowActive, /*occludedForMs=*/612345);
    ok &= require(restore.contains(QStringLiteral("occluded=0"))
                      && restore.contains(QStringLiteral("occluded_for_ms=612345")),
                  QStringLiteral("leaving occlusion reports how long it lasted"), err);

    const QString unnamed = diag::visibilityTransitionPayload(
        QString(), QWindow::Windowed, QWindow::Windowed, true, true, Qt::WindowNoState, -1);
    ok &= require(unnamed.contains(QStringLiteral("surface=(unnamed)")),
                  QStringLiteral("missing surface name renders as (unnamed)"), err);

    if (ok) {
        out << "Window visibility diagnostics spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
