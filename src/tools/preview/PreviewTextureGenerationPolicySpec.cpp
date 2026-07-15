#include "preview/quick_scene/PreviewTextureGenerationPolicy.h"

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
    using miacode::preview::quick_scene::previewTextureGenerationResetRequired;

    constexpr qsizetype entryLimit = 96;
    constexpr qint64 byteLimit = 96LL * 1024 * 1024;
    constexpr qsizetype fastKeyLimit = 8192;

    bool ok = true;
    ok &= expect(
        !previewTextureGenerationResetRequired(
            false, entryLimit, byteLimit, fastKeyLimit, entryLimit, byteLimit, fastKeyLimit),
        "limits are inclusive and must not reset the generation");
    ok &= expect(
        previewTextureGenerationResetRequired(
            true, 0, 0, 0, entryLimit, byteLimit, fastKeyLimit),
        "an explicit flush must reset the whole texture generation");
    ok &= expect(
        previewTextureGenerationResetRequired(
            false, entryLimit + 1, 0, 0, entryLimit, byteLimit, fastKeyLimit),
        "cached texture entry overflow must reset the generation");
    ok &= expect(
        previewTextureGenerationResetRequired(
            false, 0, byteLimit + 1, 0, entryLimit, byteLimit, fastKeyLimit),
        "cached texture byte overflow must reset the generation");
    ok &= expect(
        previewTextureGenerationResetRequired(
            false, 0, 0, fastKeyLimit + 1, entryLimit, byteLimit, fastKeyLimit),
        "fast-key overflow must reset the generation");

    if (ok) {
        QTextStream(stdout) << "preview_texture_generation_policy_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
