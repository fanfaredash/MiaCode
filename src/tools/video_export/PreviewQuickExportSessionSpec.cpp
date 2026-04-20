#include <QGuiApplication>
#include <QImage>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSurfaceFormat>
#include <QTextStream>

#include "preview/runtime/PreviewQuickExportSession.h"
#include "preview/scene/PreviewFrameState.h"
#include "preview/scene/PreviewLayerOrder.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

quint64 imageSignature(const QImage& image)
{
    constexpr quint64 kOffset = 1469598103934665603ULL;
    constexpr quint64 kPrime = 1099511628211ULL;

    quint64 hash = kOffset;
    const uchar* bytes = image.constBits();
    const qsizetype byteCount = image.sizeInBytes();
    for (qsizetype index = 0; index < byteCount; ++index) {
        hash ^= static_cast<quint64>(bytes[index]);
        hash *= kPrime;
    }
    return hash;
}

bool verifyPerFrameTickRendering(QTextStream& err)
{
    PreviewQuickExportSession session;
    session.setLayerFlags(miacode::preview::scene::kPreviewExportOverlayRenderLayers);
    session.setFrameSize(QSize(320, 180));

    miacode::preview::scene::PreviewFrameState state;
    state.render.showTimestamp = false;
    state.render.showDebugInfo = false;
    state.render.showObjectStatsHud = false;
    state.sceneContentRevision = 1;
    session.setFrameState(state);

    QSurfaceFormat format;
    format.setRenderableType(QSurfaceFormat::OpenGL);
    QString errorMessage;
    if (!session.initialize(format, nullptr, &errorMessage)) {
        err << "failed to initialize export session: " << errorMessage << Qt::endl;
        return false;
    }

    const QImage frame0 = session.renderFrame(&errorMessage);
    if (!require(!frame0.isNull(), QStringLiteral("first export frame should render after one-time initialization"), err)) {
        return false;
    }

    session.applyExportFrameTick(1.25, true, false, true, 0, 0.0);
    const QImage frame1 = session.renderFrame(&errorMessage);
    if (!require(!frame1.isNull(), QStringLiteral("per-frame tick should render without a second full-state sync"), err)) {
        return false;
    }

    session.applyExportFrameTick(2.50, true, false, true, 1, 60.0);
    const QImage frame2 = session.renderFrame(&errorMessage);
    if (!require(!frame2.isNull(), QStringLiteral("subsequent per-frame ticks should continue rendering"), err)) {
        return false;
    }

    if (!require(
            session.frameState().sceneContentRevision == 1,
            QStringLiteral("per-frame export ticks must not mutate sceneContentRevision"),
            err)) {
        return false;
    }
    if (!require(
            qFuzzyCompare(session.frameState().playheadSeconds, 2.50),
            QStringLiteral("per-frame export tick should update playheadSeconds in-place"),
            err)) {
        return false;
    }
    if (!require(
            session.frameState().render.showTimestamp,
            QStringLiteral("per-frame export tick should update the timestamp visibility flag"),
            err)) {
        return false;
    }
    if (!require(
            session.frameState().cpuFallbackCount == 1,
            QStringLiteral("per-frame export tick should update cpuFallbackCount in-place"),
            err)) {
        return false;
    }
    if (!require(
            qFuzzyCompare(session.frameState().fpsDisplay, 60.0),
            QStringLiteral("per-frame export tick should update fpsDisplay in-place"),
            err)) {
        return false;
    }

    const quint64 frame0Signature = imageSignature(frame0);
    const quint64 frame1Signature = imageSignature(frame1);
    const quint64 frame2Signature = imageSignature(frame2);
    if (!require(
            frame0Signature != frame1Signature,
            QStringLiteral("enabling timestamp output via per-frame tick should change the rendered frame"),
            err)) {
        return false;
    }
    if (!require(
            frame1Signature != frame2Signature,
            QStringLiteral("changing playheadSeconds via per-frame tick should change the rendered frame"),
            err)) {
        return false;
    }

    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    qputenv("QSG_RHI_BACKEND", QByteArrayLiteral("opengl"));
    QQuickWindow::setGraphicsApi(QSGRendererInterface::OpenGL);
    QGuiApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyPerFrameTickRendering(err)) {
        return 1;
    }

    out << "preview_quick_export_session_spec ok" << Qt::endl;
    return 0;
}
