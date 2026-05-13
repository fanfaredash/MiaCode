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

const TimelineNoteMarker* firstMarkerOfType(const SimaiNativeParseResult& result, const QString& type)
{
    for (const TimelineNoteMarker& marker : result.noteMarkers) {
        if (marker.type == type) {
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
        const miacode::simai::SimaiTimingMetadata timingMetadata =
            miacode::simai::buildTimingMetadataFromRawText(QStringLiteral("&whole_time_signature=3/4"), true);
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral(",,,\n,,,\nE"),
            timingMetadata);
        expect(parsed.ok, QStringLiteral("timeline parse accepts whole_time_signature metadata"));
        expect(parsed.measureLineSeconds.size() == 3, QStringLiteral("whole_time_signature metadata creates 3/4 measure markers"));
        if (parsed.measureLineSeconds.size() == 3) {
            expect(
                nearlyEqual(parsed.measureLineSeconds.at(0), 0.0)
                    && nearlyEqual(parsed.measureLineSeconds.at(1), 1.5)
                    && nearlyEqual(parsed.measureLineSeconds.at(2), 3.0),
                QStringLiteral("whole_time_signature metadata shifts measure markers to 3/4 timing"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral(",,|| 3 / 4\n,,,\nE"));
        expect(parsed.ok, QStringLiteral("timeline parse accepts inline time-signature comments"));
        expect(parsed.measureLineSeconds.size() == 3, QStringLiteral("inline time-signature comment truncates and restarts measure timing"));
        if (parsed.measureLineSeconds.size() == 3) {
            expect(
                nearlyEqual(parsed.measureLineSeconds.at(0), 0.0)
                    && nearlyEqual(parsed.measureLineSeconds.at(1), 1.0)
                    && nearlyEqual(parsed.measureLineSeconds.at(2), 2.5),
                QStringLiteral("inline time-signature comment restarts measure markers at the comment position"));
        }

        const SimaiNativeValidationReport controlReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("1, || 3 / 4\n,,,\nE"),
            SimaiNativeValidationLocale::English
        );
        const SimaiNativeValidationReport commentReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("1, || just a comment\nE"),
            SimaiNativeValidationLocale::English
        );
        expect(controlReport.ok, QStringLiteral("validation accepts canonical inline time-signature comments"));
        expect(controlReport.warningCount == 0, QStringLiteral("inline time-signature comments do not create warnings"));
        expect(commentReport.ok, QStringLiteral("validation keeps ordinary || comments as plain comments"));
        expect(commentReport.warningCount == 0, QStringLiteral("ordinary || comments do not create warnings"));
    }

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
        const SimaiNativeParseResult zeroHold = SimaiNativeParser::parseForTimeline(QStringLiteral("1h[8:0]/A1h[8:0],\nE"));
        expect(zeroHold.ok, QStringLiteral("hold and touch-hold accept zero duration"));
        const TimelineNoteMarker* hold = firstMarkerOfType(zeroHold, QLatin1String("hold"));
        const TimelineNoteMarker* touchHold = firstMarkerOfType(zeroHold, QLatin1String("touch_hold"));
        expect(hold != nullptr && touchHold != nullptr, QStringLiteral("zero-duration hold repro emits both hold kinds"));
        if (hold != nullptr) {
            expect(nearlyEqual(hold->endSecond, hold->second), QStringLiteral("zero-duration hold keeps tail on the head timing"));
        }
        if (touchHold != nullptr) {
            expect(
                nearlyEqual(touchHold->endSecond, touchHold->second),
                QStringLiteral("zero-duration touch-hold keeps tail on the head timing"));
        }
    }

    {
        const SimaiNativeParseResult zeroSlide = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5[8:0],\nE"));
        const SimaiNativeParseResult zeroHashedSlide = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5[120#0],\nE"));
        expect(!zeroSlide.ok, QStringLiteral("slide rejects zero fraction duration"));
        expect(!zeroHashedSlide.ok, QStringLiteral("slide rejects zero # duration"));
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
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("6<8[160#16:1]/2>4[16:1],\nE")
        );
        expect(parsed.ok, QStringLiteral("hashed-duration slash slide each repro parses"));

        int slideCount = 0;
        int slideEachCount = 0;
        for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
            if (marker.type != QLatin1String("slide")) {
                continue;
            }
            ++slideCount;
            if (marker.slideEach) {
                ++slideEachCount;
            }
        }

        expect(slideCount == 2, QStringLiteral("hashed-duration slash slide each repro emits two slides"));
        expect(slideEachCount == 2, QStringLiteral("slash-paired slides with matching shoot moment both keep yellow track state"));
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("1-5[8:1],1,\nE")
        );
        expect(parsed.ok, QStringLiteral("tap-on-slide-head repro parses"));
        const TimelineNoteMarker* tap = firstMarkerOfType(parsed, QLatin1String("tap"));
        expect(tap != nullptr, QStringLiteral("tap-on-slide-head repro emits tap marker"));
        if (tap != nullptr) {
            expect(tap->slideHead, QStringLiteral("tap on slide shoot moment sets slideHead"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("{16}1-5[8:1],,,,1-4[8:1],\nE")
        );
        expect(parsed.ok, QStringLiteral("synthetic-head-on-slide-head repro parses"));
        const TimelineNoteMarker* laterSlide = nullptr;
        for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
            if (marker.type == QLatin1String("slide") && nearlyEqual(marker.second, 0.5)) {
                laterSlide = &marker;
                break;
            }
        }
        expect(laterSlide != nullptr, QStringLiteral("synthetic-head-on-slide-head repro emits later slide"));
        if (laterSlide != nullptr) {
            expect(laterSlide->slideHead, QStringLiteral("synthetic slide head on slide shoot moment sets slideHead"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("1h[4:1]/1-5[8:1],\nE")
        );
        expect(parsed.ok, QStringLiteral("hold-tail-on-slide-head repro parses"));
        const TimelineNoteMarker* hold = firstMarkerOfType(parsed, QLatin1String("hold"));
        expect(hold != nullptr, QStringLiteral("hold-tail-on-slide-head repro emits hold marker"));
        if (hold != nullptr) {
            expect(hold->tailOnSlideHead, QStringLiteral("hold tail at slide shoot moment sets tailOnSlideHead"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("1-5[8:1],A1,\nE")
        );
        expect(parsed.ok, QStringLiteral("touch-on-slide-head repro parses"));
        const TimelineNoteMarker* touch = firstMarkerOfType(parsed, QLatin1String("touch"));
        expect(touch != nullptr, QStringLiteral("touch-on-slide-head repro emits touch marker"));
        if (touch != nullptr) {
            expect(touch->onSlide, QStringLiteral("touch at slide head window sets onSlide"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("1w5[8:1],A1,\nE")
        );
        expect(parsed.ok, QStringLiteral("touch-on-wifi-head repro parses"));
        const TimelineNoteMarker* touch = firstMarkerOfType(parsed, QLatin1String("touch"));
        expect(touch != nullptr, QStringLiteral("touch-on-wifi-head repro emits touch marker"));
        if (touch != nullptr) {
            expect(touch->onSlide, QStringLiteral("touch at wifi head window sets onSlide"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("1w5[8:1],B1,\nE")
        );
        expect(parsed.ok, QStringLiteral("touch-on-wifi-pad-enter repro parses"));
        const TimelineNoteMarker* touch = firstMarkerOfType(parsed, QLatin1String("touch"));
        expect(touch != nullptr, QStringLiteral("touch-on-wifi-pad-enter repro emits touch marker"));
        if (touch != nullptr) {
            expect(touch->onSlide, QStringLiteral("touch at wifi pad-enter window sets onSlide"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("1-5[8:1],B1,\nE")
        );
        expect(parsed.ok, QStringLiteral("touch-on-slide-pad-enter repro parses"));
        const TimelineNoteMarker* touch = firstMarkerOfType(parsed, QLatin1String("touch"));
        expect(touch != nullptr, QStringLiteral("touch-on-slide-pad-enter repro emits touch marker"));
        if (touch != nullptr) {
            expect(touch->onSlide, QStringLiteral("touch at slide pad-enter window sets onSlide"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("1-5[384:1]/5-1[8:1],\nE")
        );
        expect(parsed.ok, QStringLiteral("before-after-slide repro parses"));
        const TimelineNoteMarker* firstSlide = nullptr;
        const TimelineNoteMarker* secondSlide = nullptr;
        for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
            if (marker.type != QLatin1String("slide")) {
                continue;
            }
            if (firstSlide == nullptr) {
                firstSlide = &marker;
            } else {
                secondSlide = &marker;
                break;
            }
        }
        expect(firstSlide != nullptr && secondSlide != nullptr, QStringLiteral("before-after-slide repro emits both slide markers"));
        if (firstSlide != nullptr && secondSlide != nullptr) {
            expect(firstSlide->beforeSlide, QStringLiteral("earlier short slide sets beforeSlide when its tail hits next shoot moment"));
            expect(secondSlide->afterSlide, QStringLiteral("later slide sets afterSlide when previous tail hits its shoot moment"));
        }
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
        const SimaiNativeParseResult strictInlineTerminalWithComment =
            SimaiNativeParser::validateSyntax(QStringLiteral("{1},E || terminal comment"));
        const SimaiNativeValidationReport terminalCommentReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("E || terminal comment"),
            SimaiNativeValidationLocale::English
        );
        const SimaiNativeValidationReport terminalCommentWithControlReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("(120)E || terminal comment"),
            SimaiNativeValidationLocale::English
        );
        const SimaiNativeParseResult strictTerminalWithComma = SimaiNativeParser::validateSyntax(QStringLiteral("E,"));
        const SimaiNativeParseResult strictTerminalWithTrailingText =
            SimaiNativeParser::validateSyntax(QStringLiteral("{1},E trailing"));

        expect(lenientInlineLowerTerminal.ok, QStringLiteral("lenient parse accepts inline lowercase terminal marker"));
        expect(strictInlineLowerTerminal.ok, QStringLiteral("validate accepts inline lowercase terminal marker"));
        expect(strictInlineUpperTerminal.ok, QStringLiteral("validate accepts inline uppercase terminal marker"));
        expect(strictInlineTerminalWithComment.ok, QStringLiteral("validate accepts inline terminal marker followed by comment"));
        expect(terminalCommentReport.ok && terminalCommentReport.warningCount == 0,
               QStringLiteral("validation report does not warn for terminal marker followed by comment"));
        expect(terminalCommentWithControlReport.ok && terminalCommentWithControlReport.warningCount == 0,
               QStringLiteral("validation report does not warn for control-prefixed terminal marker followed by comment"));
        expect(!strictTerminalWithComma.ok, QStringLiteral("validate rejects terminal marker followed by comma"));
        expect(!strictTerminalWithTrailingText.ok, QStringLiteral("validate rejects non-comment trailing text after terminal marker"));

        const QString terminalPrefix = QStringLiteral("Invalid terminal marker placement: ");
        expect(lenientInlineLowerTerminal.errors.isEmpty(), QStringLiteral("inline lowercase terminal marker adds no lenient errors"));
        expect(strictInlineLowerTerminal.errors.isEmpty(), QStringLiteral("inline lowercase terminal marker adds no strict errors"));
        if (!strictTerminalWithComma.errors.isEmpty()) {
            expect(
                strictTerminalWithComma.errors.constFirst().message.startsWith(terminalPrefix),
                QStringLiteral("terminal marker followed by comma stays terminal marker error")
            );
        }
        if (!strictTerminalWithTrailingText.errors.isEmpty()) {
            expect(
                strictTerminalWithTrailingText.errors.constFirst().message.startsWith(terminalPrefix),
                QStringLiteral("terminal marker with trailing text stays terminal marker error")
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
                clampedReport.issues.constFirst().displayMessage.contains(QStringLiteral("384")),
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

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral("1$$bx,\nE"));
        expect(parsed.ok, QStringLiteral("tap star material parses with $$ / b / x in mixed order"));
        expect(parsed.noteMarkers.size() == 1, QStringLiteral("tap star material emits one marker"));
        if (parsed.noteMarkers.size() == 1) {
            const TimelineNoteMarker& marker = parsed.noteMarkers.constFirst();
            expect(marker.type == QLatin1String("tap"), QStringLiteral("tap star material stays tap kind"));
            expect(marker.tapUsesStarMaterial, QStringLiteral("tap star material sets star flag"));
            expect(marker.tapStarDouble, QStringLiteral("tap $$ sets double-star flag"));
            expect(marker.isBreak, QStringLiteral("tap star material keeps break flag"));
            expect(marker.isEx, QStringLiteral("tap star material keeps ex flag"));
        }
    }

    {
        const SimaiNativeParseResult invalid = SimaiNativeParser::parseForTimeline(QStringLiteral("1$h[4:1],\nE"));
        expect(!invalid.ok, QStringLiteral("tap star material rejects hold modifier combination"));
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral("1@bx-4b[8:1],\nE"));
        expect(parsed.ok, QStringLiteral("@ slide head material parses with head b/x and track b"));
        const TimelineNoteMarker* marker = firstSlideLikeMarker(parsed);
        expect(marker != nullptr, QStringLiteral("@ slide head material emits slide marker"));
        if (marker != nullptr) {
            expect(marker->slideHeadUsesTapMaterial, QStringLiteral("@ slide marks tap-material head"));
            expect(marker->hasHeadStar, QStringLiteral("@ slide keeps head object"));
            expect(marker->headBreak, QStringLiteral("@ slide keeps head break"));
            expect(marker->headEx, QStringLiteral("@ slide keeps head ex"));
            expect(marker->trackBreak, QStringLiteral("@ slide keeps track break"));
        }
    }

    {
        const SimaiNativeParseResult invalid = SimaiNativeParser::parseForTimeline(QStringLiteral("1@?-4[8:1],\nE"));
        expect(!invalid.ok, QStringLiteral("@ slide rejects combination with headless modifiers"));
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral("1?-4[8:1]/1!-4[8:1],\nE"));
        expect(parsed.ok, QStringLiteral("headless slide ? and ! both parse"));
        expect(parsed.noteMarkers.size() == 2, QStringLiteral("headless slide examples emit two markers"));
        if (parsed.noteMarkers.size() == 2) {
            const TimelineNoteMarker& gradual = parsed.noteMarkers.at(0);
            const TimelineNoteMarker& immediate = parsed.noteMarkers.at(1);
            expect(!gradual.hasHeadStar && !gradual.headlessImmediate,
                   QStringLiteral("? slide disables head star with gradual wait visual"));
            expect(!immediate.hasHeadStar && immediate.headlessImmediate,
                   QStringLiteral("! slide disables head star with immediate wait visual"));
        }
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5[0.5##8:1],\nE"));
        expect(parsed.ok, QStringLiteral("delay slide accepts [wait##fraction] syntax"));
        const TimelineNoteMarker* marker = firstSlideLikeMarker(parsed);
        expect(marker != nullptr, QStringLiteral("delay slide wait##fraction emits slide marker"));
        if (marker != nullptr) {
            expect(nearlyEqual(marker->slideTraceSecond, 0.5), QStringLiteral("delay slide stores explicit wait seconds"));
            expect(nearlyEqual(marker->endSecond, 0.75), QStringLiteral("delay slide stores bpm-derived motion duration after explicit wait"));
        }
    }

    // <HS*N> hi-speed multiplier directive — Q1: only the bracketed
    // <HS*N> form is recognized; Q2: reset at E (chart end); Q5: hold
    // body freezes the HS at its emission time; Q7: negative/zero is an
    // error.
    {
        // Baseline: no HS → every note's hsMultiplier defaults to 1.0.
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral("1,2,3,4,E"));
        expect(parsed.ok, QStringLiteral("baseline chart with no HS directive parses ok"));
        bool allDefault = !parsed.noteMarkers.isEmpty();
        for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
            if (!nearlyEqual(marker.hsMultiplier, 1.0)) {
                allDefault = false;
                break;
            }
        }
        expect(allDefault, QStringLiteral("notes default to hsMultiplier = 1.0 when no <HS*> appears"));
    }

    {
        // <HS*2> followed by notes — emitted markers carry the multiplier.
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("1,<HS*2>2,3,E"));
        expect(parsed.ok, QStringLiteral("<HS*2> directive parses"));
        expect(parsed.noteMarkers.size() == 3, QStringLiteral("<HS*2> chart still emits all three taps"));
        if (parsed.noteMarkers.size() == 3) {
            expect(nearlyEqual(parsed.noteMarkers.at(0).hsMultiplier, 1.0),
                   QStringLiteral("note before <HS*2> keeps hsMultiplier 1.0"));
            expect(nearlyEqual(parsed.noteMarkers.at(1).hsMultiplier, 2.0),
                   QStringLiteral("note after <HS*2> picks up hsMultiplier 2.0"));
            expect(nearlyEqual(parsed.noteMarkers.at(2).hsMultiplier, 2.0),
                   QStringLiteral("HS persists until next directive — third note also 2.0"));
        }
    }

    {
        // Fractional + reset: <HS*0.5>, then <HS*1> reverts to default.
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("<HS*0.5>1,2,<HS*1>3,E"));
        expect(parsed.ok, QStringLiteral("<HS*0.5> followed by <HS*1> parses"));
        if (parsed.noteMarkers.size() == 3) {
            expect(nearlyEqual(parsed.noteMarkers.at(0).hsMultiplier, 0.5),
                   QStringLiteral("first note under <HS*0.5> has hsMultiplier 0.5"));
            expect(nearlyEqual(parsed.noteMarkers.at(1).hsMultiplier, 0.5),
                   QStringLiteral("second note under <HS*0.5> still 0.5"));
            expect(nearlyEqual(parsed.noteMarkers.at(2).hsMultiplier, 1.0),
                   QStringLiteral("<HS*1> resets effective multiplier to 1.0"));
        }
    }

    {
        // Q5: hold body uses HS at its start; a later directive does not
        // retroactively reshape it.
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("<HS*1.5>1h[4:1],<HS*3>2,E"));
        expect(parsed.ok, QStringLiteral("hold under <HS*1.5> followed by <HS*3> parses"));
        const TimelineNoteMarker* hold = firstMarkerOfType(parsed, QStringLiteral("hold"));
        expect(hold != nullptr && nearlyEqual(hold->hsMultiplier, 1.5),
               QStringLiteral("hold body keeps the HS in effect at its start (Q5)"));
        if (parsed.noteMarkers.size() >= 2) {
            const TimelineNoteMarker& last = parsed.noteMarkers.constLast();
            expect(nearlyEqual(last.hsMultiplier, 3.0),
                   QStringLiteral("subsequent tap picks up the later <HS*3>"));
        }
    }

    {
        // Q7: zero rejected as invalid.
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("<HS*0>1,E"));
        bool sawInvalidHs = false;
        for (const SimaiNativeMessage& err : parsed.errors) {
            if (err.message.contains(QStringLiteral("HS"))) {
                sawInvalidHs = true;
                break;
            }
        }
        expect(sawInvalidHs, QStringLiteral("<HS*0> emits an HS-related parse error"));
    }

    {
        // Q7: negative rejected.
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("<HS*-2>1,E"));
        bool sawInvalidHs = false;
        for (const SimaiNativeMessage& err : parsed.errors) {
            if (err.message.contains(QStringLiteral("HS"))) {
                sawInvalidHs = true;
                break;
            }
        }
        expect(sawInvalidHs, QStringLiteral("<HS*-2> emits an HS-related parse error"));
    }

    {
        // Q1: the old bracket-less HS*N> form is no longer recognized as
        // a directive — it falls through to ordinary token parsing,
        // which produces errors / unrelated tokens. We don't assert on
        // any specific error here, only that the new <HS*N> *is* the
        // canonical form by verifying it works (done above) and the old
        // form does not silently mutate HS state.
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(
            QStringLiteral("HS*2>1,2,E"));
        // Whatever the parser produces, the first emitted tap (if any)
        // must not have hsMultiplier 2.0 — that would mean the old form
        // accidentally still works.
        bool oldFormStillMutatesHs = false;
        for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
            if (marker.type == QLatin1String("tap")
                && !nearlyEqual(marker.hsMultiplier, 1.0)) {
                oldFormStillMutatesHs = true;
                break;
            }
        }
        expect(!oldFormStillMutatesHs,
               QStringLiteral("old bracket-less HS*N> form no longer mutates HS state (Q1)"));
    }

    if (failed != 0) {
        err << "\nSimai parser spec failed: " << failed << " case(s)\n";
        return 1;
    }

    out << "\nSimai parser spec passed.\n";
    return 0;
}
