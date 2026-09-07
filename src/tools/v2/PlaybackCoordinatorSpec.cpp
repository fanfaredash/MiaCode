// Boundary regression for stages 4.8, 4.9a and 4.9b.
//
// The coordinator is the only playback authority. Preview and Timeline
// compatibility surfaces are separate projection adapters and must not make
// the coordinator depend on either UI surface contract. RuntimeContext is the
// transitional storage boundary while the remaining per-domain state split is
// completed in the later 4.9 work.

#include <QCoreApplication>
#include <QFile>
#include <QStringList>
#include <QString>
#include <QTextStream>

#include "app/runtime/playback/PlaybackIdentityGate.h"

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined (repo root absolute path)"
#endif

namespace {

QString readSource(const QString& relativePath)
{
    QFile file(QStringLiteral(MIACODE_SOURCE_ROOT) + QLatin1Char('/') + relativePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll());
}

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool verifyCoordinatorOwnsOnlyPlaybackContracts(QTextStream& err)
{
    const QString header = readSource(QStringLiteral("src/app/runtime/playback/PlaybackCoordinator.h"));
    bool ok = require(!header.isEmpty(),
                      QStringLiteral("PlaybackCoordinator.h is present"), err);
    ok &= require(header.contains(QStringLiteral("class PlaybackCoordinator"))
                      && header.contains(QStringLiteral("miacode::v2::PlaybackControl"))
                      && header.contains(QStringLiteral("miacode::v2::PreviewPlaybackPort"))
                      && header.contains(QStringLiteral("miacode::v2::AudioClockSource")),
                  QStringLiteral("the coordinator implements the three narrow playback contracts"), err);
    ok &= require(!header.contains(QStringLiteral("PreviewSurface.h"))
                      && !header.contains(QStringLiteral("TimelineSurface.h"))
                      && !header.contains(QStringLiteral("public miacode::v2::PreviewSurface"))
                      && !header.contains(QStringLiteral("public miacode::v2::TimelineSurface")),
                  QStringLiteral("the coordinator has no direct Preview or Timeline surface dependency"), err);
    ok &= require(!header.contains(QStringLiteral("QQuick"))
                      && !header.contains(QStringLiteral("QSG")),
                  QStringLiteral("the coordinator header has no direct QML or scene-graph dependency"), err);
    return ok;
}

bool verifyRuntimeContextBoundary(QTextStream& err)
{
    const QString coordinator = readSource(
        QStringLiteral("src/app/runtime/playback/PlaybackCoordinator.h"));
    const QString context = readSource(QStringLiteral("src/app/runtime/RuntimeContext.h"));
    const QString members = readSource(QStringLiteral("src/app/runtime/SessionMembers.inc"));
    const QString session = readSource(QStringLiteral("src/app/runtime/Session.h"));
    bool ok = require(!coordinator.isEmpty() && !context.isEmpty() && !members.isEmpty()
                          && !session.isEmpty(),
                      QStringLiteral("runtime context boundary sources are readable"), err);
    ok &= require(coordinator.contains(QStringLiteral("runtime/RuntimeContext.h"))
                      && !coordinator.contains(QStringLiteral("runtime/Session.h")),
                  QStringLiteral("PlaybackCoordinator.h depends on RuntimeContext, not Session.h"), err);
    ok &= require(!coordinator.contains(QStringLiteral("Session::HostUi"))
                      && !coordinator.contains(QStringLiteral("Session::HostState")),
                  QStringLiteral("the coordinator header has no Session-owned UI/state type dependency"), err);
    ok &= require(context.contains(QStringLiteral("class RuntimeContext"))
                      && context.contains(QStringLiteral("MIACODE_RUNTIME_CONTEXT_TYPES"))
                      && members.contains(QStringLiteral("struct Ui"))
                      && members.contains(QStringLiteral("struct State"))
                      && !members.contains(QStringLiteral("struct HostUi"))
                      && !members.contains(QStringLiteral("struct HostState")),
                  QStringLiteral("RuntimeContext injects the transitional Ui and State records"), err);
    ok &= require(session.contains(QStringLiteral("runtime/RuntimeContext.h"))
                      && session.contains(QStringLiteral("MIACODE_SESSION_RUNTIME_MEMBERS")),
                  QStringLiteral("Session includes RuntimeContext and only injects its runtime members"), err);

    const QStringList hostHeaders{
        QStringLiteral("src/app/runtime/document/DocumentSessionHost.h"),
        QStringLiteral("src/app/runtime/editor/EditorHost.h"),
        QStringLiteral("src/app/runtime/export/VideoExportHost.h"),
        QStringLiteral("src/app/runtime/media/MediaJobsHost.h"),
        QStringLiteral("src/app/runtime/preview/StageMediaHost.h"),
        QStringLiteral("src/app/runtime/settings/SettingsHost.h"),
        QStringLiteral("src/app/runtime/validation/ValidationHost.h"),
    };
    for (const QString& hostPath : hostHeaders) {
        const QString host = readSource(hostPath);
        ok &= require(!host.isEmpty(), hostPath + QStringLiteral(" is readable"), err);
        ok &= require(!host.contains(QStringLiteral("Session::HostUi"))
                          && !host.contains(QStringLiteral("Session::HostState"))
                          && host.contains(QStringLiteral("RuntimeContext::Ui"))
                          && host.contains(QStringLiteral("RuntimeContext::State")),
                      hostPath + QStringLiteral(" uses explicit RuntimeContext types"), err);
    }

    const QString shellHost = readSource(QStringLiteral("src/app/runtime/shell/ShellHost.h"));
    ok &= require(!shellHost.isEmpty()
                      && !shellHost.contains(QStringLiteral("RuntimeContext::Ui"))
                      && !shellHost.contains(QStringLiteral("RuntimeContext::State"))
                      && !shellHost.contains(QStringLiteral("QWidget"))
                      && shellHost.contains(QStringLiteral("QWindow"))
                      && shellHost.contains(QStringLiteral("requestShellClose")),
                  QStringLiteral("ShellHost is a QML lifecycle bridge without Widget state"), err);
    return ok;
}

bool verifyRuntimeContextOutlivesHosts(QTextStream& err)
{
    const QString session = readSource(QStringLiteral("src/app/runtime/Session.h"));
    const qsizetype contextPosition = session.indexOf(
        QStringLiteral("miacode::runtime::RuntimeContext runtimeContext_;"));
    const qsizetype firstHostPosition = session.indexOf(
        QStringLiteral("std::unique_ptr<miacode::runtime::EditorHost> editor_;"));
    return require(contextPosition >= 0 && firstHostPosition >= 0
                       && contextPosition < firstHostPosition,
                   QStringLiteral("RuntimeContext is declared before every borrowing host"), err);
}

bool verifyTimelineStorageIsConstructedBeforeItsAliases(QTextStream& err)
{
    // Stage 4.9b. RuntimeContextBoundarySpec compile-asserts that State only
    // aliases the timeline record; a compile-time assertion cannot see member
    // ORDER, and a reordered context would bind those aliases into storage that
    // has not been constructed yet. Same hazard class as the 4.9a host-order
    // check above, so it is guarded the same way.
    //
    // Stage 4.9e-4 added PlaybackState alongside TimelineState as a second
    // record State borrows by reference (canonical playback-authority
    // storage). Same hazard, same check, extended to the new record.
    const QString context = readSource(QStringLiteral("src/app/runtime/RuntimeContext.h"));
    const qsizetype timelinePosition =
        context.indexOf(QStringLiteral("    TimelineState timeline;"));
    const qsizetype playbackPosition =
        context.indexOf(QStringLiteral("    PlaybackState playback;"));
    const qsizetype statePosition = context.indexOf(QStringLiteral("    State state;"));
    bool ok = require(timelinePosition >= 0 && statePosition >= 0
                          && timelinePosition < statePosition,
                      QStringLiteral("TimelineState is declared before the State record aliasing it"), err);
    ok &= require(playbackPosition >= 0 && statePosition >= 0
                      && playbackPosition < statePosition,
                  QStringLiteral("PlaybackState is declared before the State record aliasing it"), err);
    ok &= require(context.contains(QStringLiteral(": state(timeline, playback)")),
                  QStringLiteral("RuntimeContext injects the timeline and playback records into State"), err);
    ok &= require(context.contains(QStringLiteral("RuntimeContext(const RuntimeContext&) = delete;")),
                  QStringLiteral("RuntimeContext is non-copyable so aliases cannot outlive their record"), err);
    return ok;
}

bool verifyProjectionAdaptersOwnLegacySurfaceContracts(QTextStream& err)
{
    const QString adapters = readSource(
        QStringLiteral("src/app/runtime/playback/PlaybackSurfaceAdapters.h"));
    bool ok = require(!adapters.isEmpty(),
                      QStringLiteral("legacy surface adapters are present"), err);
    ok &= require(adapters.contains(QStringLiteral("PreviewSurface"))
                      && adapters.contains(QStringLiteral("TimelineSurface"))
                      && adapters.contains(QStringLiteral("PlaybackCoordinator")),
                  QStringLiteral("surface compatibility is isolated behind explicit adapters"), err);
    return ok;
}

bool verifyAssemblyNoLongerUsesPlaybackControlAdapter(QTextStream& err)
{
    const QString bootstrap = readSource(QStringLiteral("src/app/runtime/SessionBootstrap.cpp"));
    const QString cmake = readSource(QStringLiteral("CMakeLists.txt"));
    bool ok = require(!bootstrap.isEmpty() && !cmake.isEmpty(),
                      QStringLiteral("coordinator assembly sources are readable"), err);
    ok &= require(!bootstrap.contains(QStringLiteral("PlaybackControlAdapter"))
                      && !cmake.contains(QStringLiteral("PlaybackControlAdapter.cpp")),
                  QStringLiteral("the old playback adapter is removed from production assembly"), err);
    return ok;
}

bool verifyPlaybackIdentityGateBehavior(QTextStream& err)
{
    miacode::runtime::PlaybackIdentityGate gate(41);
    gate.setDocumentRevision(9);
    const miacode::v2::PlaybackCallbackStamp initial = gate.currentStamp();
    bool ok = require(gate.active()
                          && initial.sessionGeneration == 41
                          && initial.documentRevision == 9
                          && initial.playbackSequence == 0,
                      QStringLiteral("the identity gate starts with an explicit session and revision"), err);

    ok &= require(gate.advanceSequence() == 1,
                  QStringLiteral("one playback command advances exactly one sequence"), err);
    const miacode::v2::PlaybackCallbackStamp current = gate.currentStamp();
    ok &= require(gate.accepts(current),
                  QStringLiteral("the current callback stamp is accepted"), err);
    ok &= require(!gate.accepts(initial),
                  QStringLiteral("a stale command sequence is rejected"), err);

    const miacode::v2::PlaybackCallbackStamp wrongRevision{41, 10, 1};
    const miacode::v2::PlaybackCallbackStamp wrongGeneration{42, 9, 1};
    ok &= require(!gate.accepts(wrongRevision) && !gate.accepts(wrongGeneration),
                  QStringLiteral("revision and generation mismatches are rejected"), err);

    gate.invalidate();
    const miacode::v2::PlaybackCallbackStamp invalidated = gate.currentStamp();
    ok &= require(!gate.active()
                      && invalidated.sessionGeneration == 42
                      && invalidated.playbackSequence == 2
                      && !gate.accepts(invalidated)
                      && gate.advanceSequence() == 2,
                  QStringLiteral("invalidation rejects callbacks and freezes command sequencing"), err);
    gate.setDocumentRevision(11);
    ok &= require(!gate.active() && !gate.accepts(gate.currentStamp()),
                  QStringLiteral("a revision update cannot reactivate an invalidated session"), err);
    return ok;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;
    ok &= verifyCoordinatorOwnsOnlyPlaybackContracts(err);
    ok &= verifyRuntimeContextBoundary(err);
    ok &= verifyRuntimeContextOutlivesHosts(err);
    ok &= verifyTimelineStorageIsConstructedBeforeItsAliases(err);
    ok &= verifyProjectionAdaptersOwnLegacySurfaceContracts(err);
    ok &= verifyAssemblyNoLongerUsesPlaybackControlAdapter(err);
    ok &= verifyPlaybackIdentityGateBehavior(err);
    if (ok) {
        QTextStream(stdout) << "playback_coordinator_spec: OK" << Qt::endl;
    }
    return ok ? 0 : 1;
}
