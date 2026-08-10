// Source-level contract for PreviewStageMediaHost PV-memory evidence wiring.
// This deliberately verifies hook placement rather than instantiating the
// multimedia host: the host needs platform backends and QML output objects.

#include <QFile>
#include <QRegularExpression>
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
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + QLatin1String(relativePath));
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

    const QString header = sourceFile("src/preview/runtime/PreviewStageMediaHost.h");
    const QString host = sourceFile("src/preview/runtime/PreviewStageMediaHost.cpp");
    const QString media = sourceFile("src/preview/runtime/PreviewStageMediaHost_Media.cpp");
    const QString backend = sourceFile("src/preview/runtime/PreviewStageMediaHost_Backend.cpp");
    const QString playback = sourceFile("src/preview/runtime/PreviewStageMediaHost_Playback.cpp");
    const QString diagnostics = sourceFile("src/preview/runtime/PreviewStageMediaHost_Diagnostics.cpp");

    ok &= require(!header.isEmpty() && !host.isEmpty() && !media.isEmpty() && !backend.isEmpty()
                      && !playback.isEmpty() && !diagnostics.isEmpty(),
                  QStringLiteral("all host implementation sources are readable"), err);

    // The periodic timer must not exist or arm outside runtime --debug. The
    // explicit gate prevents passive PV capture during ordinary editor use.
    ok &= require(containsAll(header, {QStringLiteral("schedulePvMemoryPeriodicSample"),
                                       QStringLiteral("pvMemoryDiagnostics_")})
                      && containsAll(diagnostics, {QStringLiteral("debugModeEnabled()"),
                                                    QStringLiteral("hasCurrentSource()"),
                                                    QStringLiteral("kPvMemoryPeriodicSampleMs")}),
                  QStringLiteral("debug-off cannot arm periodic PV-memory sampling"), err);

    const QRegularExpression toImageCall(QStringLiteral(R"REGEX(\.toImage\s*\()REGEX"));
    ok &= require(diagnostics.count(toImageCall) == 1,
                  QStringLiteral("diagnostics contains exactly one toImage call"), err);

    ok &= require(containsAll(media, {QStringLiteral("beginPvMemorySource"),
                                      QStringLiteral("loadVideoMedia"),
                                      QStringLiteral("clearPvMemorySource"),
                                      QStringLiteral("releaseDecoderForFileReplace")}),
                  QStringLiteral("source load and clear/file-replace hooks are present"), err);
    ok &= require(containsAll(diagnostics, {QStringLiteral("observePvMemoryFrame"),
                                            QStringLiteral("QElapsedTimer toImageTimer"),
                                            QStringLiteral("decodedImage.sizeInBytes()")}),
                  QStringLiteral("first frame and existing toImage measurement are recorded"), err);
    ok &= require(containsAll(playback, {QStringLiteral("PvMemoryBoundary::Play"),
                                         QStringLiteral("PvMemoryBoundary::Pause")}),
                  QStringLiteral("play and pause boundaries are recorded"), err);
    ok &= require(containsAll(host, {QStringLiteral("PvMemoryBoundary::OutputAttach"),
                                     QStringLiteral("PvMemoryBoundary::OutputDetach"),
                                     QStringLiteral("destroyPvMemorySource")}),
                  QStringLiteral("output and shutdown/destructor boundaries are recorded"), err);
    ok &= require(containsAll(backend, {QStringLiteral("PvMemoryBoundary::EndOfMedia"),
                                        QStringLiteral("latePvMemoryNoMedia"),
                                        QStringLiteral("PlayerDestroyBefore"),
                                        QStringLiteral("PlayerDestroyAfter")}),
                  QStringLiteral("end/no-media and recovery destroy boundaries are recorded"), err);
    ok &= require(containsAll(diagnostics, {QStringLiteral("postClearPvMemoryCheckpoint"),
                                            QStringLiteral("kPostClear3SecondsMs"),
                                            QStringLiteral("kPostClear15SecondsMs"),
                                            QStringLiteral("QTimer::singleShot")}),
                  QStringLiteral("epoch-cancelled delayed post-clear callbacks are present"), err);

    // This channel is intentionally the entire feature surface: no runtime or
    // audio PV-memory records and no file path/frame/external-process probes.
    const QString joined = host + media + backend + playback + diagnostics;
    ok &= require(joined.contains(QStringLiteral("Channel::PvMemory"))
                      && !joined.contains(QStringLiteral("Channel::Runtime,\n        QStringLiteral(\"pv_memory"))
                      && !joined.contains(QStringLiteral("Channel::Audio,\n        QStringLiteral(\"pv_memory"))
                      && !joined.contains(QStringLiteral("QProcess")),
                  QStringLiteral("PV-memory records are dedicated and do not spawn external probes"), err);

    if (ok) {
        out << "PV-memory host contract spec passed." << Qt::endl;
    }
    return ok ? 0 : 1;
}
