#include "timeline/quick/TimelineQuickTextureCachePolicy.h"

#include <QCoreApplication>
#include <QTextStream>

namespace {

bool expect(bool condition, const char* message)
{
    if (condition) {
        return true;
    }
    QTextStream(stderr) << "FAIL: " << message << Qt::endl;
    return false;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    using miacode::timeline::quick::timelineTextureCacheFlushRequired;

    constexpr qsizetype textureLimit = 8192;
    constexpr qint64 byteLimit = 64LL * 1024 * 1024;
    constexpr qsizetype pixmapLimit = 8192;

    bool ok = true;
    ok &= expect(
        !timelineTextureCacheFlushRequired(0, 0, 0, textureLimit, byteLimit, pixmapLimit),
        "an empty cache must not request a flush");
    ok &= expect(
        !timelineTextureCacheFlushRequired(
            textureLimit, byteLimit, pixmapLimit, textureLimit, byteLimit, pixmapLimit),
        "limits are inclusive and must not request a flush");
    ok &= expect(
        timelineTextureCacheFlushRequired(
            textureLimit + 1, 0, 0, textureLimit, byteLimit, pixmapLimit),
        "texture entry overflow must request a flush");
    ok &= expect(
        timelineTextureCacheFlushRequired(
            0, byteLimit + 1, 0, textureLimit, byteLimit, pixmapLimit),
        "texture byte overflow must request a flush");
    // The pixmap arm matters on its own: transformedPixmaps_ is the CPU-side twin of
    // the note textures and is not covered by the partial theme invalidation, so it
    // can be over the cap while the texture count still looks healthy.
    ok &= expect(
        timelineTextureCacheFlushRequired(
            0, 0, pixmapLimit + 1, textureLimit, byteLimit, pixmapLimit),
        "transformed-pixmap entry overflow must request a flush on its own");

    if (ok) {
        QTextStream(stdout) << "timeline_quick_texture_cache_policy_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
