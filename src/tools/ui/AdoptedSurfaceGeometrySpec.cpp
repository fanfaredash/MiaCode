// Geometry for the macOS adopted-surface takeover, split out of the Widgets
// editor's spec when that editor was deleted. Neither half is about a text
// editor: one resolves coordinates through a rehosted QWindow, the other plans
// drag-selection autoscroll without asking Qt where the pointer is — Qt derives
// that from QCursor::pos(), which on an adopted surface lands on the orphan
// panel rather than the widget the user is dragging in.

#include "common/AdoptedSurfaceDragAutoScroll.h"
#include "common/AdoptedWidgetCoordinates.h"

#include <QApplication>
#include <QTextStream>
#include <QWidget>
#include <QWindow>

namespace {

bool expect(bool condition, const QString& message, QTextStream& out, int* failed)
{
    if (condition) {
        out << "[PASS] " << message << '\n';
        return true;
    }
    out << "[FAIL] " << message << '\n';
    if (failed != nullptr) {
        ++(*failed);
    }
    return false;
}

} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    Q_UNUSED(app);

    QTextStream out(stdout);
    int failed = 0;

    {
        QWidget adoptedSurface;
        QWidget nestedWidget(&adoptedSurface);
        nestedWidget.move(17, 29);
        auto* adoptedWindow = new QWindow;
        const QPoint localPoint(5, 7);
        miacode::ui::bindAdoptedSurfaceWindow(&adoptedSurface, adoptedWindow);
        const auto route = miacode::ui::adoptedWidgetCoordinateRoute(&nestedWidget, localPoint);
        expect(route.window == adoptedWindow
                   && route.surfacePoint == QPoint(22, 36),
               QStringLiteral("adopted widget coordinates resolve through the bridge surface"),
               out,
               &failed);
        expect(
            miacode::ui::mapGlobalPointToWidget(
                &nestedWidget,
                adoptedWindow->mapToGlobal(route.surfacePoint)) == localPoint,
            QStringLiteral("adopted global coordinates resolve back into the nested widget"),
            out,
            &failed);
        delete adoptedWindow;
        expect(miacode::ui::adoptedWidgetCoordinateRoute(&nestedWidget, localPoint).window == nullptr,
               QStringLiteral("destroying the adopted window clears the bridge coordinate route"),
               out,
               &failed);
    }

    {
        // Drag-selection autoscroll geometry (the brain of the macOS takeover —
        // no scroll area on an adopted surface may let Qt re-derive the held
        // pointer from QCursor::pos()).
        const QRect viewportRect(0, 0, 400, 300);
        const auto inside = miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(120, 90));
        expect(inside.intervalMs == 0
                   && inside.horizontalStep == 0
                   && inside.verticalStep == 0
                   && inside.clampedPosition == QPoint(120, 90),
               QStringLiteral("pointer inside the viewport plans no autoscroll"),
               out,
               &failed);

        const auto gutter = miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(-30, 90));
        expect(gutter.clampedPosition == QPoint(0, 90)
                   && gutter.horizontalStep == -1
                   && gutter.verticalStep == 0
                   && gutter.intervalMs >= 16 && gutter.intervalMs <= 100,
               QStringLiteral("pointer over the line-number gutter clamps back and scrolls left"),
               out,
               &failed);

        const auto belowRight =
            miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(480, 360));
        expect(belowRight.clampedPosition == QPoint(399, 299)
                   && belowRight.horizontalStep == 1
                   && belowRight.verticalStep == 1,
               QStringLiteral("pointer past the bottom-right corner scrolls on both axes"),
               out,
               &failed);

        const auto nudge = miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(0, -2));
        const auto lunge = miacode::ui::planDragAutoScrollStep(viewportRect, QPoint(0, -200));
        expect(nudge.intervalMs == 100 && lunge.intervalMs == 16
                   && nudge.verticalStep == -1 && lunge.verticalStep == -1,
               QStringLiteral("autoscroll cadence accelerates with the overshoot, floored at a frame"),
               out,
               &failed);

        expect(miacode::ui::planDragAutoScrollStep(QRect(), QPoint(-30, 90)).intervalMs == 0,
               QStringLiteral("an invalid viewport rect plans no autoscroll"),
               out,
               &failed);
    }

    if (failed != 0) {
        out << "AdoptedSurfaceGeometry spec failed: " << failed << '\n';
        return 1;
    }

    out << "AdoptedSurfaceGeometry spec passed.\n";
    return 0;
}
