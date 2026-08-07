// Behavior spec for src/common/DebugOptions.h accessors.
//
// Locks down the *bespoke* per-flag logic — default values, optional-flag
// `value_or` fallbacks, int/double parse + range clamping, debug-mode category
// gating, and the DComp cascade (children short-circuit unless the parent
// `MIACODE_PREVIEW_USE_DCOMP` is on). This is the safety net that a future
// table-driven DebugOptions registry must preserve: without it, rerouting the
// accessors through a generic table could silently change a default or drop a
// cascade and nothing would catch it.
//
// Registered with CTest as `debug_options_spec`. Drives the accessors by
// setting/clearing env vars; most read live, but the per-channel category gates
// (startup/runtime/audio/export/preview-profile) are snapshot into atomics at
// setDebugModeEnabled time, so a disable flag changed afterwards needs an explicit
// refreshDebugCategoryCache() to take effect. Cases must clean up after themselves. The Windows-on-ARM64 emulation auto-disable branch depends on
// real hardware detection and is not exercised here; every assertion below holds
// regardless of that branch (override is checked first; the default is false
// either way).

#include <QByteArray>
#include <QCoreApplication>
#include <QTextStream>

#include "common/DebugOptions.h"

namespace {

namespace dbg = miacode::debug_options;

void setEnv(const char* key, const char* value)
{
    qputenv(key, QByteArray(value));
}

void unsetEnv(const char* key)
{
    qunsetenv(key);
}

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << "FAIL: " << message << Qt::endl;
    }
    return condition;
}

bool verifyValueParsing(QTextStream& err)
{
    bool ok = true;
    ok &= require(dbg::isTruthyValue(QStringLiteral("1")), "isTruthy('1')", err);
    ok &= require(dbg::isTruthyValue(QStringLiteral("  TRUE ")), "isTruthy(' TRUE ') trims+lowercases", err);
    ok &= require(dbg::isTruthyValue(QStringLiteral("Yes")), "isTruthy('Yes')", err);
    ok &= require(dbg::isTruthyValue(QStringLiteral("on")), "isTruthy('on')", err);
    ok &= require(!dbg::isTruthyValue(QStringLiteral("0")), "isTruthy('0') is false", err);
    ok &= require(!dbg::isTruthyValue(QStringLiteral("maybe")), "isTruthy('maybe') is false", err);
    ok &= require(dbg::isFalseyValue(QStringLiteral("OFF")), "isFalsey('OFF')", err);
    ok &= require(dbg::isFalseyValue(QStringLiteral("no")), "isFalsey('no')", err);
    ok &= require(!dbg::isFalseyValue(QStringLiteral("1")), "isFalsey('1') is false", err);
    return ok;
}

bool verifyEnvHelpers(QTextStream& err)
{
    bool ok = true;

    // Legacy-key fallback: primary wins when set; falls back to legacy otherwise.
    unsetEnv("SPECPROBE_PRIMARY");
    unsetEnv("SPECPROBE_LEGACY");
    setEnv("SPECPROBE_LEGACY", "legacy-val");
    ok &= require(
        dbg::envValue("SPECPROBE_PRIMARY", "SPECPROBE_LEGACY") == QStringLiteral("legacy-val"),
        "envValue falls back to legacy key when primary unset", err);
    setEnv("SPECPROBE_PRIMARY", "primary-val");
    ok &= require(
        dbg::envValue("SPECPROBE_PRIMARY", "SPECPROBE_LEGACY") == QStringLiteral("primary-val"),
        "envValue prefers primary key over legacy", err);
    unsetEnv("SPECPROBE_PRIMARY");
    unsetEnv("SPECPROBE_LEGACY");

    // Int parsing + fallback.
    unsetEnv("SPECPROBE_INT");
    ok &= require(dbg::envIntValue("SPECPROBE_INT", 7) == 7, "envInt default when unset", err);
    setEnv("SPECPROBE_INT", "42");
    ok &= require(dbg::envIntValue("SPECPROBE_INT", 7) == 42, "envInt parses value", err);
    setEnv("SPECPROBE_INT", "notanint");
    ok &= require(dbg::envIntValue("SPECPROBE_INT", 7) == 7, "envInt falls back on parse failure", err);
    unsetEnv("SPECPROBE_INT");

    // Double parsing + fallback.
    unsetEnv("SPECPROBE_DBL");
    ok &= require(qFuzzyCompare(dbg::envDoubleValue("SPECPROBE_DBL", 1.5) + 1.0, 2.5),
                  "envDouble default when unset", err);
    setEnv("SPECPROBE_DBL", "3.25");
    ok &= require(qFuzzyCompare(dbg::envDoubleValue("SPECPROBE_DBL", 1.5), 3.25),
                  "envDouble parses value", err);
    unsetEnv("SPECPROBE_DBL");

    // Optional flag: empty -> nullopt, truthy/falsey -> value, garbage -> nullopt.
    unsetEnv("SPECPROBE_OPT");
    ok &= require(!dbg::envOptionalFlagValue("SPECPROBE_OPT").has_value(),
                  "envOptional unset -> nullopt", err);
    setEnv("SPECPROBE_OPT", "1");
    ok &= require(dbg::envOptionalFlagValue("SPECPROBE_OPT").value_or(false),
                  "envOptional '1' -> true", err);
    setEnv("SPECPROBE_OPT", "off");
    ok &= require(dbg::envOptionalFlagValue("SPECPROBE_OPT").has_value()
                      && !dbg::envOptionalFlagValue("SPECPROBE_OPT").value(),
                  "envOptional 'off' -> false", err);
    setEnv("SPECPROBE_OPT", "garbage");
    ok &= require(!dbg::envOptionalFlagValue("SPECPROBE_OPT").has_value(),
                  "envOptional garbage -> nullopt", err);
    unsetEnv("SPECPROBE_OPT");

    return ok;
}

bool verifyDebugCategoryGating(QTextStream& err)
{
    bool ok = true;
    unsetEnv("MIACODE_DISABLE_STARTUP_TIMING");

    dbg::setDebugModeEnabled(false);
    ok &= require(!dbg::startupTimingEnabled(), "category off when debug mode off", err);

    dbg::setDebugModeEnabled(true);
    ok &= require(dbg::startupTimingEnabled(), "category on when debug mode on + not disabled", err);

    setEnv("MIACODE_DISABLE_STARTUP_TIMING", "1");
    // Category gates are now SNAPSHOT into atomics at setDebugModeEnabled time
    // (no per-line env read), so a disable flag set after debug mode was applied
    // requires an explicit re-snapshot to take effect. This mirrors production,
    // where the disable flags are launch-time env vars resolved once at startup.
    dbg::refreshDebugCategoryCache();
    ok &= require(!dbg::startupTimingEnabled(), "category off when its disable key is set", err);

    unsetEnv("MIACODE_DISABLE_STARTUP_TIMING");
    dbg::setDebugModeEnabled(false);
    return ok;
}

bool verifyDefaultsAndClamps(QTextStream& err)
{
    bool ok = true;

    // Preserve the historical default-on path while the two diagnostic
    // launch profiles select explicit sides of the GPU-binding A/B.
    unsetEnv("MIACODE_GPU_BIND_HIGH_PERFORMANCE");
    ok &= require(dbg::gpuBindHighPerformanceEnabled(),
                  "GPU high-performance binding defaults on", err);
    setEnv("MIACODE_GPU_BIND_HIGH_PERFORMANCE", "0");
    ok &= require(!dbg::gpuBindHighPerformanceEnabled(),
                  "GPU high-performance binding respects explicit 0", err);
    setEnv("MIACODE_GPU_BIND_HIGH_PERFORMANCE", "1");
    ok &= require(dbg::gpuBindHighPerformanceEnabled(),
                  "GPU high-performance binding respects explicit 1", err);
    unsetEnv("MIACODE_GPU_BIND_HIGH_PERFORMANCE");

    // Frame-pacing sample ms: default 1000, positive-guarded.
    unsetEnv("MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS");
    ok &= require(dbg::previewFramePacingDiagnosticSampleMs() == 1000, "sample ms default 1000", err);
    setEnv("MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS", "500");
    ok &= require(dbg::previewFramePacingDiagnosticSampleMs() == 500, "sample ms takes value", err);
    setEnv("MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS", "0");
    ok &= require(dbg::previewFramePacingDiagnosticSampleMs() == 1000, "sample ms <=0 -> default", err);
    unsetEnv("MIACODE_PREVIEW_FRAME_PACING_DIAG_SAMPLE_MS");

    // Visual look-ahead vsyncs: default 1.0, clamped to [0, 4].
    unsetEnv("MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS");
    ok &= require(qFuzzyCompare(dbg::previewVisualLookaheadVsyncs(), 1.0), "lookahead default 1.0", err);
    setEnv("MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS", "2");
    ok &= require(qFuzzyCompare(dbg::previewVisualLookaheadVsyncs(), 2.0), "lookahead takes in-range value", err);
    setEnv("MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS", "0");
    ok &= require(qFuzzyCompare(dbg::previewVisualLookaheadVsyncs() + 1.0, 1.0), "lookahead 0 allowed", err);
    setEnv("MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS", "5");
    ok &= require(qFuzzyCompare(dbg::previewVisualLookaheadVsyncs(), 1.0), "lookahead >4 -> default", err);
    setEnv("MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS", "-1");
    ok &= require(qFuzzyCompare(dbg::previewVisualLookaheadVsyncs(), 1.0), "lookahead <0 -> default", err);
    setEnv("MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS", "abc");
    ok &= require(qFuzzyCompare(dbg::previewVisualLookaheadVsyncs(), 1.0), "lookahead invalid -> default", err);
    unsetEnv("MIACODE_PREVIEW_VISUAL_LOOKAHEAD_VSYNCS");

    // Compare-dump max samples: 0 unless runtime debug output is on, else default 8.
    dbg::setDebugModeEnabled(false);
    unsetEnv("MIACODE_DISABLE_RUNTIME_DEBUG_OUTPUT");
    unsetEnv("MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES");
    ok &= require(dbg::previewCompareDumpMaxSamples() == 0, "compare-dump samples 0 when not in debug", err);
    dbg::setDebugModeEnabled(true);
    ok &= require(dbg::previewCompareDumpMaxSamples() == 8, "compare-dump samples default 8 in debug", err);
    setEnv("MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES", "3");
    ok &= require(dbg::previewCompareDumpMaxSamples() == 3, "compare-dump samples takes value", err);
    setEnv("MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES", "-1");
    ok &= require(dbg::previewCompareDumpMaxSamples() == 8, "compare-dump samples <0 -> default 8", err);
    unsetEnv("MIACODE_PREVIEW_DIAG_COMPARE_DUMP_MAX_SAMPLES");
    dbg::setDebugModeEnabled(false);

    return ok;
}

bool verifyDCompCascade(QTextStream& err)
{
    bool ok = true;
    const char* kUse = "MIACODE_PREVIEW_USE_DCOMP";
    const char* kExclusive = "MIACODE_PREVIEW_DCOMP_EXCLUSIVE";
    const char* kPerPixel = "MIACODE_PREVIEW_DCOMP_PER_PIXEL_ALPHA";
    const char* kTopLevel = "MIACODE_PREVIEW_DCOMP_TOPLEVEL_HWND";
    const char* kQuiesce = "MIACODE_PREVIEW_DCOMP_QUIESCE_QSG";
    for (const char* k : {kUse, kExclusive, kPerPixel, kTopLevel, kQuiesce}) {
        unsetEnv(k);
    }

    // Parent off (default): every child short-circuits to false even if its own
    // flag is set.
    ok &= require(!dbg::previewUseDCompEnabled(), "DComp defaults off", err);
    setEnv(kExclusive, "1");
    setEnv(kPerPixel, "1");
    setEnv(kTopLevel, "1");
    setEnv(kQuiesce, "1");
    ok &= require(!dbg::previewDCompExclusiveEnabled(), "exclusive false when parent off", err);
    ok &= require(!dbg::previewDCompPerPixelAlphaEnabled(), "per-pixel-alpha false when parent off", err);
    ok &= require(!dbg::previewDCompTopLevelHwndEnabled(), "top-level-hwnd false when parent off", err);
    ok &= require(!dbg::previewDCompQuiesceQsgEnabled(), "quiesce-qsg false when parent off", err);
    for (const char* k : {kExclusive, kPerPixel, kTopLevel, kQuiesce}) {
        unsetEnv(k);
    }

    // Parent on: children follow their own override/default.
    setEnv(kUse, "1");
    ok &= require(dbg::previewUseDCompEnabled(), "DComp on with override", err);
    ok &= require(dbg::previewDCompTopLevelHwndEnabled(), "top-level-hwnd defaults on under DComp", err);
    ok &= require(dbg::previewDCompPerPixelAlphaEnabled(), "per-pixel-alpha defaults on under DComp", err);
    ok &= require(dbg::previewDCompExclusiveEnabled(), "exclusive auto-on via per-pixel-alpha default", err);

    setEnv(kPerPixel, "0");
    ok &= require(!dbg::previewDCompExclusiveEnabled(), "exclusive off when per-pixel off + not forced", err);
    setEnv(kExclusive, "1");
    ok &= require(dbg::previewDCompExclusiveEnabled(), "exclusive on when explicitly forced", err);
    setEnv(kTopLevel, "0");
    ok &= require(!dbg::previewDCompTopLevelHwndEnabled(), "top-level-hwnd respects explicit off", err);

    for (const char* k : {kUse, kExclusive, kPerPixel, kTopLevel, kQuiesce}) {
        unsetEnv(k);
    }
    return ok;
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    const bool ok =
        static_cast<int>(verifyValueParsing(err))
        & static_cast<int>(verifyEnvHelpers(err))
        & static_cast<int>(verifyDebugCategoryGating(err))
        & static_cast<int>(verifyDefaultsAndClamps(err))
        & static_cast<int>(verifyDCompCascade(err));

    if (!ok) {
        return 1;
    }
    out << "debug_options_spec ok" << Qt::endl;
    return 0;
}
