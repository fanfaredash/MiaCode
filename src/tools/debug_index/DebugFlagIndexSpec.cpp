// Drift guard for docs/ops/DEBUG_INDEX.md.
//
// Every `MIACODE_*` environment flag read anywhere in src/ must be documented in
// docs/ops/DEBUG_INDEX.md, and every flag the doc mentions must either still be read
// in the code or be an explicitly-retired flag (kept in the doc's retired
// section for history). This catches the two drift directions that have bitten
// before: a new flag added without a doc entry, and a removed flag left stale in
// the doc. Registered with CTest as `debug_flag_index_spec` so it runs locally
// via `ctest -R debug_flag_index_spec` and in CI alongside the other specs.
//
// The repo root is injected at configure time via the MIACODE_SOURCE_ROOT
// compile definition (see CMakeLists.txt) — the spec reads the source tree and
// the doc from disk rather than embedding a flag list.

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QRegularExpressionMatchIterator>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTextStream>

#ifndef MIACODE_SOURCE_ROOT
#error "MIACODE_SOURCE_ROOT must be defined (repo root absolute path)"
#endif

namespace {

// Flags intentionally removed from the code but kept in docs/ops/DEBUG_INDEX.md's
// retired section for history. Keep this in lock-step with that section: when a
// flag is genuinely retired, move it to the doc's retired list AND add it here.
const QSet<QString> kRetiredFlags = {
    QStringLiteral("MIACODE_ENABLE_PYGAME_PREVIEW"),
    QStringLiteral("MIACODE_PREVIEW_DIAG_COMPARE_VIDEO_FALLBACK_EVERY"),
    QStringLiteral("MIACODE_PREVIEW_DIAG_COMPARE_PRESENT_EVERY"),
    QStringLiteral("MIACODE_PREVIEW_SESSION_SCRIPT"),
    QStringLiteral("MIACODE_DISABLE_GL_DEBUG_MESSAGES"),
    QStringLiteral("MIACODE_PREVIEW_DONT_CREATE_NATIVE_WIDGET_SIBLINGS"),
    QStringLiteral("MIACODE_PREVIEW_VISUAL_SMOOTHING"),
    QStringLiteral("MIACODE_SKIP_DIAG_D3D11"),
    QStringLiteral("MIACODE_TIMELINE_USE_DCOMP"),
    QStringLiteral("MIACODE_PREVIEW_USE_DCOMP"),
    QStringLiteral("MIACODE_PREVIEW_DCOMP_EXCLUSIVE"),
    QStringLiteral("MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND"),
    QStringLiteral("MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA"),
    QStringLiteral("MIACODE_PREVIEW_DCOMP_QUIESCE_QSG"),
};

// Spec files that embed MIACODE_* flag-name literals for contract checking
// rather than reading them as real env flags, so they are skipped during the
// code scan. This spec's own file is here for the retired allowlist above;
// V1ShellRemovalSpec.cpp is here because it pins MIACODE_UI_SKIN as a token
// that must NOT survive v1 shell removal — that literal would otherwise look
// like a live flag read to this scan. See:
// docs/specs/ui/plans/2026-08-25-v2-stage0a-remove-v1-shell.md
//
// The exclusion is only valid for files that embed literals for contract
// checking, not files that actually read env flags — see the exclusionAbuse
// check below, which fails loudly if an excluded file ever adds a real
// qgetenv/qEnvironmentVariable call.
const QSet<QString> kSelfExcludedFileNames = {
    QStringLiteral("DebugFlagIndexSpec.cpp"),
    QStringLiteral("V1ShellRemovalSpec.cpp"),
};

// MIACODE_* tokens that are CMake compile definitions (injected via
// target_compile_definitions), not runtime env flags read with qgetenv. These
// legitimately do not belong in docs/ops/DEBUG_INDEX.md. Any spec that consumes
// the source-root compile define (this one, ui_text_locale_spec, …) references
// the token in source, so filter it out globally rather than per-file.
const QSet<QString> kCompileDefinitions = {
    QStringLiteral("MIACODE_SOURCE_ROOT"),
    // Path to the spec-only MiaCode.UI import mirror; a dev-tools compile
    // definition, not a runtime env flag.
    QStringLiteral("MIACODE_QML_SPEC_IMPORT_ROOT"),
};

QSet<QString> collectFlags(const QString& text)
{
    static const QRegularExpression re(QStringLiteral("MIACODE_[A-Z0-9_]+"));
    QSet<QString> flags;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        const QString flag = it.next().captured(0);
        if (kCompileDefinitions.contains(flag)) {
            continue;
        }
        flags.insert(flag);
    }
    return flags;
}

QString readFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QString();
    }
    return QString::fromUtf8(file.readAll());
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    const QString root = QStringLiteral(MIACODE_SOURCE_ROOT);
    const QString srcDir = root + QStringLiteral("/src");
    const QString docPath = root + QStringLiteral("/docs/ops/DEBUG_INDEX.md");

    // 1. Every MIACODE_* literal read across the source tree (the spec files
    //    in kSelfExcludedFileNames excluded — they embed flag-name literals
    //    for contract checking, not real env reads). Matched by bare
    //    filename (QDirIterator::fileName()), not a path-tail endsWith,
    //    so a file merely sharing a name suffix (e.g. a hypothetical
    //    LegacyV1ShellRemovalSpec.cpp) is not silently excluded too.
    QSet<QString> codeFlags;
    QHash<QString, QString> excludedFilePaths;
    int scanned = 0;
    QDirIterator it(
        srcDir,
        {QStringLiteral("*.cpp"), QStringLiteral("*.h")},
        QDir::Files,
        QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (kSelfExcludedFileNames.contains(it.fileName())) {
            excludedFilePaths.insert(it.fileName(), path);
            continue;
        }
        codeFlags.unite(collectFlags(readFile(path)));
        ++scanned;
    }
    if (scanned == 0) {
        err << "debug_flag_index_spec: scanned 0 source files under " << srcDir
            << " — is MIACODE_SOURCE_ROOT correct?" << Qt::endl;
        return 1;
    }

    // 1b. The exclusion above is only valid for files that embed flag-name
    //     literals for contract checking, not files that actually read env
    //     flags. A genuine call to qgetenv or qEnvironmentVariable slipped
    //     into an excluded file would go undocumented and unnoticed, so
    //     check for it. Matched as call syntax (name followed by an opening
    //     paren), not a bare substring, so prose that merely mentions the
    //     function name in a comment — as this file's own comments do, to
    //     explain the exclusion — does not self-trip it.
    static const QRegularExpression envReadCall(
        QStringLiteral("\\b(qgetenv|qEnvironmentVariable)\\s*\\("));
    QStringList exclusionAbuse;
    for (const QString& name : kSelfExcludedFileNames) {
        const QString excludedPath = excludedFilePaths.value(name);
        if (excludedPath.isEmpty()) {
            continue;
        }
        const QString excludedText = readFile(excludedPath);
        if (envReadCall.match(excludedText).hasMatch()) {
            exclusionAbuse.append(name);
        }
    }
    exclusionAbuse.sort();

    // 2. Every flag mentioned in DEBUG_INDEX.md.
    const QString doc = readFile(docPath);
    if (doc.isEmpty()) {
        err << "debug_flag_index_spec: could not read " << docPath << Qt::endl;
        return 1;
    }
    const QSet<QString> docFlags = collectFlags(doc);

    // 3a. Undocumented: read in code but missing from the doc.
    QStringList undocumented;
    for (const QString& flag : codeFlags) {
        if (!docFlags.contains(flag)) {
            undocumented.append(flag);
        }
    }
    // 3b. Stale: in the doc but neither read in code nor explicitly retired.
    QStringList stale;
    for (const QString& flag : docFlags) {
        if (!codeFlags.contains(flag) && !kRetiredFlags.contains(flag)) {
            stale.append(flag);
        }
    }
    undocumented.sort();
    stale.sort();

    bool ok = true;
    if (!undocumented.isEmpty()) {
        ok = false;
        err << undocumented.size()
            << " MIACODE_* flag(s) read in src/ but missing from docs/ops/DEBUG_INDEX.md:" << Qt::endl;
        for (const QString& flag : undocumented) {
            err << "  - " << flag << Qt::endl;
        }
        err << "Fix: document each in docs/ops/DEBUG_INDEX.md (and mirror in "
               ".claude/skills/miacode-dev-guide/references/debug-and-logging.md)." << Qt::endl;
    }
    if (!stale.isEmpty()) {
        ok = false;
        err << stale.size()
            << " MIACODE_* flag(s) in docs/ops/DEBUG_INDEX.md no longer read anywhere in src/:" << Qt::endl;
        for (const QString& flag : stale) {
            err << "  - " << flag << Qt::endl;
        }
        err << "Fix: delete from DEBUG_INDEX.md, or move it to the doc's retired list "
               "and add it to kRetiredFlags in this spec." << Qt::endl;
    }
    if (!exclusionAbuse.isEmpty()) {
        ok = false;
        err << exclusionAbuse.size()
            << " file(s) in kSelfExcludedFileNames actually read an env flag:" << Qt::endl;
        for (const QString& name : exclusionAbuse) {
            err << "  - " << name << Qt::endl;
        }
        err << "Fix: the exclusion in kSelfExcludedFileNames is only valid for files that "
               "embed MIACODE_* literals for contract checking, not files that call "
               "qgetenv/qEnvironmentVariable to read a real flag. Remove the file from "
               "kSelfExcludedFileNames and document the flag instead." << Qt::endl;
    }

    if (!ok) {
        return 1;
    }

    out << "debug_flag_index_spec ok (" << codeFlags.size() << " live flags across "
        << scanned << " files, " << docFlags.size() << " documented)" << Qt::endl;
    return 0;
}
