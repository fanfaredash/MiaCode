#include "app/quick_shell/QuickShellPopupPosition.h"
#include "app/ui/UiNativeWindowThemePolicy.h"

#include <QCoreApplication>
#include <QDebug>

namespace {

bool expectPoint(const char* name, const QPoint& actual, const QPoint& expected)
{
    if (actual == expected) {
        return true;
    }
    qCritical().nospace() << name << ": expected " << expected << ", got " << actual;
    return false;
}

bool expectAppearance(
    const char* name,
    UiNativeWindowThemePolicy::Appearance actual,
    UiNativeWindowThemePolicy::Appearance expected)
{
    if (actual == expected) {
        return true;
    }
    qCritical().nospace() << name << ": appearance mismatch";
    return false;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    bool ok = true;

    const QRect available(0, 0, 1440, 900);
    const QRect actionScreenRect = miacode::quick_shell::actionScreenRectFromSurface(
        QPoint(160, 125), QRect(20, 0, 60, 30));
    if (actionScreenRect != QRect(180, 125, 60, 30)) {
        qCritical() << "surface-local action rect did not include adopted window origin"
                    << actionScreenRect;
        ok = false;
    }
    ok &= expectPoint(
        "opens below action",
        miacode::quick_shell::popupTopLeftForAction(
            QRect(400, 200, 80, 24), QSize(180, 120), available),
        QPoint(400, 224));
    ok &= expectPoint(
        "clamps at right edge",
        miacode::quick_shell::popupTopLeftForAction(
            QRect(1380, 200, 60, 24), QSize(180, 120), available),
        QPoint(1260, 224));
    ok &= expectPoint(
        "flips above action at bottom edge",
        miacode::quick_shell::popupTopLeftForAction(
            QRect(400, 850, 80, 24), QSize(180, 120), available),
        QPoint(400, 730));
    ok &= expectPoint(
        "clamps oversized popup to available origin",
        miacode::quick_shell::popupTopLeftForAction(
            QRect(-20, -10, 30, 20), QSize(1600, 1000), available),
        QPoint(0, 0));

    using Appearance = UiNativeWindowThemePolicy::Appearance;
    ok &= expectAppearance(
        "system appearance",
        UiNativeWindowThemePolicy::appearanceFor(UiText::ThemePreference::System),
        Appearance::System);
    ok &= expectAppearance(
        "light appearance",
        UiNativeWindowThemePolicy::appearanceFor(UiText::ThemePreference::Light),
        Appearance::Light);
    ok &= expectAppearance(
        "dark appearance",
        UiNativeWindowThemePolicy::appearanceFor(UiText::ThemePreference::Dark),
        Appearance::Dark);

    return ok ? 0 : 1;
}
