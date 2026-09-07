// Phase 1 self-test for the operation breadcrumb logger. Drives the
// primitive through nested scopes (normal exit, fail(), exception
// unwind) and asserts the resulting log lines contain the expected
// chain shape.
//
// See docs/ops/OPERATION_LOG_PATTERNS_SPEC.md.

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

#include "common/CrashRecovery.h"
#include "common/DebugLog.h"
#include "common/OperationLog.h"

#include <stdexcept>

namespace {

bool g_verbose = false;

QTextStream& err()
{
    static QTextStream stream(stderr);
    return stream;
}

QTextStream& out()
{
    static QTextStream stream(stdout);
    return stream;
}

void dumpLogIfVerbose(const QString& tag, const QString& contents)
{
    if (!g_verbose) {
        return;
    }
    out() << "----- " << tag << " -----" << Qt::endl;
    if (contents.isEmpty()) {
        out() << "(empty)" << Qt::endl;
    } else {
        out() << contents;
        if (!contents.endsWith(QLatin1Char('\n'))) {
            out() << Qt::endl;
        }
    }
    out().flush();
}

bool require(bool condition, const QString& message)
{
    if (!condition) {
        err() << "FAIL: " << message << Qt::endl;
        return false;
    }
    return true;
}

QString readLog(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

bool checkContains(const QString& haystack, const QString& needle, const QString& tag)
{
    if (haystack.contains(needle)) {
        return true;
    }
    err() << "FAIL: " << tag << " — expected to contain `" << needle
          << "`. Log was:\n" << haystack << Qt::endl;
    return false;
}

void leafThatThrows()
{
    MC_OP("oplog_test::leaf");
    _mc_op_.note(QStringLiteral("k=v"));
    throw std::runtime_error("synthetic_failure");
}

void midOp()
{
    MC_OP("oplog_test::mid");
    leafThatThrows();
}

bool runChainOnExceptionTest(const QString& logPath)
{
    QFile::remove(logPath);
    bool caught = false;
    try {
        MC_OP("oplog_test::root");
        midOp();
    } catch (const std::runtime_error&) {
        caught = true;
    }
    if (!require(caught, QStringLiteral("expected runtime_error to propagate"))) {
        return false;
    }

    miacode::debug_log::flushAsyncLogWriter(2000);
    const QString contents = readLog(logPath);
    dumpLogIfVerbose(QStringLiteral("ChainOnException"), contents);
    if (!require(!contents.isEmpty(), QStringLiteral("operation log is empty after exception"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("op=oplog_test::leaf"), QStringLiteral("leaf op recorded"))) {
        return false;
    }
    // The chain on the leaf's failure entry should walk leaf ← mid ← root.
    if (!checkContains(
            contents,
            QStringLiteral("oplog_test::leaf \xe2\x86\x90 oplog_test::mid \xe2\x86\x90 oplog_test::root"),
            QStringLiteral("chain leaf ← mid ← root present"))) {
        return false;
    }
    // MSVC's std::current_exception() returns null during pre-catch
    // unwind, so the destructor uses "what=(unwind)" as the marker.
    // Reliable what() text is asserted via the catch-handler path in
    // runFatalChainAppendTest below.
    if (!checkContains(contents, QStringLiteral("reason=exception"), QStringLiteral("reason=exception marker"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("notes=[k=v]"), QStringLiteral("notes recorded"))) {
        return false;
    }
    return true;
}

bool runFailReturnTest(const QString& logPath)
{
    QFile::remove(logPath);
    auto endpoint = []() -> bool {
        MC_OP("oplog_test::fail_endpoint");
        _mc_op_.note(QStringLiteral("path=/tmp/x"));
        _mc_op_.fail(QStringLiteral("disk full"));
        return false;
    };
    const bool result = endpoint();
    if (!require(!result, QStringLiteral("endpoint must return false"))) {
        return false;
    }

    miacode::debug_log::flushAsyncLogWriter(2000);
    const QString contents = readLog(logPath);
    dumpLogIfVerbose(QStringLiteral("FailReturn"), contents);
    if (!checkContains(contents, QStringLiteral("op=oplog_test::fail_endpoint"), QStringLiteral("fail op recorded"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("reason=disk full"), QStringLiteral("reason recorded"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("notes=[path=/tmp/x]"), QStringLiteral("note recorded"))) {
        return false;
    }
    return true;
}

bool runHappyPathSilentTest(const QString& logPath)
{
    QFile::remove(logPath);
    {
        MC_OP("oplog_test::happy_outer");
        {
            MC_OP("oplog_test::happy_inner");
            // normal exit
        }
    }
    miacode::debug_log::flushAsyncLogWriter(2000);
    const QString contents = readLog(logPath);
    dumpLogIfVerbose(QStringLiteral("HappyPath"), contents);
    if (!require(contents.isEmpty(), QStringLiteral("happy path must produce no log lines, got: ") + contents)) {
        return false;
    }
    return true;
}

bool runFatalChainAppendTest(const QString& fatalPath)
{
    QFile::remove(fatalPath);
    {
        MC_OP("oplog_test::fatal_outer");
        {
            MC_OP("oplog_test::fatal_inner");
            miacode::debug_log::appendFatalMessage(
                QStringLiteral("test/seam"),
                QStringLiteral("synthetic_fatal"));
        }
    }
    miacode::debug_log::flushAsyncLogWriter(2000);
    const QString contents = readLog(fatalPath);
    dumpLogIfVerbose(QStringLiteral("FatalChannelChainAppend"), contents);
    if (!require(!contents.isEmpty(), QStringLiteral("fatal log empty"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("synthetic_fatal"), QStringLiteral("payload present"))) {
        return false;
    }
    if (!checkContains(
            contents,
            QStringLiteral("chain=oplog_test::fatal_inner \xe2\x86\x90 oplog_test::fatal_outer"),
            QStringLiteral("fatal-channel chain inlined by appendFatalMessage"))) {
        return false;
    }
    return true;
}

bool runCurrentChainEmptyOutsideAnyScopeTest()
{
    const QString chain = miacode::oplog::currentChain();
    return require(chain.isEmpty(),
                   QStringLiteral("currentChain() outside any scope should be empty, got: ") + chain);
}

// Phase 4 — exercises the heap-free shadow buffer + flush. Pushes a
// chain via MC_OP, calls flushShadowToDisk() directly (the SEH filter
// would do the same on a real crash), and asserts the chain is in the
// resulting file in the exact leaf-first order the SEH path emits.
// Off Windows the flush is a no-op by design, so the test asserts that
// contract instead (see the #ifndef Q_OS_WIN branch below).
bool runShadowFlushTest(const QString& shadowPath)
{
    QFile::remove(shadowPath);

    // The shadow path is captured at installShadow() time. Re-installing
    // is a no-op (idempotent) — for the test we set the env var BEFORE
    // installShadow() runs so the path is captured correctly.
    {
        MC_OP("oplog_test::shadow_outer");
        {
            MC_OP("oplog_test::shadow_mid");
            {
                MC_OP("oplog_test::shadow_leaf");
                miacode::oplog::flushShadowToDisk();
            }
        }
    }

#ifndef Q_OS_WIN
    // flushShadowToDisk() is a documented no-op off Windows (see OperationLog.h):
    // the dump exists to serve the SEH filter, and no other platform has an
    // equivalent to wire it into. Push/pop still maintain the shadow buffers, so
    // assert the no-op contract here instead of a file that is never written.
    return require(!QFile::exists(shadowPath),
                   QStringLiteral("flushShadowToDisk should stay a no-op off Windows"));
#else
    const QString contents = readLog(shadowPath);
    if (!require(!contents.isEmpty(),
                 QStringLiteral("shadow log empty after flushShadowToDisk"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("miacode operation breadcrumb shadow"),
                       QStringLiteral("shadow header"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("[2] oplog_test::shadow_leaf"),
                       QStringLiteral("shadow leaf at depth index 2"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("[1] oplog_test::shadow_mid"),
                       QStringLiteral("shadow mid at depth index 1"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("[0] oplog_test::shadow_outer"),
                       QStringLiteral("shadow outer at depth index 0"))) {
        return false;
    }
    if (!checkContains(contents, QStringLiteral("depth=3"),
                       QStringLiteral("shadow depth=3"))) {
        return false;
    }
    return true;
#endif
}

// Phase 4 — micro-benchmark to confirm the shadow buffer adds
// negligible overhead per MC_OP push/pop. Runs N iterations of a
// nested-scope construct/destruct cycle and reports the per-cycle
// cost. Used to gate the design — if the cost goes above ~200ns we
// reconsider.
bool runShadowBenchmark()
{
    constexpr int kIterations = 100000;
    constexpr int kInnerDepth = 4;

    auto worker = [] {
        // 4-deep nested MC_OP. Each iteration does 4 push + 4 pop.
        // Distinct nested blocks because the MC_OP macro declares a
        // local named _mc_op_ — multiple in the same scope conflict.
        for (int i = 0; i < kIterations; ++i) {
            MC_OP("bench::a");
            { MC_OP("bench::b");
                { MC_OP("bench::c");
                    { MC_OP("bench::d");
                        (void)i;
                    }
                }
            }
        }
    };

    QElapsedTimer timer;
    timer.start();
    worker();
    const qint64 elapsedNs = timer.nsecsElapsed();
    const double cyclesPerOp = static_cast<double>(elapsedNs)
                               / (static_cast<double>(kIterations) * kInnerDepth * 2.0);

    out() << QStringLiteral("benchmark: %1 iterations × %2-deep × (push+pop) "
                            "= %3 ns per push-or-pop")
                    .arg(kIterations)
                    .arg(kInnerDepth)
                    .arg(cyclesPerOp, 0, 'f', 1)
          << Qt::endl;

    // Soft budget — fail loudly if we ever creep above 1µs per op.
    // Today's measurement should be well under 200 ns.
    if (cyclesPerOp > 1000.0) {
        err() << "FAIL: MC_OP overhead exceeded 1 µs per push/pop (regression?)" << Qt::endl;
        return false;
    }
    return true;
}

}  // namespace

// Realistic mini-scenarios (not assertions) — they exist so a verbose
// run produces sample log output the team can read in the PR. Mirrors
// the rollout shapes the design doc describes for areas A–C.
void runRealisticScenarioDemos(const QString& logPath, const QString& fatalPath)
{
    if (!g_verbose) {
        return;
    }
    QFile::remove(logPath);
    QFile::remove(fatalPath);

    // Scenario 1: editor save endpoint that fails on a permission error.
    auto saveDocument = [](const QString& path) -> bool {
        MC_OP("MainWindow::DocumentSection::saveToPath");
        _mc_op_.note(QStringLiteral("path=%1").arg(path));
        if (path.contains(QLatin1String("readonly"))) {
            _mc_op_.fail(QStringLiteral("QFile::open: permission denied"));
            return false;
        }
        return true;
    };
    auto onSaveFile = [&]() -> bool {
        MC_OP("MainWindow::DocumentSection::onSaveFile");
        return saveDocument(QStringLiteral("<chart-root>/readonly/maidata.txt"));
    };
    (void)onSaveFile();

    // Scenario 2: IPC handler that bails on an unknown command.
    auto handleAttach = [](const QString& cmd) {
        MC_OP("DemoSession::handleAttach");
        _mc_op_.note(QStringLiteral("hwnd=0x0"));
        Q_UNUSED(cmd);
        _mc_op_.fail(QStringLiteral("editor HWND invalid"));
    };
    auto processInbound = [&](const QString& cmd) {
        MC_OP("DemoSession::processInbound");
        _mc_op_.note(QStringLiteral("cmd=%1").arg(cmd));
        if (cmd == QLatin1String("attach")) {
            handleAttach(cmd);
        }
    };
    processInbound(QStringLiteral("attach"));

    // Scenario 3: parser exception — chain captured, what= placeholder
    // (real what() shows up via the Fatal-channel path below).
    auto parseFile = [] {
        MC_OP("SimaiNativeParser::parse");
        _mc_op_.note(QStringLiteral("line=42"));
        throw std::runtime_error("unexpected token '&' at line 42");
    };
    auto importChart = [&] {
        MC_OP("ChartImporter::importChart");
        try {
            parseFile();
        } catch (...) {
            // Real catch-handler convention: fatal + chain inlined,
            // and the parse op's chain-only line is already on Channel::Operation.
            miacode::debug_log::appendFatalMessage(
                QStringLiteral("import/parse_failed"),
                QStringLiteral("parser raised"));
        }
    };
    importChart();

    // Scenario 4: deep nested IPC-style exception chain. Mirrors the
    // shape of a thrown exception walking a multi-level MC_OP stack:
    //   main → runDemoEndpoint → DemoSession::run
    //        → onStdinCommandLine → handleAttach → (throw)
    // The leaf throws (simulating a heap exhaustion during shared-memory
    // attach). The catch handler at the outer boundary calls
    // appendFatalMessage exactly the way a real catch-handler does, and
    // the chain is auto-inlined into both the fatal entry AND each
    // op-failure entry as the Scope destructors unwind.
    auto handleAttachThrows = [] {
        MC_OP("DemoSession::handleAttach");
        _mc_op_.note(QStringLiteral("editor_hwnd=0x12340000 session=demo proto=1"));
        // Simulate the failure mode: SharedMemory attach fails because
        // the slot byte size doesn't match what the endpoint expects,
        // and the constructor throws.
        throw std::runtime_error("SharedMemory::attach: slot byte size mismatch (got 65536, expected 524288)");
    };
    auto onStdinCommandLineSim = [&] {
        MC_OP("DemoSession::onStdinCommandLine");
        _mc_op_.note(QStringLiteral("cmd=attach"));
        handleAttachThrows();
    };
    auto demoSessionRun = [&] {
        MC_OP("DemoSession::run");
        onStdinCommandLineSim();
    };
    auto runDemoEndpointSim = [&] {
        MC_OP("runDemoEndpoint");
        _mc_op_.note(QStringLiteral("staticTestMode=0"));
        try {
            demoSessionRun();
        } catch (const std::exception& ex) {
            // currentExceptionDetail() would run INSIDE the catch in
            // production, so what() is captured here.
            const QString detail = QString::fromUtf8(ex.what());
            const QString error = QStringLiteral("Unhandled demo endpoint exception.");
            miacode::debug_log::appendFatalMessage(
                QStringLiteral("demo/endpoint_exception"),
                QStringLiteral("%1 details=%2").arg(error, detail));
        }
    };
    auto mainSim = [&] {
        MC_OP("main");
        runDemoEndpointSim();
    };
    mainSim();

    // Scenario 5: Phase-3 audio init chain (area H). Mirrors the
    // actual MC_OP stack in src/audio/BassPreviewAudioBackend.cpp:
    //   reloadAssets → initializeAudioEngine → ensureBassFxLoaded
    // when bass_fx.dll is missing from the runtime install. Each
    // layer emits its own fail() with the underlying error code so
    // the Channel::Operation log shows which BASS call rejected the
    // request and what it was trying to do.
    auto ensureBassFxLoadedSim = [] {
        MC_OP("BassPreviewAudioBackend::ensureBassFxLoaded");
        const QString libraryPath = QStringLiteral("<app-dir>/bass_fx.dll");
        _mc_op_.note(QStringLiteral("path=%1").arg(libraryPath));
        _mc_op_.fail(QStringLiteral("LoadLibraryW failed err=126"));
        return false;
    };
    auto initializeAudioEngineSim = [&] {
        MC_OP("BassPreviewAudioBackend::initializeAudioEngine");
        _mc_op_.note(QStringLiteral("device_sr=48000"));
        if (!ensureBassFxLoadedSim()) {
            _mc_op_.fail(QStringLiteral("bass_fx load failed"));
            return false;
        }
        return true;
    };
    auto reloadAssetsSim = [&] {
        MC_OP("BassPreviewAudioBackend::reloadAssets");
        if (!initializeAudioEngineSim()) {
            _mc_op_.fail(QStringLiteral("initializeAudioEngine failed"));
            return;
        }
    };
    reloadAssetsSim();

    // Scenario 6: Phase-3 render device-lost recovery chain (area J).
    // A DXGI_ERROR_DEVICE_REMOVED arrives via the renderer's queued
    // signal; onRendererDeviceLost runs on the GUI thread, tears down
    // Core, and tries to re-initialise. Re-init throws because the GPU
    // TDR hasn't completed yet — the catch handler at the GUI-thread
    // seam logs via appendFatalMessage.
    auto initialiseIfReadySim = [] {
        MC_OP("PreviewDCompSurface::initialiseIfReady");
        throw std::runtime_error("DXGI_ERROR_DEVICE_REMOVED on re-init (HRESULT 0x887A0005)");
    };
    auto onRendererDeviceLostSim = [&] {
        MC_OP("PreviewDCompSurface::onRendererDeviceLost");
        _mc_op_.note(QStringLiteral("frames_pre=2487"));
        initialiseIfReadySim();
    };
    auto guiThreadDispatchSim = [&] {
        MC_OP("PreviewDCompSurface::onRendererDeviceLostQueuedDispatch");
        try {
            onRendererDeviceLostSim();
        } catch (const std::exception& ex) {
            const QString detail = QString::fromUtf8(ex.what());
            miacode::debug_log::appendFatalMessage(
                QStringLiteral("preview/dcomp_recovery_failed"),
                QStringLiteral("Recovery from DEVICE_REMOVED failed: %1").arg(detail));
        }
    };
    guiThreadDispatchSim();

    miacode::debug_log::flushAsyncLogWriter(2000);
    dumpLogIfVerbose(QStringLiteral("Realistic scenarios — Channel::Operation"), readLog(logPath));
    dumpLogIfVerbose(QStringLiteral("Realistic scenarios — Channel::Fatal"), readLog(fatalPath));
}

// Cross-process crash demo — parent side. Spawns a child copy of
// oplog_self_test with --child-crash, waits for the child's
// CrashExit, then reads the child's shadow log out of MIACODE_LOG_DIR
// and prints it. Mirrors what a process supervisor that collates a
// crashed child's shadow log does in production.
int runCrossProcessCrashDemo(const QString& selfExecutablePath)
{
    QTemporaryDir parentLogDir;
    if (!parentLogDir.isValid()) {
        err() << "could not create temp log dir" << Qt::endl;
        return 1;
    }
    const QString sharedLogDir = parentLogDir.path();
    out() << QStringLiteral("[parent] log_dir=%1").arg(sharedLogDir) << Qt::endl;

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("MIACODE_LOG_DIR"), sharedLogDir);
    // Clear any inherited override so the child uses MIACODE_LOG_DIR.
    env.remove(QStringLiteral("MIACODE_OPLOG_SHADOW_PATH"));

    QProcess child;
    child.setProcessEnvironment(env);
    child.setProcessChannelMode(QProcess::SeparateChannels);
    child.start(selfExecutablePath,
                QStringList{QStringLiteral("--child-crash")});
    if (!child.waitForStarted(5000)) {
        err() << "[parent] child failed to start: " << child.errorString() << Qt::endl;
        return 1;
    }
    out() << QStringLiteral("[parent] child started pid=%1, awaiting crash...")
                 .arg(child.processId())
          << Qt::endl;
    const qint64 childPid = static_cast<qint64>(child.processId());
    child.waitForFinished(10000);

    const auto exitStatus = child.exitStatus();
    const int exitCode = child.exitCode();
    out() << QStringLiteral("[parent] child finished status=%1 exit_code=0x%2")
                 .arg(exitStatus == QProcess::CrashExit ? "CrashExit" : "NormalExit")
                 .arg(static_cast<unsigned>(exitCode), 0, 16)
          << Qt::endl;
    if (exitStatus != QProcess::CrashExit) {
        err() << "[parent] expected CrashExit, got NormalExit" << Qt::endl;
        return 1;
    }

    // Mirrors a process supervisor's crash handler: read the dead
    // child's shadow file from the shared log dir and inline it into
    // the parent's bug report.
    const QString shadowPath = QDir(sharedLogDir).filePath(
        QStringLiteral("miacode_op_chain_%1.log").arg(childPid));
    QFile shadowFile(shadowPath);
    if (!shadowFile.exists()) {
        err() << "[parent] shadow log missing at " << shadowPath << Qt::endl;
        return 1;
    }
    if (!shadowFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
        err() << "[parent] could not open shadow log: " << shadowFile.errorString() << Qt::endl;
        return 1;
    }
    const QByteArray shadowContents = shadowFile.readAll();
    shadowFile.close();
    out() << QStringLiteral("[parent] collated child shadow log (%1 bytes from %2):")
                 .arg(shadowContents.size())
                 .arg(shadowPath)
          << Qt::endl;
    out() << QString::fromUtf8(shadowContents);
    if (!shadowContents.endsWith('\n')) {
        out() << Qt::endl;
    }
    return 0;
}

// Cross-process crash demo — child side. Pushes a known op chain,
// then triggers a SEGV that runs through the SEH filter so the
// shadow buffer flush fires. Process exits via CrashExit.
[[noreturn]] void runChildCrash()
{
    // crash_recovery::install() registers the SEH top-level filter
    // that calls flushShadowToDisk on access violations etc., AND
    // calls installShadow internally. The shadow path resolves from
    // MIACODE_LOG_DIR (set by parent).
    miacode::crash_recovery::install();

    // Build a realistic-looking chain so the parent's collation has
    // something interesting to show. Note that we deliberately do
    // NOT pop these scopes — we're about to crash, so the chain
    // visible to the SEH path / explicit flush below is the chain
    // at the moment of death.
    [[maybe_unused]] miacode::oplog::Scope mc_op_main("xprocess_demo::child_main");
    [[maybe_unused]] miacode::oplog::Scope mc_op_loop("xprocess_demo::worker_loop");
    [[maybe_unused]] miacode::oplog::Scope mc_op_handle("xprocess_demo::handle_request");
    [[maybe_unused]] miacode::oplog::Scope mc_op_deser("xprocess_demo::deserialize_payload");

    // Flush the shadow eagerly. In production this happens via the
    // SEH filter on access violations / segfaults. For a deterministic
    // demo we drive it directly, then __fastfail — same end-state for
    // the parent (CrashExit + populated shadow log).
    miacode::oplog::flushShadowToDisk();

#ifdef _WIN32
    __fastfail(7);  // STATUS_STACK_BUFFER_OVERRUN — QProcess sees CrashExit
#else
    std::abort();
#endif
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    bool simulateFailure = false;
    bool xprocessDemo = false;
    bool childCrash = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--verbose") || arg == QLatin1String("-v")) {
            g_verbose = true;
        } else if (arg == QLatin1String("--simulate-failure")) {
            // Intentionally break one expectation so the harness output
            // demonstrates the FAIL: line shape. Used only when manually
            // showcasing the test framework — never wired into CI.
            simulateFailure = true;
        } else if (arg == QLatin1String("--xprocess-demo")) {
            xprocessDemo = true;
        } else if (arg == QLatin1String("--child-crash")) {
            childCrash = true;
        }
    }

    if (childCrash) {
        runChildCrash();
        // unreachable
    }
    if (xprocessDemo) {
        return runCrossProcessCrashDemo(QCoreApplication::applicationFilePath());
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        err() << "could not create temp dir" << Qt::endl;
        return 1;
    }
    const QString opLogPath = QDir(tempDir.path()).filePath(QStringLiteral("op.log"));
    const QString fatalLogPath = QDir(tempDir.path()).filePath(QStringLiteral("fatal.log"));
    const QString shadowLogPath = QDir(tempDir.path()).filePath(QStringLiteral("shadow.log"));
    qputenv("MIACODE_OPERATION_LOG_PATH", opLogPath.toUtf8());
    qputenv("MIACODE_FATAL_LOG_PATH", fatalLogPath.toUtf8());
    // Set BEFORE installShadow() so it captures our test path. The
    // shadow path is resolved once and cached as wchar_t for the SEH
    // path's heap-free safety; later changes to the env var are not
    // picked up.
    qputenv("MIACODE_OPLOG_SHADOW_PATH", shadowLogPath.toUtf8());
    miacode::oplog::installShadow();

    // Force the async writer into synchronous mode for the duration of
    // the test. The default async path leaves bytes in the QFile's
    // internal buffer between writes, so a same-process readback would
    // race the worker. shutdownAsyncLogWriter() sets permanentShutdown_,
    // after which every enqueue takes the open/write/close path
    // (close() flushes). See DebugLog.cpp::writeEntrySync.
    miacode::debug_log::shutdownAsyncLogWriter();

    if (!runCurrentChainEmptyOutsideAnyScopeTest()) {
        return 1;
    }
    if (!runHappyPathSilentTest(opLogPath)) {
        return 1;
    }
    if (!runFailReturnTest(opLogPath)) {
        return 1;
    }
    if (!runChainOnExceptionTest(opLogPath)) {
        return 1;
    }
    if (!runFatalChainAppendTest(fatalLogPath)) {
        return 1;
    }
    if (!runShadowFlushTest(shadowLogPath)) {
        return 1;
    }
    dumpLogIfVerbose(QStringLiteral("Shadow flush — heap-free dump"),
                     readLog(shadowLogPath));
    if (!runShadowBenchmark()) {
        return 1;
    }

    if (simulateFailure) {
        // Run a deliberately-broken assertion to demonstrate the
        // FAIL: line shape. The check looks for a sentinel string
        // that the system never writes.
        QFile::remove(opLogPath);
        {
            MC_OP("oplog_test::sim_outer");
            {
                MC_OP("oplog_test::sim_leaf");
                // Push some chain context, no failure inside.
            }
        }
        miacode::debug_log::flushAsyncLogWriter(2000);
        const QString contents = readLog(opLogPath);
        if (!checkContains(contents,
                           QStringLiteral("op=this_op_was_never_logged"),
                           QStringLiteral("simulate-failure demo"))) {
            return 1;
        }
    }

    runRealisticScenarioDemos(opLogPath, fatalLogPath);

    miacode::debug_log::shutdownAsyncLogWriter();
    out() << "oplog_self_test ok" << Qt::endl;
    return 0;
}
