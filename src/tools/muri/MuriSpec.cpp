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

        if (!analyzed.report.diagnostics.isEmpty()) {
            const MuriDiagnostic& diagnostic = analyzed.report.diagnostics.constFirst();
            expect(diagnostic.kind == MuriKind::SlideHeadTap,
                QStringLiteral("runtime anchor repro diagnostic kind is slide-head-tap"));
            expect(nearlyEqual(diagnostic.anchorSecond, 0.0),
                QStringLiteral("runtime anchor repro uses slide start as anchor second"));
            expect(nearlyEqual(diagnostic.second, 0.25),
                QStringLiteral("runtime anchor repro keeps the actual early-judge second"));
            expect(diagnostic.line == 2 && diagnostic.col == 1,
                QStringLiteral("runtime anchor repro points at the first involved object"));
        }

        if (!analyzed.staticReferences.isEmpty()) {
            const MuriStaticReference& reference = analyzed.staticReferences.constFirst();
            expect(reference.cause.line == 2 && reference.cause.col == 1,
                QStringLiteral("static anchor repro keeps the earlier slide as cause"));
            expect(reference.affected.line == 3 && reference.affected.col == 1,
                QStringLiteral("static anchor repro keeps the later tap as affected"));
        }

        if (!analyzed.visibleEntries.isEmpty()) {
            const miacode::muri::MuriPanelEntry& entry = analyzed.visibleEntries.constFirst();
            expect(nearlyEqual(entry.second, 0.0),
                QStringLiteral("anchor repro visible entry sorts by the first involved object"));
            expect(entry.line == 2 && entry.col == 1,
                QStringLiteral("anchor repro visible entry points at the first involved object"));
            expect(!entry.isStatic,
                QStringLiteral("anchor repro runtime entry suppresses the duplicate static entry"));
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
