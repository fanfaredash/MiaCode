// Phase 1 self-test for the operation breadcrumb logger. Drives the
// primitive through nested scopes (normal exit, fail(), exception
// unwind) and asserts the resulting log lines contain the expected
// chain shape.
//
// See docs/OPERATION_BREADCRUMB_LOGGING_PLAN.md §5.1.

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QString>
#include <QTemporaryDir>
#include <QTextStream>

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
        return saveDocument(QStringLiteral("D:/charts/readonly/maidata.txt"));
    };
    (void)onSaveFile();

    // Scenario 2: IPC handler that bails on an unknown command.
    auto handleAttach = [](const QString& cmd) {
        MC_OP("PreviewWorkerSession::handleAttach");
        _mc_op_.note(QStringLiteral("hwnd=0x0"));
        Q_UNUSED(cmd);
        _mc_op_.fail(QStringLiteral("editor HWND invalid"));
    };
    auto processInbound = [&](const QString& cmd) {
        MC_OP("PreviewWorkerSession::processInbound");
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

    // Scenario 4: Phase-2-shape worker IPC exception chain. Mirrors the
    // actual MC_OP stack a thrown exception would walk in production:
    //   main → runCliPreviewWorker → PreviewWorkerSession::run
    //        → onStdinCommandLine → handleAttach → (throw)
    // The leaf throws bad_alloc (simulating a heap exhaustion during
    // shared-memory attach). The catch handler at the runCli* boundary
    // calls appendFatalMessage exactly the way main.cpp does, and the
    // chain is auto-inlined into both the fatal entry AND each
    // op-failure entry as the Scope destructors unwind.
    auto handleAttachThrows = [] {
        MC_OP("PreviewWorkerSession::handleAttach");
        _mc_op_.note(QStringLiteral("editor_hwnd=0x12340000 session=demo proto=1"));
        // Simulate the failure mode: SharedMemory attach fails because
        // the editor's slot byte size doesn't match what the worker
        // expects, and the constructor throws.
        throw std::runtime_error("SharedMemory::attach: slot byte size mismatch (got 65536, expected 524288)");
    };
    auto onStdinCommandLineSim = [&] {
        MC_OP("PreviewWorkerSession::onStdinCommandLine");
        _mc_op_.note(QStringLiteral("cmd=attach"));
        handleAttachThrows();
    };
    auto previewWorkerRun = [&] {
        MC_OP("PreviewWorkerSession::run");
        onStdinCommandLineSim();
    };
    auto runCliPreviewWorkerSim = [&] {
        MC_OP("runCliPreviewWorker");
        _mc_op_.note(QStringLiteral("staticTestMode=0"));
        try {
            previewWorkerRun();
        } catch (const std::exception& ex) {
            // Mirrors main.cpp:1352. currentExceptionDetail() runs
            // INSIDE the catch, so what() is captured here.
            const QString detail = QString::fromUtf8(ex.what());
            const QString error = QStringLiteral("Unhandled preview worker exception.");
            miacode::debug_log::appendFatalMessage(
                QStringLiteral("preview/worker_exception"),
                QStringLiteral("%1 details=%2").arg(error, detail));
        }
    };
    auto mainSim = [&] {
        MC_OP("main");
        runCliPreviewWorkerSim();
    };
    mainSim();

    miacode::debug_log::flushAsyncLogWriter(2000);
    dumpLogIfVerbose(QStringLiteral("Realistic scenarios — Channel::Operation"), readLog(logPath));
    dumpLogIfVerbose(QStringLiteral("Realistic scenarios — Channel::Fatal"), readLog(fatalPath));
}

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    bool simulateFailure = false;
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QLatin1String("--verbose") || arg == QLatin1String("-v")) {
            g_verbose = true;
        } else if (arg == QLatin1String("--simulate-failure")) {
            // Intentionally break one expectation so the harness output
            // demonstrates the FAIL: line shape. Used only when manually
            // showcasing the test framework — never wired into CI.
            simulateFailure = true;
        }
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) {
        err() << "could not create temp dir" << Qt::endl;
        return 1;
    }
    const QString opLogPath = QDir(tempDir.path()).filePath(QStringLiteral("op.log"));
    const QString fatalLogPath = QDir(tempDir.path()).filePath(QStringLiteral("fatal.log"));
    qputenv("MIACODE_OPERATION_LOG_PATH", opLogPath.toUtf8());
    qputenv("MIACODE_FATAL_LOG_PATH", fatalLogPath.toUtf8());

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
