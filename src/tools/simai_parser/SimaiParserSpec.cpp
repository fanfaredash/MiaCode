#include "SimaiNativeParser.h"

#include <QCoreApplication>
#include <QTextStream>
#include <QtMath>

namespace {

const TimelineNoteMarker* firstSlideLikeMarker(const SimaiNativeParseResult& result)
{
    for (const TimelineNoteMarker& marker : result.noteMarkers) {
        if (marker.type == QLatin1String("slide") || marker.type == QLatin1String("wifi")) {
            return &marker;
        }
    }
    return nullptr;
}

bool nearlyEqual(double a, double b, double epsilon = 1e-6)
{
    return qAbs(a - b) <= epsilon;
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
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral("A1xhf[4:1],\nE"));
        expect(parsed.ok, QStringLiteral("touch supports mixed modifier order x/h/f"));
        expect(!parsed.noteMarkers.isEmpty(), QStringLiteral("touch mixed modifier creates marker"));
        if (!parsed.noteMarkers.isEmpty()) {
            const TimelineNoteMarker& marker = parsed.noteMarkers.constFirst();
            expect(marker.type == QLatin1String("touch_hold"), QStringLiteral("touch mixed modifier parses as touch_hold"));
            expect(marker.isFirework, QStringLiteral("touch mixed modifier keeps firework flag"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral("C1bx,\nE"));
        expect(parsed.ok, QStringLiteral("lenient parse accepts C1 with unsupported b/x modifiers"));
        expect(!parsed.noteMarkers.isEmpty(), QStringLiteral("C1 lenient parse emits marker"));
        if (!parsed.noteMarkers.isEmpty()) {
            expect(parsed.noteMarkers.constFirst().touchPad == QLatin1String("C"), QStringLiteral("C1 is normalized to C in lenient mode"));
        }
    }

    {
        const SimaiNativeParseResult strictC1 = SimaiNativeParser::validateSyntax(QStringLiteral("C1,\nE"));
        expect(!strictC1.ok, QStringLiteral("validate rejects C1/C2 form"));
    }

    {
        const SimaiNativeParseResult strictOk = SimaiNativeParser::validateSyntax(QStringLiteral("1-5b[8:1],\nE"));
        const SimaiNativeParseResult strictTailB = SimaiNativeParser::validateSyntax(QStringLiteral("1-5[8:1]b,\nE"));
        const SimaiNativeParseResult strictMidB = SimaiNativeParser::validateSyntax(QStringLiteral("1-5b-1[8:2],\nE"));
        const SimaiNativeParseResult lenientTailB = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5[8:1]b,\nE"));

        expect(strictOk.ok, QStringLiteral("validate accepts break slide b before duration block"));
        expect(!strictTailB.ok, QStringLiteral("validate rejects break slide b after duration block"));
        expect(!strictMidB.ok, QStringLiteral("validate rejects break slide b inside shape chain"));
        expect(lenientTailB.ok, QStringLiteral("timeline parse keeps lenient break slide b position"));
    }

    {
        const SimaiNativeParseResult strictFestival = SimaiNativeParser::validateSyntax(QStringLiteral("1-5[8:1]-1[8:2],\nE"));
        expect(!strictFestival.ok, QStringLiteral("validate rejects per-segment duration for festival slide"));

        const SimaiNativeParseResult lenientFestival = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5[8:1]-1[8:2],\nE"));
        const SimaiNativeParseResult totalDuration = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5-1[8:3],\nE"));
        expect(lenientFestival.ok, QStringLiteral("timeline parse accepts per-segment duration for festival slide"));
        expect(totalDuration.ok, QStringLiteral("timeline parse accepts total duration festival slide"));

        const TimelineNoteMarker* a = firstSlideLikeMarker(lenientFestival);
        const TimelineNoteMarker* b = firstSlideLikeMarker(totalDuration);
        expect(a != nullptr && b != nullptr, QStringLiteral("both festival cases emit slide marker"));
        if (a != nullptr && b != nullptr) {
            expect(nearlyEqual(a->slideTraceSecond, b->slideTraceSecond), QStringLiteral("festival wait time is normalized consistently"));
            expect(nearlyEqual(a->endSecond, b->endSecond), QStringLiteral("festival end time matches normalized total duration"));
            expect(a->slideSegmentDurations.size() == b->slideSegmentDurations.size(), QStringLiteral("festival segment count matches"));
            const int compareCount = qMin(a->slideSegmentDurations.size(), b->slideSegmentDurations.size());
            bool allSegmentMatch = true;
            for (int i = 0; i < compareCount; ++i) {
                if (!nearlyEqual(a->slideSegmentDurations.at(i), b->slideSegmentDurations.at(i))) {
                    allSegmentMatch = false;
                    break;
                }
            }
            expect(allSegmentMatch, QStringLiteral("festival per-segment syntax normalizes to uniform chain speed"));
        }
    }

    {
        const SimaiNativeParseResult lenientBeat = SimaiNativeParser::parseForTimeline(QStringLiteral("{10}1,\nE"));
        const SimaiNativeParseResult strictBeatInvalid = SimaiNativeParser::validateSyntax(QStringLiteral("{10}1,\nE"));
        const SimaiNativeParseResult strictBeatValid = SimaiNativeParser::validateSyntax(QStringLiteral("{12}1,\nE"));
        expect(lenientBeat.ok, QStringLiteral("lenient parse accepts non-divisor beat value"));
        expect(!strictBeatInvalid.ok, QStringLiteral("validate rejects beat value that is not a positive divisor of 384"));
        expect(strictBeatValid.ok, QStringLiteral("validate accepts beat value that divides 384"));
    }

    {
        const SimaiNativeParseResult lenientInvalid = SimaiNativeParser::parseForTimeline(QStringLiteral("@,\nE"));
        const SimaiNativeParseResult strictInvalid = SimaiNativeParser::validateSyntax(QStringLiteral("@,\nE"));
        expect(!lenientInvalid.ok, QStringLiteral("lenient parse rejects illegal token"));
        expect(!strictInvalid.ok, QStringLiteral("validate also rejects illegal token"));
    }

    {
        const SimaiNativeParseResult lenientNoTerminal = SimaiNativeParser::parseForTimeline(QStringLiteral("1,\n"));
        const SimaiNativeParseResult strictNoTerminal = SimaiNativeParser::validateSyntax(QStringLiteral("1,\n"));
        const SimaiNativeParseResult strictNoComma = SimaiNativeParser::validateSyntax(QStringLiteral("1\nE"));
        expect(lenientNoTerminal.ok, QStringLiteral("lenient parse accepts missing terminal E line"));
        expect(strictNoTerminal.ok, QStringLiteral("validate accepts missing terminal E line"));
        expect(!strictNoComma.ok, QStringLiteral("validate reports missing beat separator in strict checks"));
    }

    {
        const SimaiNativeValidationReport strictOnlyReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("{10}1,\nE"),
            SimaiNativeValidationLocale::English
        );
        expect(!strictOnlyReport.ok, QStringLiteral("validation report fails when strict parse fails"));
        expect(strictOnlyReport.errorCount == 0 && strictOnlyReport.warningCount == 1, QStringLiteral("strict-only failure is downgraded to warning"));
        if (!strictOnlyReport.issues.isEmpty()) {
            expect(
                strictOnlyReport.issues.constFirst().displayMessage.startsWith(QStringLiteral("[WARNING]")),
                QStringLiteral("strict-only issue is prefixed with warning severity")
            );
        }
    }

    {
        const SimaiNativeValidationReport zhReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("1\nE"),
            SimaiNativeValidationLocale::Chinese
        );
        expect(!zhReport.ok, QStringLiteral("zh validation report fails on strict syntax issue"));
        if (!zhReport.issues.isEmpty()) {
            expect(
                zhReport.issues.constFirst().displayMessage.startsWith(QStringLiteral("[警告]")),
                QStringLiteral("zh report uses chinese warning prefix for strict-only issue")
            );
            expect(
                zhReport.issues.constFirst().displayMessage.contains(QStringLiteral("缺少拍间分隔符")),
                QStringLiteral("zh report localizes strict message detail")
            );
        }
    }

    {
        const SimaiNativeValidationReport emptyReport = SimaiNativeParser::buildValidationReport(
            QString(),
            SimaiNativeValidationLocale::English
        );
        expect(!emptyReport.ok, QStringLiteral("empty chart validation report fails"));
        expect(emptyReport.errorCount == 1, QStringLiteral("empty chart validation emits one error"));
        if (!emptyReport.issues.isEmpty()) {
            expect(
                emptyReport.issues.constFirst().displayMessage.contains(QStringLiteral("Chart is empty.")),
                QStringLiteral("empty chart validation emits canonical message")
            );
        }
    }

    if (failed != 0) {
        err << "\nSimai parser spec failed: " << failed << " case(s)\n";
        return 1;
    }

    out << "\nSimai parser spec passed.\n";
    return 0;
}
