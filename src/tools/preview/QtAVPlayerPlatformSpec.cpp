#include <QFile>
#include <QStringList>
#include <QTextStream>

namespace {

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

QString sourceFile(const char* relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/')
               + QLatin1String(relativePath));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool containsAll(const QString& source, const QStringList& needles)
{
    for (const QString& needle : needles) {
        if (!source.contains(needle)) {
            return false;
        }
    }
    return true;
}

}  // namespace

int main()
{
    QTextStream err(stderr);
    QTextStream out(stdout);
    bool ok = true;

    const QString cmake = sourceFile("CMakeLists.txt");
    const QString qtavCmake = sourceFile(
        "third_party/QtAVPlayer/src/QtAVPlayer/QtAVPlayer.cmake");
    const QString videoToolbox = sourceFile(
        "third_party/QtAVPlayer/src/QtAVPlayer/qavhwdevice_videotoolbox.mm");
    const QString videoFrame = sourceFile(
        "third_party/QtAVPlayer/src/QtAVPlayer/qavvideoframe.cpp");
    const QString backend = sourceFile(
        "src/preview/runtime/PreviewStageMediaHost_Backend.cpp");
    const QString playback = sourceFile(
        "src/preview/runtime/PreviewStageMediaHost_Playback.cpp");
    const QString diagnostics = sourceFile(
        "src/preview/runtime/PreviewStageMediaHost_Diagnostics.cpp");
    const QString sharedDevice = sourceFile(
        "src/preview/runtime/PreviewSharedD3D11Device.cpp");
    const QString packageMac = sourceFile("scripts/build/package-mac.sh");

    ok &= require(!cmake.isEmpty() && !qtavCmake.isEmpty() && !videoToolbox.isEmpty() && !videoFrame.isEmpty() && !backend.isEmpty() && !playback.isEmpty()
                      && !diagnostics.isEmpty() && !sharedDevice.isEmpty() && !packageMac.isEmpty(),
                  QStringLiteral("QtAVPlayer platform sources are readable"), err);

    ok &= require(cmake.contains(QStringLiteral(
                      "if (WIN32 OR APPLE)\n    find_package(Qt6 6.8 REQUIRED COMPONENTS MultimediaQuickPrivate)")),
                  QStringLiteral("QtAVPlayer private Qt Multimedia bridge covers Windows and macOS"), err);
    const qsizetype qtavBlock = cmake.indexOf(QStringLiteral("# QtAVPlayer (FFmpeg)"));
    ok &= require(qtavBlock >= 0
                      && cmake.indexOf(QStringLiteral("if (WIN32 OR APPLE)"), qtavBlock) >= 0,
                  QStringLiteral("QtAVPlayer CMake integration covers Windows and macOS"), err);
    ok &= require(containsAll(cmake, {
                      QStringLiteral("MIACODE_USE_QTAVPLAYER=1"),
                      QStringLiteral("QT_AVPLAYER_MULTIMEDIA"),
                      QStringLiteral("Qt6::MultimediaQuickPrivate"),
                      QStringLiteral("MIACODE_FFMPEG_DEV_DIR"),
                  }),
                  QStringLiteral("both platforms build the QVideoFrame bridge from FFmpeg"), err);
    ok &= require(qtavCmake.contains(QStringLiteral("if(APPLE)"))
                      && qtavCmake.contains(QStringLiteral("qavhwdevice_videotoolbox.mm")),
                  QStringLiteral("QtAVPlayer Apple VideoToolbox source remains enabled"), err);
    ok &= require(containsAll(videoToolbox, {
                      QStringLiteral("if (d->pbuf)\n        CVPixelBufferRelease(d->pbuf);"),
                      QStringLiteral("if (m_hw->pbuf)\n            CVPixelBufferRelease(m_hw->pbuf);"),
                  }),
                  QStringLiteral("VideoToolbox pixel buffers are never released before acquisition"), err);
    ok &= require(containsAll(videoToolbox, {
                      QStringLiteral("kCVPixelFormatType_420YpCbCr8BiPlanarFullRange"),
                      QStringLiteral("kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange"),
                      QStringLiteral("kCVPixelFormatType_420YpCbCr10BiPlanarFullRange"),
                      QStringLiteral("MTLPixelFormatR16Unorm"),
                      QStringLiteral("MTLPixelFormatRG16Unorm"),
                      QStringLiteral("if (!surface || planes != 2)"),
                      QStringLiteral("releaseTextureObjects"),
                      QStringLiteral("m_textureObjects[2]"),
                      QStringLiteral("m_texturesReady"),
                  }) && containsAll(videoFrame, {
                      QStringLiteral("videoToolboxPixelFormat"),
                      QStringLiteral("kCVPixelFormatType_420YpCbCr10BiPlanarVideoRange"),
                      QStringLiteral("QVideoFrameFormat::Format_P010"),
                  }),
                  QStringLiteral("VideoToolbox maps 8-bit and 10-bit bi-planar frames to matching Metal and Qt formats"), err);
    ok &= require(containsAll(packageMac, {
                      QStringLiteral("MIACODE_FFMPEG_DEV_DIR"),
                      QStringLiteral("ffmpeg@6"),
                      QStringLiteral("-DMIACODE_FFMPEG_DEV_DIR="),
                  }),
                  QStringLiteral("macOS package build resolves and forwards the FFmpeg development SDK"), err);
    ok &= require(containsAll(packageMac, {
                      QStringLiteral("bundle_ffmpeg_dylib_closure"),
                      QStringLiteral("resolve_ffmpeg_dylib_source"),
                      QStringLiteral("strip_ffmpeg_build_rpath"),
                      QStringLiteral("verify_no_external_ffmpeg_dylib_references"),
                      QStringLiteral("validate_bundled_ffmpeg_minos"),
                      QStringLiteral("install_name_tool -delete_rpath"),
                      QStringLiteral("install_name_tool -change"),
                      QStringLiteral("--parallel 2"),
                  }),
                  QStringLiteral("macOS package embeds the FFmpeg dylib closure with bounded build parallelism"), err);

    ok &= require(backend.contains(QStringLiteral("#if defined(Q_OS_WIN)"))
                      && backend.contains(QStringLiteral("qavd3d11sharedcontext_p.h")),
                  QStringLiteral("D3D11 shared-context include is platform guarded"), err);
    ok &= require(containsAll(backend, {
                      QStringLiteral("hardware_decoder=d3d11va"),
                      QStringLiteral("hardware_decoder=videotoolbox"),
                      QStringLiteral("#elif defined(Q_OS_MACOS)"),
                  }),
                  QStringLiteral("backend diagnostics identify the active Windows or macOS hardware decoder"), err);
    ok &= require(diagnostics.contains(QStringLiteral("#if defined(Q_OS_WIN) && defined(MIACODE_USE_QTAVPLAYER)")),
                  QStringLiteral("D3D11 diagnostics are platform guarded"), err);
    ok &= require(sharedDevice.contains(QStringLiteral("#if defined(Q_OS_WIN) && defined(MIACODE_USE_QTAVPLAYER)")),
                  QStringLiteral("shared D3D11 device implementation is Windows-only"), err);
    const qsizetype commitStart = playback.indexOf(
        QStringLiteral("void PreviewStageMediaHost::commitPreparedPlaybackStart"));
    const qsizetype commitEnd = playback.indexOf(
        QStringLiteral("void PreviewStageMediaHost::cancelPreparedPlaybackStart"), commitStart);
    const QString commit = commitStart >= 0 && commitEnd >= 0
        ? playback.mid(commitStart, commitEnd - commitStart)
        : QString();
    ok &= require(containsAll(commit, {
                      QStringLiteral("lastSeekMs_ = targetMs;\n    player_->seek(targetMs);"),
                      QStringLiteral("lastSeekMs_ = targetMs;\n    player_->setPosition(targetMs);"),
                  }),
                  QStringLiteral("prepared playback commit always seeks before starting either backend"), err);

    if (ok) {
        out << "QtAVPlayer platform spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
