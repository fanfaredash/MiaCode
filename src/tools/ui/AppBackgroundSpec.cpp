#include "app/ui/AppBackgroundPainter.h"
#include "app/ui/AppBackgroundSettings.h"

#include <QApplication>
#include <QFile>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtMath>

namespace {
QImage render(miacode::ui::AppBackgroundPainter& background, QWidget& surface)
{
    const qreal dpr = surface.devicePixelRatioF();
    QImage image(QSize(qCeil(surface.width() * dpr), qCeil(surface.height() * dpr)),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    background.paintBackgroundForSurface(&surface, painter);
    return image;
}
}

int main(int argc, char** argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    QPalette palette = app.palette();
    palette.setColor(QPalette::Window, Qt::black);
    app.setPalette(palette);
    QTextStream out(stdout);
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) {
            out << "FAIL: " << message << '\n';
            ++failures;
        }
    };
    QTemporaryDir directory;
    expect(directory.isValid(), "temporary image directory");
    const QString path = directory.filePath(QStringLiteral("background.png"));
    QWidget window;
    window.resize(80, 60);
    window.move(100, 120);
    QWidget child(&window);
    child.setGeometry(12, 8, 40, 32);
    const qreal dpr = window.devicePixelRatioF();

    // A physical-pixel checkerboard exposes logical-resolution raster caches:
    // a 1x intermediate loses alternating pixels on a 1.5x/2x display.
    QImage source(QSize(qCeil(80 * dpr), qCeil(60 * dpr)), QImage::Format_RGB32);
    for (int y = 0; y < source.height(); ++y) {
        for (int x = 0; x < source.width(); ++x) {
            source.setPixel(x, y, (x + y) % 2 ? qRgb(255, 255, 255) : qRgb(0, 0, 0));
        }
    }
    expect(source.save(path), "write checkerboard");
    miacode::ui::AppBackgroundPainter background(&window);
    miacode::ui::AppBackgroundSettings settings;
    settings.enabled = true;
    settings.imagePath = path;
    settings.opacity = 0.8;
    settings.sizeMode = miacode::ui::AppBackgroundSizeMode::Stretch;
    background.setSettings(settings);
    expect(miacode::ui::appBackgroundIsActiveForTheme(), "decoded image activates background");
    const QImage sharp = render(background, window);
    expect(sharp.pixelColor(20, 20).red() == 0 && qAbs(sharp.pixelColor(21, 20).red() - 204) <= 1,
           "physical-pixel detail survives high-DPI rendering");
    expect(sharp.pixelColor(21, 20).alpha() == 255,
           "image opacity is applied once");

    QImage repeated = sharp;
    {
        QPainter painter(&repeated);
        background.paintBackgroundForSurface(&window, painter);
    }
    expect(repeated == sharp, "nested surfaces do not accumulate image opacity");

    for (int mode = 0; mode < 5; ++mode) {
        for (int position = 0; position < 9; ++position) {
            settings.sizeMode = static_cast<miacode::ui::AppBackgroundSizeMode>(mode);
            settings.position = static_cast<miacode::ui::AppBackgroundPosition>(position);
            background.setSettings(settings);
            const QImage whole = render(background, window);
            for (const QPoint origin : {QPoint(12, 8), QPoint(13, 9)}) {
                child.move(origin);
                const QImage part = render(background, child);
                const QImage expected = whole.copy(qRound(origin.x() * dpr), qRound(origin.y() * dpr),
                                                    part.width(), part.height());
                expect(part == expected, "child surface matches the shared canvas crop");
            }
        }
    }
    settings.sizeMode = miacode::ui::AppBackgroundSizeMode::Stretch;
    settings.opacity = 0.4;
    background.setSettings(settings);
    expect(qAbs(render(background, window).pixelColor(21, 20).red() - 102) <= 1,
           "opacity change updates cached image composition");

    source.fill(Qt::red);
    expect(source.save(path), "replace image at the same path");
    background.reloadSource();
    expect(qAbs(render(background, window).pixelColor(20, 20).red() - 102) <= 1,
           "explicit reload refreshes the same image path");
    expect(qApp->property("miacode.appBackgroundSourceRevision").toInt() == 1,
           "reload notifies the QML image consumer");
    settings.overlays.panelAlphaDark = 10;
    const QImage beforeOverlay = render(background, window);
    background.setSettings(settings);
    expect(render(background, window) == beforeOverlay, "area cover does not alter the image raster");

    background.setCanvasGeometryGlobal(QRect(window.mapToGlobal(QPoint()), window.size()));
    const QImage beforeMove = render(background, child);
    window.move(210, 240);
    background.setCanvasGeometryGlobal(QRect(window.mapToGlobal(QPoint()), window.size()));
    expect(render(background, child) == beforeMove, "moving the canvas preserves its image coordinates");

    QFile broken(path);
    expect(broken.open(QIODevice::WriteOnly | QIODevice::Truncate), "open replacement file");
    broken.write("invalid image");
    broken.close();
    background.reloadSource();
    expect(!miacode::ui::appBackgroundIsActiveForTheme(), "failed decoding restores opaque theme surfaces");
    expect(render(background, window).pixelColor(0, 0).alpha() == 0, "failed reload discards old pixels");
    expect(source.save(path), "restore valid image");
    background.reloadSource();
    expect(miacode::ui::appBackgroundIsActiveForTheme(), "valid replacement recovers after failure");

    const QJsonObject legacy{
        {QStringLiteral("blur"), 80},
        {QStringLiteral("opacity"), 0.4},
        {QStringLiteral("overlay_alpha"), QJsonObject{
            {QStringLiteral("card_dark"), 12}, {QStringLiteral("panel_dark"), 90}}}};
    const auto restored = miacode::ui::appBackgroundSettingsFromJson(legacy);
    const QJsonObject saved = miacode::ui::appBackgroundSettingsToJson(restored);
    expect(restored.opacity == 0.4 && restored.overlays.panelAlphaDark == 90,
           "legacy configuration preserves supported preferences");
    expect(!saved.contains(QStringLiteral("blur"))
               && !saved.value(QStringLiteral("overlay_alpha")).toObject().contains(QStringLiteral("card_dark")),
           "saved settings contain only configurable background properties");
    out << "AppBackground spec: DPR=" << dpr << ", failures=" << failures << '\n';
    return failures ? 1 : 0;
}
