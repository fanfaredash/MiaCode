#include "app/v2/ChartWorkspace.h"
#include "app/v2/JobProgressService.h"
#include "app/v2/MediaToolsService.h"
#include "app/v2/PreviewSurface.h"
#include "app/v2/UiRequestService.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>
#include <QTimer>
#include <QVariantMap>

#include <cmath>
#include <memory>

namespace {

class FakePreviewSurface final : public miacode::v2::PreviewSurface
{
public:
    bool beginResult = true;
    bool endResult = true;
    int beginCount = 0;
    int endCount = 0;

    bool playing() const override { return false; }
    double positionSeconds() const override { return 0.0; }
    double durationSeconds() const override { return 0.0; }
    double lowerBoundSeconds() const override { return 0.0; }
    void togglePlayback() override {}
    void stop() override {}
    void seek(double) override {}
    bool beginMediaFileOperation() override { ++beginCount; return beginResult; }
    bool endMediaFileOperation(bool) override { ++endCount; return endResult; }
    void beginScrub() override {}
    void updateScrub(double, bool) override {}
    void endScrub(double, bool) override {}
    void setPlaybackRate(double) override {}
    void nudgePlaybackRate(int) override {}
    QString playbackRateLabel() const override { return QStringLiteral("1.0x"); }
    QObject* previewRuntimeObject() const override { return nullptr; }
    QObject* stageMediaHostObject() const override { return nullptr; }
    double canvasAspectRatio() const override { return 16.0 / 9.0; }
    QStringList statsTexts() const override { return {}; }
    RenderMode muriRenderMode() const override { return RenderMode::Native; }
    void setMuriRenderMode(RenderMode) override {}
    void toggleMuriRenderMode() override {}
    QStringList availableSkinDirectoryNames() const override { return {}; }
    QString skinDisplayName(const QString&) const override { return {}; }
    QString resolveSkinDir() const override { return {}; }
    QString resolveSkinRootDir() const override { return {}; }
    QString resolveCustomOutlineDir() const override { return {}; }
    void applyOutlineVariant(PreviewOutlineVariant, bool, bool) override {}
    QVariantMap renderSettings() const override { return {}; }
    void setRenderSetting(const QString&, const QVariant&) override {}
    void refreshSurfaces() override {}
    void applySfxLevels() override {}
    void prepareForShutdown() override {}
    PreviewAudioSettings audioSettings() const override { return {}; }
    void applyAudioSettings(const PreviewAudioSettings&) override {}
    void saveAudioSettingsAsSoftwareDefault() override {}
    void restoreAudioSettingsFromSoftwareDefault() override {}
};

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) err << "FAIL: " << message << Qt::endl;
    return condition;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readBytes(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool openFixtureChart(miacode::v2::ChartWorkspace& workspace, const QString& chartPath)
{
    return workspace.openSource(
        QStringLiteral("&title=media spec\n&inote_5=(120){4}1,\n"), chartPath, 5).accepted;
}

struct MediaFixture {
    QTemporaryDir directory;
    QString chartPath;
    QString trackPath;
    QString backupPath;
    QByteArray original = QByteArrayLiteral("original-track-bytes");
    QByteArray backup = QByteArrayLiteral("backup-track-bytes");

    bool prepare()
    {
        if (!directory.isValid()) return false;
        chartPath = directory.filePath(QStringLiteral("maidata.txt"));
        trackPath = directory.filePath(QStringLiteral("track.mp3"));
        backupPath = directory.filePath(QStringLiteral("track_bak.mp3"));
        return writeBytes(chartPath, QByteArrayLiteral("&inote_5=(120){4}1,\n"))
            && writeBytes(trackPath, original) && writeBytes(backupPath, backup);
    }
};

bool verifyRestoreSuccess(QTextStream& err)
{
    MediaFixture fixture;
    if (!require(fixture.prepare(), QStringLiteral("restore fixture is prepared"), err)) return false;

    miacode::v2::ChartWorkspace workspace;
    miacode::v2::UiRequestService requests;
    miacode::v2::JobProgressService progress;
    FakePreviewSurface preview;
    miacode::v2::PreviewSurface* surface = &preview;
    miacode::v2::MediaToolsService service(workspace, requests, progress, surface);
    QSignalSpy notices(&requests, &miacode::v2::UiRequestService::noticeRequested);
    if (!require(openFixtureChart(workspace, fixture.chartPath),
                 QStringLiteral("restore fixture chart opens in the real workspace"), err)) {
        return false;
    }

    service.restoreMediaBlankBackup(true);
    return require(readBytes(fixture.trackPath) == fixture.backup,
                    QStringLiteral("restore copies the backup bytes to the target"), err)
        && require(preview.beginCount == 1 && preview.endCount == 1,
                    QStringLiteral("successful restore brackets exactly one media transaction"), err)
        && require(notices.count() == 1,
                    QStringLiteral("successful restore publishes one completion notice"), err);
}

bool verifyBeginFailureDoesNotWrite(QTextStream& err)
{
    MediaFixture fixture;
    if (!require(fixture.prepare(), QStringLiteral("begin-failure fixture is prepared"), err)) return false;

    miacode::v2::ChartWorkspace workspace;
    miacode::v2::UiRequestService requests;
    miacode::v2::JobProgressService progress;
    FakePreviewSurface preview;
    preview.beginResult = false;
    miacode::v2::PreviewSurface* surface = &preview;
    miacode::v2::MediaToolsService service(workspace, requests, progress, surface);
    if (!require(openFixtureChart(workspace, fixture.chartPath),
                 QStringLiteral("begin-failure fixture chart opens"), err)) return false;

    const QByteArray before = readBytes(fixture.trackPath);
    service.restoreMediaBlankBackup(true);
    return require(readBytes(fixture.trackPath) == before,
                    QStringLiteral("a failed begin leaves target bytes unchanged"), err)
        && require(preview.beginCount == 1 && preview.endCount == 0,
                    QStringLiteral("a failed begin never calls end"), err);
}

bool verifyEndFailureStillClosesTransaction(QTextStream& err)
{
    MediaFixture fixture;
    if (!require(fixture.prepare(), QStringLiteral("end-failure fixture is prepared"), err)) return false;

    miacode::v2::ChartWorkspace workspace;
    miacode::v2::UiRequestService requests;
    miacode::v2::JobProgressService progress;
    FakePreviewSurface preview;
    preview.endResult = false;
    miacode::v2::PreviewSurface* surface = &preview;
    miacode::v2::MediaToolsService service(workspace, requests, progress, surface);
    QSignalSpy notices(&requests, &miacode::v2::UiRequestService::noticeRequested);
    if (!require(openFixtureChart(workspace, fixture.chartPath),
                 QStringLiteral("end-failure fixture chart opens"), err)) return false;

    service.restoreMediaBlankBackup(true);
    return require(readBytes(fixture.trackPath) == fixture.backup,
                    QStringLiteral("the file write remains observable when reload is rejected"), err)
        && require(preview.beginCount == 1 && preview.endCount == 1,
                    QStringLiteral("reload failure still calls end exactly once"), err)
        && require(notices.count() == 2,
                    QStringLiteral("reload failure publishes completion and recovery notices"), err);
}

#ifndef Q_OS_WIN
bool createFfmpegStub(const QString& path)
{
    const QByteArray script =
        "#!/bin/sh\n"
        "case \"$1\" in\n"
        "  -progress)\n"
        "    printf 'out_time_us=500000\\nprogress=continue\\n'\n"
        "    sleep 5\n"
        "    exit 0\n"
        "    ;;\n"
        "  *)\n"
        "    printf 'Duration: 00:00:01.00\\n' >&2\n"
        "    exit 0\n"
        "    ;;\n"
        "esac\n";
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(script) != script.size()) return false;
    return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                               | QFileDevice::ExeOwner);
}
#endif

bool verifyFfmpegCancellationClosesTransaction(QTextStream& err)
{
#ifdef Q_OS_WIN
    // The deterministic Unix subprocess is intentionally not reproduced here,
    // but the transaction contract must still execute on Windows. A rejected
    // begin is the platform-independent cancellation/refusal fallback: no
    // bytes may change and end must not be called for an unstarted operation.
    MediaFixture fixture;
    if (!require(fixture.prepare(), QStringLiteral("Windows cancellation fixture is prepared"), err)) {
        return false;
    }
    miacode::v2::ChartWorkspace workspace;
    miacode::v2::UiRequestService requests;
    miacode::v2::JobProgressService progress;
    FakePreviewSurface preview;
    preview.beginResult = false;
    miacode::v2::PreviewSurface* surface = &preview;
    miacode::v2::MediaToolsService service(workspace, requests, progress, surface);
    if (!require(openFixtureChart(workspace, fixture.chartPath),
                 QStringLiteral("Windows cancellation fixture chart opens"), err)) {
        return false;
    }
    QSignalSpy notices(&requests, &miacode::v2::UiRequestService::noticeRequested);
    const QByteArray before = readBytes(fixture.trackPath);
    service.restoreMediaBlankBackup(true);
    const bool ok = readBytes(fixture.trackPath) == before
        && preview.beginCount == 1 && preview.endCount == 0
        && !progress.active() && notices.count() == 1
        && notices.at(0).at(1).toMap().value(QStringLiteral("severity")).toString()
            == QStringLiteral("error");
    return require(ok,
                   QStringLiteral("Windows transaction refusal leaves bytes unchanged and keeps begin/end pairing"),
                   err);
#else
    MediaFixture fixture;
    if (!require(fixture.prepare(), QStringLiteral("cancellation fixture is prepared"), err)) return false;
    const QString ffmpegPath = fixture.directory.filePath(QStringLiteral("ffmpeg-stub"));
    if (!require(createFfmpegStub(ffmpegPath), QStringLiteral("deterministic ffmpeg stub is executable"), err)) {
        return false;
    }

    const QByteArray oldFfmpeg = qgetenv("MIACODE_FFMPEG_PATH");
    const bool hadOldFfmpeg = qEnvironmentVariableIsSet("MIACODE_FFMPEG_PATH");
    qputenv("MIACODE_FFMPEG_PATH", QFile::encodeName(ffmpegPath));

    miacode::v2::ChartWorkspace workspace;
    miacode::v2::UiRequestService requests;
    miacode::v2::JobProgressService progress;
    FakePreviewSurface preview;
    miacode::v2::PreviewSurface* surface = &preview;
    miacode::v2::MediaToolsService service(workspace, requests, progress, surface);
    bool cancelScheduled = false;
    QObject::connect(&progress, &miacode::v2::JobProgressService::changed, &progress, [&]() {
        if (!cancelScheduled && progress.active() && progress.percent() > 0) {
            cancelScheduled = true;
            QTimer::singleShot(0, &progress, &miacode::v2::JobProgressService::requestCancel);
        }
    });
    bool ok = openFixtureChart(workspace, fixture.chartPath);
    QSignalSpy notices(&requests, &miacode::v2::UiRequestService::noticeRequested);
    const QByteArray before = readBytes(fixture.trackPath);
    service.convertTrackTo44100Hz();
    const QString requestId = notices.count() == 1 ? notices.at(0).at(0).toString() : QString();
    const int noticesBeforeAnswer = notices.count();
    requests.submitNoticeResult(requestId, true);
    const bool cancellationNoticePublished = notices.count() == noticesBeforeAnswer + 1;
    QVariantMap cancellationNotice;
    if (cancellationNoticePublished) {
        cancellationNotice = notices.at(noticesBeforeAnswer).at(1).toMap();
    }
    ok = ok && cancellationNoticePublished
        && cancellationNotice.value(QStringLiteral("severity")).toString()
            == QStringLiteral("information")
        && !cancellationNotice.value(QStringLiteral("text")).toString().isEmpty()
        && !cancellationNotice.value(QStringLiteral("confirmation")).toBool()
        && cancellationNotice.value(QStringLiteral("actionLabel")).toString().isEmpty()
        && notices.at(noticesBeforeAnswer).at(0).toString().isEmpty()
        && readBytes(fixture.trackPath) == before
        && preview.beginCount == 1 && preview.endCount == 1
        && cancelScheduled && !progress.active();

    if (hadOldFfmpeg) qputenv("MIACODE_FFMPEG_PATH", oldFfmpeg);
    else qunsetenv("MIACODE_FFMPEG_PATH");
    return require(ok, QStringLiteral("FFmpeg cancellation ends once without replacing the target"), err);
#endif
}

bool verifyLateConfirmationAfterServiceDestruction(QTextStream& err)
{
    MediaFixture fixture;
    if (!require(fixture.prepare(), QStringLiteral("late-confirmation fixture is prepared"), err)) return false;

    miacode::v2::ChartWorkspace workspace;
    miacode::v2::UiRequestService requests;
    miacode::v2::JobProgressService progress;
    FakePreviewSurface preview;
    miacode::v2::PreviewSurface* surface = &preview;
    QSignalSpy notices(&requests, &miacode::v2::UiRequestService::noticeRequested);
    if (!require(openFixtureChart(workspace, fixture.chartPath),
                 QStringLiteral("late-confirmation fixture chart opens"), err)) return false;

    QString requestId;
    bool queuedResponseDelivered = false;
    const QByteArray before = readBytes(fixture.trackPath);
    {
        auto service = std::make_unique<miacode::v2::MediaToolsService>(
            workspace, requests, progress, surface);
        service->convertTrackTo44100Hz();
        if (!require(notices.count() == 1 && requests.pendingNoticeCount() == 1,
                     QStringLiteral("conversion leaves one confirmation pending"), err)) return false;
        requestId = notices.at(0).at(0).toString();

        // QML answers a request on a later event-loop turn. Keep that timing
        // here so the QPointer guard is exercised against a genuinely queued
        // callback rather than a synchronous response.
        QTimer::singleShot(0, &requests, [&requests, requestId, &queuedResponseDelivered]() {
            queuedResponseDelivered = true;
            requests.submitNoticeResult(requestId, true);
        });
    }

    QCoreApplication::processEvents();
    return require(queuedResponseDelivered,
                    QStringLiteral("the late confirmation answer is delivered through the event loop"), err)
        && require(readBytes(fixture.trackPath) == before,
                    QStringLiteral("a late confirmation callback is a no-op after service destruction"), err)
        && require(preview.beginCount == 0 && preview.endCount == 0 && !progress.active(),
                    QStringLiteral("late callback does not start a transaction or progress job"), err)
        && require(notices.count() == 1,
                    QStringLiteral("late callback does not publish a completion or recovery notice"), err)
        && require(requests.pendingNoticeCount() == 0,
                    QStringLiteral("the real request service consumes the late response once"), err);
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    const bool ok = verifyRestoreSuccess(err)
        && verifyBeginFailureDoesNotWrite(err)
        && verifyEndFailureStillClosesTransaction(err)
        && verifyFfmpegCancellationClosesTransaction(err)
        && verifyLateConfirmationAfterServiceDestruction(err);
    if (ok) {
        QTextStream out(stdout);
        out << "media_tools_service_spec ok" << Qt::endl;
    }
    return ok ? 0 : 1;
}
