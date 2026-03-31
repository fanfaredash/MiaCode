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
        expect(parsed.ok, QStringLiteral("lenient parse accepts C1 with b/x modifiers"));
        expect(!parsed.noteMarkers.isEmpty(), QStringLiteral("C1 lenient parse emits marker"));
        if (!parsed.noteMarkers.isEmpty()) {
            const TimelineNoteMarker& marker = parsed.noteMarkers.constFirst();
            expect(marker.touchPad == QLatin1String("C"), QStringLiteral("C1 is normalized to C in lenient mode"));
            expect(marker.isBreak, QStringLiteral("touch b modifier binds break flag"));
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
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("{4}1x/7,7,E1/7,C/7,\n{4}E5/7,7,E6/7,C/7,\nE")
        );
        expect(parsed.ok, QStringLiteral("touch and tap mixed each repro parses"));
        int simultaneousTapCount = 0;
        int simultaneousTapEachCount = 0;
        int simultaneousTouchCount = 0;
        for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
            const bool simultaneousMoment = nearlyEqual(marker.second, 1.0)
                || nearlyEqual(marker.second, 1.5)
                || nearlyEqual(marker.second, 2.0)
                || nearlyEqual(marker.second, 3.0)
                || nearlyEqual(marker.second, 3.5);
            if (!simultaneousMoment) {
                continue;
            }
            if (marker.type == QLatin1String("tap")) {
                ++simultaneousTapCount;
                if (marker.isEach) {
                    ++simultaneousTapEachCount;
                }
            } else if (marker.type == QLatin1String("touch")) {
                ++simultaneousTouchCount;
                expect(marker.isEach, QStringLiteral("touch keeps each flag when paired with tap"));
            }
        }
        expect(simultaneousTapCount == 5, QStringLiteral("repro emits five tap notes at touch-shared moments"));
        expect(simultaneousTouchCount == 5, QStringLiteral("repro emits five touch notes at shared moments"));
        expect(simultaneousTapEachCount == simultaneousTapCount, QStringLiteral("tap is marked each when paired with touch"));
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("{4}1-4[8:1]/C,\nE")
        );
        expect(parsed.ok, QStringLiteral("touch and slide mixed each repro parses"));
        const TimelineNoteMarker* slide = nullptr;
        const TimelineNoteMarker* touch = nullptr;
        for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
            if (marker.type == QLatin1String("slide")) {
                slide = &marker;
            } else if (marker.type == QLatin1String("touch")) {
                touch = &marker;
            }
        }
        expect(slide != nullptr && touch != nullptr, QStringLiteral("touch and slide repro emits both note types"));
        if (slide != nullptr) {
            expect(slide->headEach, QStringLiteral("slide head is marked each when paired with touch"));
        }
        if (touch != nullptr) {
            expect(touch->isEach, QStringLiteral("touch remains each when paired with slide"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("(61.5){16}1b/5bpp4[16:3],,,(246){4}3bqq4[4:3]/5?pp4[4:3],\nE")
        );
        expect(parsed.ok, QStringLiteral("shared shoot-moment slide repro parses"));

        const TimelineNoteMarker* earlySlide = nullptr;
        int laterSlideCount = 0;
        int laterSlideEachCount = 0;
        for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
            if (marker.type != QLatin1String("slide")) {
                continue;
            }
            if (nearlyEqual(marker.second, 0.0)) {
                earlySlide = &marker;
                continue;
            }
            ++laterSlideCount;
            if (marker.slideEach) {
                ++laterSlideEachCount;
            }
        }

        expect(earlySlide != nullptr, QStringLiteral("shared shoot-moment repro keeps the early slide marker"));
        expect(laterSlideCount == 2, QStringLiteral("shared shoot-moment repro emits the two later slides"));
        if (earlySlide != nullptr) {
            expect(earlySlide->headEach, QStringLiteral("early slide head stays each when paired with tap"));
            expect(!earlySlide->slideEach, QStringLiteral("early slide track stays blue when a later each-group shares its shoot moment"));
        }
        expect(laterSlideEachCount == laterSlideCount, QStringLiteral("only the later simultaneous slides keep yellow track state"));
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
        const SimaiNativeParseResult lenientDoubleSlash = SimaiNativeParser::parseForTimeline(QStringLiteral("1//2,\nE"));
        const SimaiNativeParseResult strictDoubleSlash = SimaiNativeParser::validateSyntax(QStringLiteral("1//2,\nE"));
        const SimaiNativeParseResult lenientDoubleBacktick = SimaiNativeParser::parseForTimeline(QStringLiteral("1``2,\nE"));
        const SimaiNativeParseResult strictDoubleBacktick = SimaiNativeParser::validateSyntax(QStringLiteral("1``2,\nE"));

        expect(lenientDoubleSlash.ok, QStringLiteral("lenient parse keeps accepting repeated slash separator for compatibility"));
        expect(!strictDoubleSlash.ok, QStringLiteral("validate rejects repeated slash separator"));
        expect(lenientDoubleBacktick.ok, QStringLiteral("lenient parse keeps accepting repeated backtick separator for compatibility"));
        expect(!strictDoubleBacktick.ok, QStringLiteral("validate rejects repeated backtick separator"));

        const SimaiNativeValidationReport doubleSlashReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("1//2,\nE"),
            SimaiNativeValidationLocale::English
        );
        const SimaiNativeValidationReport doubleBacktickReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("1``2,\nE"),
            SimaiNativeValidationLocale::English
        );
        expect(!doubleSlashReport.ok, QStringLiteral("validation report treats repeated slash separator as error"));
        expect(doubleSlashReport.errorCount == 1, QStringLiteral("repeated slash separator counts as one validation error"));
        expect(!doubleBacktickReport.ok, QStringLiteral("validation report treats repeated backtick separator as error"));
        expect(doubleBacktickReport.errorCount == 1, QStringLiteral("repeated backtick separator counts as one validation error"));
    }

    {
        const SimaiNativeParseResult lenientInlineLowerTerminal = SimaiNativeParser::parseForTimeline(QStringLiteral("1,e"));
        const SimaiNativeParseResult strictInlineLowerTerminal = SimaiNativeParser::validateSyntax(QStringLiteral("1,e"));
        const SimaiNativeParseResult strictInlineUpperTerminal = SimaiNativeParser::validateSyntax(QStringLiteral("{1},E"));
        const SimaiNativeParseResult strictTerminalWithComma = SimaiNativeParser::validateSyntax(QStringLiteral("E,"));

        expect(lenientInlineLowerTerminal.ok, QStringLiteral("lenient parse accepts inline lowercase terminal marker"));
        expect(strictInlineLowerTerminal.ok, QStringLiteral("validate accepts inline lowercase terminal marker"));
        expect(strictInlineUpperTerminal.ok, QStringLiteral("validate accepts inline uppercase terminal marker"));
        expect(!strictTerminalWithComma.ok, QStringLiteral("validate rejects terminal marker followed by comma"));

        const QString terminalPrefix = QStringLiteral("Invalid terminal marker placement: ");
        expect(lenientInlineLowerTerminal.errors.isEmpty(), QStringLiteral("inline lowercase terminal marker adds no lenient errors"));
        expect(strictInlineLowerTerminal.errors.isEmpty(), QStringLiteral("inline lowercase terminal marker adds no strict errors"));
        if (!strictTerminalWithComma.errors.isEmpty()) {
            expect(
                strictTerminalWithComma.errors.constFirst().message.startsWith(terminalPrefix),
                QStringLiteral("terminal marker followed by comma stays terminal marker error")
            );
        }
    }

    {
        const SimaiNativeValidationReport strictOnlyReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("{10}1,\nE"),
            SimaiNativeValidationLocale::English
        );
        expect(strictOnlyReport.ok, QStringLiteral("validation report stays ok when issue is warning-only"));
        expect(strictOnlyReport.errorCount == 0 && strictOnlyReport.warningCount == 1, QStringLiteral("strict-only failure is downgraded to warning"));
        if (!strictOnlyReport.issues.isEmpty()) {
            expect(
                strictOnlyReport.issues.constFirst().displayMessage.startsWith(QStringLiteral("[WARNING]")),
                QStringLiteral("strict-only issue is prefixed with warning severity")
            );
        }
    }

    {
        const SimaiNativeParseResult lenientClamped = SimaiNativeParser::parseForTimeline(
            QStringLiteral("(60){20000}1,,\nE")
        );
        expect(lenientClamped.ok, QStringLiteral("lenient parse accepts beat value above 384 by clamping to 384"));
        expect(lenientClamped.errors.isEmpty(), QStringLiteral("clamped beat value does not create lenient errors"));
        expect(lenientClamped.warnings.isEmpty(), QStringLiteral("lenient parse stays quiet for clamped beat value"));
        expect(!lenientClamped.noteMarkers.isEmpty(), QStringLiteral("clamped beat value still emits note markers"));

        const SimaiNativeValidationReport clampedReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("(60){20000}1,,\nE"),
            SimaiNativeValidationLocale::English
        );
        expect(clampedReport.ok, QStringLiteral("validation report stays ok for beat value above 384 clamp"));
        expect(clampedReport.errorCount == 0, QStringLiteral("clamped beat value does not count as validation error"));
        expect(clampedReport.warningCount == 1, QStringLiteral("clamped beat value adds one validation warning"));
        if (!clampedReport.issues.isEmpty()) {
            expect(
                clampedReport.issues.constFirst().displayMessage.contains(QStringLiteral("treated as 384")),
                QStringLiteral("clamped beat value warning explains 384 fallback")
            );
        }
    }

    {
        const SimaiNativeValidationReport zhReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("1\nE"),
            SimaiNativeValidationLocale::Chinese
        );
        expect(zhReport.ok, QStringLiteral("zh validation report stays ok on warning-only strict issue"));
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
