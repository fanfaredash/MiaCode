#include <QCoreApplication>
#include <QTextStream>

#include <optional>

#include "tools/video_export/VideoExportRuntimePolicy.h"

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

bool verifyPolicy(QTextStream& err)
{
    using miacode::video_export::shouldRequestOffscreenPboReadback;
    using miacode::video_export::shouldRetryVideoExportWorkerAfterCrash;
    using miacode::video_export::shouldUsePremultipliedExportPipe;

    if (!require(
            shouldRequestOffscreenPboReadback(std::nullopt, std::nullopt),
            QStringLiteral("default export PBO policy should be enabled when no env override is set"),
            err)) {
        return false;
    }
    if (!require(
            shouldRequestOffscreenPboReadback(true, std::nullopt),
            QStringLiteral("ENABLE=1 should keep PBO requested"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRequestOffscreenPboReadback(false, std::nullopt),
            QStringLiteral("ENABLE=0 should disable PBO"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRequestOffscreenPboReadback(true, true),
            QStringLiteral("DISABLE=1 should override ENABLE=1"),
            err)) {
        return false;
    }
    if (!require(
            shouldRetryVideoExportWorkerAfterCrash(true, true, false, 1),
            QStringLiteral("CrashExit with PBO enabled should retry once"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRetryVideoExportWorkerAfterCrash(true, false, false, 1),
            QStringLiteral("CrashExit without PBO should not retry"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRetryVideoExportWorkerAfterCrash(false, true, false, 1),
            QStringLiteral("NormalExit failure should not retry"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRetryVideoExportWorkerAfterCrash(true, true, true, 1),
            QStringLiteral("Canceled crash should not retry"),
            err)) {
        return false;
    }
    if (!require(
            !shouldRetryVideoExportWorkerAfterCrash(true, true, false, 2),
            QStringLiteral("Crash retry should happen at most once"),
            err)) {
        return false;
    }
    if (!require(
            shouldUsePremultipliedExportPipe(true, true, std::nullopt),
            QStringLiteral("fast D3D11 export should default to premultiplied transport"),
            err)) {
        return false;
    }
    if (!require(
            !shouldUsePremultipliedExportPipe(true, false, std::nullopt),
            QStringLiteral("high-quality export should keep straight RGBA transport"),
            err)) {
        return false;
    }
    if (!require(
            !shouldUsePremultipliedExportPipe(false, true, std::nullopt),
            QStringLiteral("OpenGL export should keep straight RGBA transport"),
            err)) {
        return false;
    }
    if (!require(
            !shouldUsePremultipliedExportPipe(true, true, false),
            QStringLiteral("explicit premultiplied transport opt-out should win"),
            err)) {
        return false;
    }
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyPolicy(err)) {
        return 1;
    }

    out << "video_export_runtime_policy_spec ok" << Qt::endl;
    return 0;
}
