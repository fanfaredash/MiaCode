#include "SimaiNativeParser.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QtMath>

#include "common/MuriConfig.h"
#include "common/MuriTypes.h"
#include "tools/muri/MuriAnalyzer.h"
#include "tools/muri/MuriPanelEntries.h"
#include "tools/muri/MuriStaticChecker.h"

namespace {

struct AnalyzedChart {
    SimaiNativeParseResult parsed;
    MuriAnalysisReport report;
    QVector<MuriStaticReference> staticReferences;
    QVector<miacode::muri::MuriPanelEntry> visibleEntries;
};

bool nearlyEqual(double a, double b, double epsilon = 1e-6)
{
    return qAbs(a - b) <= epsilon;
}

AnalyzedChart analyzeChart(const QString& chartText)
{
    AnalyzedChart result;
    result.parsed = SimaiNativeParser::parseForTimeline(chartText);
    result.report = MuriAnalyzer::analyze(result.parsed.noteMarkers);
    result.staticReferences = miacode::muri::buildStaticMuriReferences(
        result.parsed.noteMarkers,
        static_cast<double>(miacode::muri::kStaticTapOnSlideThresholdDefaultMs) / 1000.0);
    result.visibleEntries = miacode::muri::buildVisibleMuriPanelEntries(result.report, result.staticReferences);
    return result;
}

int countDiagnostics(const QVector<MuriDiagnostic>& diagnostics, MuriKind kind)
{
    int count = 0;
    for (const MuriDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.kind == kind) {
            ++count;
        }
    }
    return count;
}

int countStaticReferences(const QVector<MuriStaticReference>& references, MuriKind kind)
{
    int count = 0;
    for (const MuriStaticReference& reference : references) {
        if (reference.kind == kind) {
            ++count;
        }
    }
    return count;
}

int countVisibleEntries(const QVector<miacode::muri::MuriPanelEntry>& entries, MuriKind kind)
{
    int count = 0;
    for (const miacode::muri::MuriPanelEntry& entry : entries) {
        if (entry.kind == kind) {
            ++count;
        }
    }
    return count;
}

int countDiagnosticsWithAlertLevel(
    const QVector<MuriDiagnostic>& diagnostics,
    MuriKind kind,
    MuriAlertLevel alertLevel)
{
    int count = 0;
    for (const MuriDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.kind == kind && diagnostic.alertLevel == alertLevel) {
            ++count;
        }
    }
    return count;
}

int countStaticReferencesWithAlertLevel(
    const QVector<MuriStaticReference>& references,
    MuriKind kind,
    MuriAlertLevel alertLevel)
{
    int count = 0;
    for (const MuriStaticReference& reference : references) {
        if (reference.kind == kind && reference.alertLevel == alertLevel) {
            ++count;
        }
    }
    return count;
}

int countVisibleEntriesWithAlertLevel(
    const QVector<miacode::muri::MuriPanelEntry>& entries,
    MuriKind kind,
    MuriAlertLevel alertLevel)
{
    int count = 0;
    for (const miacode::muri::MuriPanelEntry& entry : entries) {
        if (entry.kind == kind && entry.alertLevel == alertLevel) {
            ++count;
        }
    }
    return count;
}

const MuriDiagnostic* firstDiagnostic(const QVector<MuriDiagnostic>& diagnostics, MuriKind kind)
{
    for (const MuriDiagnostic& diagnostic : diagnostics) {
        if (diagnostic.kind == kind) {
            return &diagnostic;
        }
    }
    return nullptr;
}

const MuriStaticReference* firstStaticReference(const QVector<MuriStaticReference>& references, MuriKind kind)
{
    for (const MuriStaticReference& reference : references) {
        if (reference.kind == kind) {
            return &reference;
        }
    }
    return nullptr;
}

const miacode::muri::MuriPanelEntry* firstVisibleEntry(
    const QVector<miacode::muri::MuriPanelEntry>& entries,
    MuriKind kind)
{
    for (const miacode::muri::MuriPanelEntry& entry : entries) {
        if (entry.kind == kind) {
            return &entry;
        }
    }
    return nullptr;
}

}  // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QTextStream out(stdout);
    QTextStream err(stderr);

    int failed = 0;
    const auto expect = [&](bool condition, const QString& message) {
        if (condition) {
            out << "[PASS] " << message << '\n';
            return;
        }
        err << "[FAIL] " << message << '\n';
        ++failed;
    };

    {
        const AnalyzedChart analyzed = analyzeChart(
            QStringLiteral("(240){16}\n8>3[4:1],,,,,\n8,\nE\n"));
        expect(analyzed.parsed.ok, QStringLiteral("anchor repro chart parses"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::SlideHeadTap) == 1,
            QStringLiteral("anchor repro emits one runtime slide-head-tap diagnostic"));
        expect(countStaticReferences(analyzed.staticReferences, MuriKind::SlideHeadTap) == 1,
            QStringLiteral("anchor repro emits one static slide-head-tap reference"));
        expect(countVisibleEntries(analyzed.visibleEntries, MuriKind::SlideHeadTap) == 1,
            QStringLiteral("anchor repro panel keeps one visible slide-head-tap entry"));

        if (const MuriDiagnostic* diagnostic =
                firstDiagnostic(analyzed.report.diagnostics, MuriKind::SlideHeadTap)) {
            expect(diagnostic->kind == MuriKind::SlideHeadTap,
                QStringLiteral("runtime anchor repro diagnostic kind is slide-head-tap"));
            expect(nearlyEqual(diagnostic->anchorSecond, 0.0),
                QStringLiteral("runtime anchor repro uses slide start as anchor second"));
            expect(nearlyEqual(diagnostic->second, 0.25),
                QStringLiteral("runtime anchor repro keeps the actual early-judge second"));
            expect(diagnostic->line == 2 && diagnostic->col == 1,
                QStringLiteral("runtime anchor repro points at the first involved object"));
            expect(diagnostic->detail.contains(QStringLiteral("tap 8")),
                QStringLiteral("runtime anchor repro detail names the affected tap lane"));
        }

        if (const MuriStaticReference* reference =
                firstStaticReference(analyzed.staticReferences, MuriKind::SlideHeadTap)) {
            expect(reference->cause.line == 2 && reference->cause.col == 1,
                QStringLiteral("static anchor repro keeps the earlier slide as cause"));
            expect(reference->affected.line == 3 && reference->affected.col == 1,
                QStringLiteral("static anchor repro keeps the later tap as affected"));
        }

        if (const miacode::muri::MuriPanelEntry* entry =
                firstVisibleEntry(analyzed.visibleEntries, MuriKind::SlideHeadTap)) {
            expect(nearlyEqual(entry->second, 0.0),
                QStringLiteral("anchor repro visible entry sorts by the first involved object"));
            expect(entry->line == 2 && entry->col == 1,
                QStringLiteral("anchor repro visible entry points at the first involved object"));
            expect(!entry->isStatic,
                QStringLiteral("anchor repro runtime entry suppresses the duplicate static entry"));
        }
    }

    {
        const AnalyzedChart analyzed = analyzeChart(
            QStringLiteral("(240){16}\n8>4[4:1]/1>5[4:1],,,,,\n1-5[8:1],\nE\n"));
        expect(analyzed.parsed.ok, QStringLiteral("synthetic head-star repro chart parses"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::SlideHeadTap) == 1,
            QStringLiteral("synthetic head-star repro keeps one runtime slide-head-tap diagnostic"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::MultiTouch) == 0,
            QStringLiteral("synthetic head-star repro no longer reports multitouch"));
        expect(countStaticReferences(analyzed.staticReferences, MuriKind::SlideHeadTap) == 1,
            QStringLiteral("synthetic head-star repro keeps one static slide-head-tap reference"));
        expect(countStaticReferences(analyzed.staticReferences, MuriKind::TapOnSlide) == 1,
            QStringLiteral("synthetic head-star repro now also catches the earlier slide tail on the head star"));
        expect(countVisibleEntries(analyzed.visibleEntries, MuriKind::SlideHeadTap) == 1,
            QStringLiteral("synthetic head-star repro keeps one visible slide-head-tap entry"));
        expect(countVisibleEntries(analyzed.visibleEntries, MuriKind::MultiTouch) == 0,
            QStringLiteral("synthetic head-star repro shows no visible multitouch entry"));
        expect(countVisibleEntries(analyzed.visibleEntries, MuriKind::TapOnSlide) == 1,
            QStringLiteral("synthetic head-star repro keeps one visible tap-on-slide entry"));
        expect(analyzed.visibleEntries.size() == 2,
            QStringLiteral("synthetic head-star repro now shows both valid head-star issues"));

        if (const MuriDiagnostic* diagnostic =
                firstDiagnostic(analyzed.report.diagnostics, MuriKind::SlideHeadTap)) {
            expect(diagnostic->detail.contains(QStringLiteral("star 1")),
                QStringLiteral("synthetic head-star repro runtime detail names the affected star lane"));
        }

        if (const miacode::muri::MuriPanelEntry* entry =
                firstVisibleEntry(analyzed.visibleEntries, MuriKind::TapOnSlide)) {
            expect(entry->rawDetail.contains(QStringLiteral("star 1")),
                QStringLiteral("synthetic head-star repro visible tap-on-slide detail names the star lane"));
        }
    }

    {
        const AnalyzedChart analyzed = analyzeChart(
            QStringLiteral(
                "(264){80},,,,,,,,8-4[8:1],,,,,,,,,,,,,,,,,,,,8,,,,,,,,,,,,,,,,,,,,3/5,,,,,,,,,,,,,,,,,,,,8>4[4:1],,,,,1>5[4:1],,,,,2>6[4:1],,\n"
                "{80},,,3<7[4:1],,,,,,,,,,,,,,,,,,,,,,,,,1-5[8:1],,,,,,,,,,,,,,,,,,,,1,,,,,,,,,,,,,,,,,,,,3/7,,,,,,,,,,,,\n"
                "{80},,,,,,,,1<5[4:1],,,,,8<4[4:1],,,,,7<3[4:1],,,,,6>2[4:1],,,,,1,,,,,,,,,,,,,,,,,,,,8-3[8:1]*-5[8:1],,,,,,,,,,,,,,,,,,,,8,,,,,,,,,,,,\n"
                "{80},,,,,,,,2/6,,,,,,,,,,,,,,,,,,,,8>4[4:1],,,,,1>5[4:1],,,,,2>6[4:1],,,,,3<7[4:1],,,,,8,,,,,,,,,,,,,,,,,,,,1-4[8:1]*-6[8:1],,,,,,,,,,,,\n"
                "E\n"));
        expect(analyzed.parsed.ok, QStringLiteral("prophesy late-warning repro chart parses"));
        expect(countDiagnosticsWithAlertLevel(
                   analyzed.report.diagnostics,
                   MuriKind::SlideHeadTap,
                   MuriAlertLevel::Warning)
                >= 1,
            QStringLiteral("prophesy late-warning repro emits runtime warning slide-head-tap"));
        expect(countStaticReferencesWithAlertLevel(
                   analyzed.staticReferences,
                   MuriKind::SlideHeadTap,
                   MuriAlertLevel::Warning)
                >= 1,
            QStringLiteral("prophesy late-warning repro emits static warning slide-head-tap"));
        expect(countVisibleEntriesWithAlertLevel(
                   analyzed.visibleEntries,
                   MuriKind::SlideHeadTap,
                   MuriAlertLevel::Warning)
                >= 1,
            QStringLiteral("prophesy late-warning repro keeps warning slide-head-tap visible"));
    }

    {
        const AnalyzedChart analyzed = analyzeChart(
            QStringLiteral(
                "(193){8},1/5-8[8:1],,5/2-7[8:1],,2/6-3[8:1],,1/6,\n"
                "{8}2,3-6[8:1],,3/7-2[8:1],,7/4-1[8:1],,4/8-5[8:1],\n"
                "E\n"));
        expect(analyzed.parsed.ok, QStringLiteral("head-star tap-on-slide repro chart parses"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::TapOnSlide) == 0,
            QStringLiteral("head-star tap-on-slide repro stays static-only at runtime"));
        expect(countStaticReferences(analyzed.staticReferences, MuriKind::TapOnSlide) == 1,
            QStringLiteral("head-star tap-on-slide repro restores one static tap-on-slide"));
        expect(countVisibleEntries(analyzed.visibleEntries, MuriKind::TapOnSlide) == 1,
            QStringLiteral("head-star tap-on-slide repro keeps one visible tap-on-slide entry"));

        if (const MuriStaticReference* reference =
                firstStaticReference(analyzed.staticReferences, MuriKind::TapOnSlide)) {
            expect(reference->kind == MuriKind::TapOnSlide,
                QStringLiteral("head-star tap-on-slide repro first static kind is tap-on-slide"));
            expect(reference->affected.markerType == QStringLiteral("tap"),
                QStringLiteral("head-star tap-on-slide repro models the affected head star as tap"));
            expect(reference->affected.headStarTapLike,
                QStringLiteral("head-star tap-on-slide repro flags the affected helper as star-like"));
            expect(reference->affected.line == 2 && reference->affected.col == 6,
                QStringLiteral("head-star tap-on-slide repro anchors the affected slide at its real token"));
            expect(reference->cause.line == 1 && reference->cause.col == 36,
                QStringLiteral("head-star tap-on-slide repro keeps the prior slide tail as cause"));
        }

        if (const miacode::muri::MuriPanelEntry* entry =
                firstVisibleEntry(analyzed.visibleEntries, MuriKind::TapOnSlide)) {
            expect(entry->rawDetail.contains(QStringLiteral("star 3")),
                QStringLiteral("head-star tap-on-slide repro visible detail names the affected star lane"));
        }
    }

    {
        const AnalyzedChart analyzed = analyzeChart(
            QStringLiteral("(240){16}\n111,\nE\n"));
        expect(analyzed.parsed.ok, QStringLiteral("dense overlap repro chart parses"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::Overlap) == 1,
            QStringLiteral("dense overlap repro keeps one runtime overlap diagnostic"));
        expect(countStaticReferences(analyzed.staticReferences, MuriKind::Overlap) == 1,
            QStringLiteral("dense overlap repro keeps one static overlap reference"));
        expect(countVisibleEntries(analyzed.visibleEntries, MuriKind::Overlap) == 1,
            QStringLiteral("dense overlap repro keeps one visible overlap entry"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::MultiTouch) == 0,
            QStringLiteral("dense overlap repro does not add multitouch"));
    }

    {
        const AnalyzedChart analyzed = analyzeChart(
            QStringLiteral("(240){16}\n118,\nE\n"));
        expect(analyzed.parsed.ok, QStringLiteral("overlap-plus-third-note repro chart parses"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::Overlap) == 1,
            QStringLiteral("overlap-plus-third-note repro keeps one overlap diagnostic"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::MultiTouch) == 0,
            QStringLiteral("overlap-plus-third-note repro no longer reports multitouch"));
        expect(countVisibleEntries(analyzed.visibleEntries, MuriKind::Overlap) == 1,
            QStringLiteral("overlap-plus-third-note repro keeps one visible overlap entry"));
        expect(analyzed.visibleEntries.size() == 1,
            QStringLiteral("overlap-plus-third-note repro shows only one visible issue"));
    }

    {
        const AnalyzedChart analyzed = analyzeChart(
            QStringLiteral("(240){16}\n123,\nE\n"));
        expect(analyzed.parsed.ok, QStringLiteral("plain multitouch repro chart parses"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::Overlap) == 0,
            QStringLiteral("plain multitouch repro has no overlap diagnostic"));
        expect(countDiagnostics(analyzed.report.diagnostics, MuriKind::MultiTouch) == 1,
            QStringLiteral("plain multitouch repro still reports multitouch"));
        expect(countVisibleEntries(analyzed.visibleEntries, MuriKind::MultiTouch) == 1,
            QStringLiteral("plain multitouch repro keeps one visible multitouch entry"));

        if (!analyzed.visibleEntries.isEmpty()) {
            const miacode::muri::MuriPanelEntry& entry = analyzed.visibleEntries.constFirst();
            expect(entry.kind == MuriKind::MultiTouch,
                QStringLiteral("plain multitouch repro visible entry kind is multitouch"));
            expect(entry.line == 2 && entry.col == 1,
                QStringLiteral("plain multitouch repro anchors to the earliest contributing action"));
        }
    }

    if (failed != 0) {
        err << "\nMuri spec failed: " << failed << " case(s)\n";
        return 1;
    }

    out << "\nMuri spec passed.\n";
    return 0;
}
