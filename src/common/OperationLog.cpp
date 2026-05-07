#include "common/OperationLog.h"

#include "common/DebugLog.h"

#include <QFileInfo>

#include <exception>

namespace miacode::oplog {

namespace {

// Per-thread chain top. POD pointer initialised to nullptr — no
// constructor, no static-init-order trap. Safe to read from a Scope
// constructor that runs during library init.
thread_local Scope* g_chainTop = nullptr;

QString shortFile(const std::source_location& loc)
{
    const char* filePath = loc.file_name();
    if (filePath == nullptr) {
        return QString();
    }
    const QString full = QString::fromUtf8(filePath);
    return QFileInfo(full).fileName();
}

QString locationSuffix(const std::source_location& loc)
{
    const QString file = shortFile(loc);
    if (file.isEmpty()) {
        return QString();
    }
    return QStringLiteral(" at=%1:%2").arg(file).arg(static_cast<unsigned>(loc.line()));
}

QString notesSuffix(const QStringList& notes)
{
    if (notes.isEmpty()) {
        return QString();
    }
    return QStringLiteral(" notes=[%1]").arg(notes.join(QStringLiteral("; ")));
}

QString currentExceptionWhat()
{
    // Called from ~Scope() during stack unwind. On MSVC,
    // std::current_exception() returns a null pointer when invoked
    // before the matching catch handler is entered (per MSVC docs:
    // "outside of exception handling, returns an empty exception_ptr").
    // Itanium ABI implementations are more permissive but we target
    // MSVC-first, so callers must treat empty as "what unavailable".
    //
    // Reliable what() capture happens in the catch handler via
    // appendFatalMessage, which runs after the exception is "currently
    // handled" — see DebugLog.cpp::appendFatalMessage and the
    // currentExceptionDetail() helper in app/main.cpp.
    std::exception_ptr ep = std::current_exception();
    if (!ep) {
        return QString();
    }
    try {
        std::rethrow_exception(ep);
    } catch (const std::exception& ex) {
        const QString what = QString::fromUtf8(ex.what()).trimmed();
        return what.isEmpty() ? QStringLiteral("std::exception") : what;
    } catch (...) {
        return QStringLiteral("unknown non-std exception");
    }
}

}  // namespace

QString currentChain()
{
    QStringList parts;
    for (const Scope* s = g_chainTop; s != nullptr; s = s->parent_) {
        parts << QString::fromUtf8(s->op_);
    }
    return parts.join(QStringLiteral(" \xe2\x86\x90 "));  // ' ← ' (UTF-8)
}

Scope::Scope(const char* op, std::source_location loc) noexcept
    : op_(op != nullptr ? op : "")
    , entryUncaught_(std::uncaught_exceptions())
    , parent_(g_chainTop)
    , location_(loc)
{
    g_chainTop = this;
}

Scope::Scope(const char* op, QString detail, std::source_location loc) noexcept
    : op_(op != nullptr ? op : "")
    , entryUncaught_(std::uncaught_exceptions())
    , parent_(g_chainTop)
    , location_(loc)
{
    g_chainTop = this;
    if (!detail.isEmpty()) {
        notes_.append(std::move(detail));
    }
}

Scope::~Scope() noexcept
{
    // Unlink before any logging so that re-entrance into log code
    // sees a chain that excludes this (about-to-die) scope.
    if (g_chainTop == this) {
        g_chainTop = parent_;
    }

    if (emitted_) {
        return;
    }

    const int now = std::uncaught_exceptions();
    if (now <= entryUncaught_) {
        // Normal exit — happy path stays silent.
        return;
    }

    // Exception unwind. g_chainTop now points at our parent; we prepend
    // ourselves to the chain string so the failing op is at the head.
    QString parentChain = currentChain();
    QString chain = parentChain.isEmpty()
                        ? QString::fromUtf8(op_)
                        : QStringLiteral("%1 \xe2\x86\x90 %2")
                              .arg(QString::fromUtf8(op_), parentChain);
    QString what;
    try {
        what = currentExceptionWhat();
    } catch (...) {
        // Defensive — currentExceptionWhat itself shouldn't throw,
        // but ~Scope is noexcept so we belt-and-brace.
    }
    // Omit the what= field when MSVC's std::current_exception() returns
    // null during pre-catch unwind. The catch-handler path
    // (appendFatalMessage) captures what() reliably; the destructor
    // provides the chain context.
    QString whatField = what.isEmpty()
                            ? QStringLiteral("(unwind)")
                            : what;
    QString payload = QStringLiteral("op=%1 chain=%2 reason=exception what=%3%4%5")
                          .arg(QString::fromUtf8(op_),
                               chain,
                               whatField,
                               locationSuffix(location_),
                               notesSuffix(notes_));
    try {
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Operation,
            QStringLiteral("failed"),
            payload,
            /*force=*/true);
    } catch (...) {
        // Swallow — destructor is noexcept; letting an exception out
        // during unwind would call std::terminate.
    }
}

void Scope::note(QString kv) noexcept
{
    if (kv.isEmpty()) {
        return;
    }
    try {
        notes_.append(std::move(kv));
    } catch (...) {
        // QStringList allocation failed — drop the note silently.
    }
}

void Scope::fail(QString reason) noexcept
{
    if (emitted_) {
        return;
    }
    emitted_ = true;
    try {
        QString chain = currentChain();
        QString payload = QStringLiteral("op=%1 chain=%2 reason=%3%4%5")
                              .arg(QString::fromUtf8(op_),
                                   chain,
                                   reason.isEmpty() ? QStringLiteral("(unspecified)")
                                                    : reason,
                                   locationSuffix(location_),
                                   notesSuffix(notes_));
        miacode::debug_log::appendLine(
            miacode::debug_log::Channel::Operation,
            QStringLiteral("failed"),
            payload,
            /*force=*/true);
    } catch (...) {
        // fail() is noexcept; drop on failure to log.
    }
}

}  // namespace miacode::oplog
