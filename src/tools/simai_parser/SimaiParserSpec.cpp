#include "SimaiNativeParser.h"

#include <QCoreApplication>
#include <QFile>
#include <QHash>
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
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral("A1hf[4:1],\nE"));
        expect(parsed.ok, QStringLiteral("touch supports mixed modifier order h/f"));
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
        // beta51+ — touch-hold without a duration signature is accepted and
        // treated as duration 0, mirroring the tap-hold behaviour for `1h`.
        // Covers the no-bracket form (`Ch`, `A1h`) and the empty-bracket
        // form (`Ch[]`). Tap-form `1h` was already supported pre-G1 but
        // is included here for symmetry.
        const SimaiNativeParseResult bareHold = SimaiNativeParser::parseForTimeline(QStringLiteral("Ch/A1h/1h/Ch[],\nE"));
        expect(bareHold.ok, QStringLiteral("bare touch-hold (no [...] / empty []) accepted as duration 0"));
        int touchHoldCount = 0;
        int holdCount = 0;
        for (const TimelineNoteMarker& marker : bareHold.noteMarkers) {
            if (marker.type == QLatin1String("touch_hold")) {
                ++touchHoldCount;
                expect(
                    nearlyEqual(marker.endSecond, marker.second),
                    QStringLiteral("bare touch-hold tail sits on the head timing"));
            } else if (marker.type == QLatin1String("hold")) {
                ++holdCount;
                expect(
                    nearlyEqual(marker.endSecond, marker.second),
                    QStringLiteral("bare tap-hold tail sits on the head timing"));
            }
        }
        expect(touchHoldCount == 3, QStringLiteral("Ch, A1h, Ch[] all emit touch_hold markers"));
        expect(holdCount == 1, QStringLiteral("1h still emits a single tap hold"));
    }

    {
        const SimaiNativeParseResult zeroSlide = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5[8:0],\nE"));
        const SimaiNativeParseResult zeroHashedSlide = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5[120#0],\nE"));
        expect(!zeroSlide.ok, QStringLiteral("slide rejects zero fraction duration"));
        expect(!zeroHashedSlide.ok, QStringLiteral("slide rejects zero # duration"));
    }

    {
        // Same-lane v slides (XvX = out to center, back to the same lane) are
        // a supported extension shape spliced into slide_data.json. The
        // opposite-lane form Xv(X+4) stays unsupported on purpose — it is
        // geometrically identical to the straight slide X-(X+4).
        const SimaiNativeParseResult sameLaneV = SimaiNativeParser::parseForTimeline(QStringLiteral("1v1[8:1],\nE"));
        expect(sameLaneV.ok, QStringLiteral("timeline parse accepts same-lane v slide 1v1"));
        const TimelineNoteMarker* marker = firstSlideLikeMarker(sameLaneV);
        expect(marker != nullptr, QStringLiteral("same-lane v slide emits a slide marker"));
        if (marker != nullptr) {
            expect(marker->slideTrackKey == QStringLiteral("1v1"), QStringLiteral("same-lane v slide resolves shape key 1v1"));
            expect(marker->endLane == 1, QStringLiteral("same-lane v slide ends on its start lane"));
            expect(
                !marker->slideSegmentPoints.isEmpty() && !marker->slideSegmentPoints.constFirst().isEmpty(),
                QStringLiteral("same-lane v slide carries sampled path geometry"));
        }
        const SimaiNativeParseResult strictSameLaneV = SimaiNativeParser::validateSyntax(QStringLiteral("1v1[8:1],\nE"));
        expect(strictSameLaneV.ok, QStringLiteral("strict validation accepts same-lane v slide 1v1"));
        const SimaiNativeParseResult chainSameLaneV = SimaiNativeParser::validateSyntax(QStringLiteral("5v5v2[8:1],\nE"));
        expect(chainSameLaneV.ok, QStringLiteral("same-lane v slide chains with further segments"));
        const SimaiNativeParseResult oppositeV = SimaiNativeParser::parseForTimeline(QStringLiteral("1v5[8:1],\nE"));
        expect(!oppositeV.ok, QStringLiteral("opposite-lane v slide 1v5 stays rejected (equals 1-5)"));
    }

    {
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(QStringLiteral("C1b,\nE"));
        expect(parsed.ok, QStringLiteral("lenient parse accepts C1 with b modifier"));
        expect(!parsed.noteMarkers.isEmpty(), QStringLiteral("C1 lenient parse emits marker"));
        if (!parsed.noteMarkers.isEmpty()) {
            const TimelineNoteMarker& marker = parsed.noteMarkers.constFirst();
            expect(marker.touchPad == QLatin1String("C"), QStringLiteral("C1 is normalized to C in lenient mode"));
            expect(marker.isBreak, QStringLiteral("touch b modifier binds break flag"));
        }
    }

    {
        // Strict accepts C1/C2 as aliases for C (same normalization as the
        // lenient pass) and flags the non-canonical form as warning-only.
        const SimaiNativeParseResult strictC1 = SimaiNativeParser::validateSyntax(QStringLiteral("C1,\nE"));
        expect(strictC1.ok, QStringLiteral("validate accepts C1/C2 form as a non-canonical alias for C"));
        expect(strictC1.errors.isEmpty(), QStringLiteral("C1/C2 form emits no strict error"));
        expect(
            !strictC1.warnings.isEmpty()
                && strictC1.warnings.constFirst().message.startsWith(QStringLiteral("Non-canonical center touch token")),
            QStringLiteral("validate flags C1/C2 form with a non-canonical touch warning"));
    }

    {
        const SimaiNativeParseResult touchB1x = SimaiNativeParser::validateSyntax(QStringLiteral("B1x,\nE"));
        const SimaiNativeParseResult touchCx = SimaiNativeParser::validateSyntax(QStringLiteral("Cx,\nE"));
        const SimaiNativeParseResult touchC2x = SimaiNativeParser::validateSyntax(QStringLiteral("C2x,\nE"));
        const SimaiNativeParseResult lenientTouchB1x = SimaiNativeParser::parseForTimeline(QStringLiteral("B1x,\nE"));

        expect(!touchB1x.ok, QStringLiteral("validate rejects x after B1 touch"));
        expect(!touchCx.ok, QStringLiteral("validate rejects x after C touch"));
        expect(!touchC2x.ok, QStringLiteral("validate rejects x after C2 touch alias"));
        expect(!lenientTouchB1x.ok, QStringLiteral("timeline parse rejects x after touch"));
    }

    {
        const SimaiNativeParseResult uppercaseEx = SimaiNativeParser::validateSyntax(QStringLiteral("2X,\nE"));
        const SimaiNativeParseResult uppercaseBreak = SimaiNativeParser::validateSyntax(QStringLiteral("2B,\nE"));
        const SimaiNativeParseResult uppercaseSlideHeadEx = SimaiNativeParser::validateSyntax(QStringLiteral("2X-6[8:1],\nE"));
        const SimaiNativeParseResult uppercaseSlideHeadBreak = SimaiNativeParser::validateSyntax(QStringLiteral("2B-6[8:1],\nE"));
        const SimaiNativeParseResult touchB2 = SimaiNativeParser::validateSyntax(QStringLiteral("B2,\nE"));
        const SimaiNativeParseResult touchB4 = SimaiNativeParser::validateSyntax(QStringLiteral("B4,\nE"));

        expect(!uppercaseEx.ok, QStringLiteral("validate rejects uppercase X after a tap lane"));
        expect(!uppercaseBreak.ok, QStringLiteral("validate rejects uppercase B after a tap lane"));
        expect(!uppercaseSlideHeadEx.ok, QStringLiteral("validate rejects uppercase X after a slide head lane"));
        expect(!uppercaseSlideHeadBreak.ok, QStringLiteral("validate rejects uppercase B after a slide head lane"));
        expect(touchB2.ok, QStringLiteral("validate keeps B2 as a valid touch note"));
        expect(touchB4.ok, QStringLiteral("validate keeps B4 as a valid touch note"));
    }

    {
        const SimaiNativeParseResult strictOk = SimaiNativeParser::validateSyntax(QStringLiteral("1-5b[8:1],\nE"));
        const SimaiNativeParseResult strictTailB = SimaiNativeParser::validateSyntax(QStringLiteral("1-5[8:1]b,\nE"));
        const SimaiNativeParseResult strictMidB = SimaiNativeParser::validateSyntax(QStringLiteral("1-5b-1[8:2],\nE"));
        const SimaiNativeParseResult lenientTailB = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5[8:1]b,\nE"));

        expect(strictOk.ok, QStringLiteral("validate accepts break slide b before duration block"));
        expect(strictOk.warnings.isEmpty(), QStringLiteral("canonical break slide b position stays warning-free"));
        // Non-canonical b placement is warning-only; the slide still parses
        // (with trackBreak set) instead of being dropped from the markers.
        expect(strictTailB.ok && strictTailB.errors.isEmpty(),
            QStringLiteral("validate keeps break slide b after duration block warning-only"));
        expect(
            !strictTailB.warnings.isEmpty()
                && strictTailB.warnings.constFirst().message.startsWith(QStringLiteral("Invalid break slide modifier position")),
            QStringLiteral("break slide b after duration block emits position warning"));
        expect(!strictTailB.noteMarkers.isEmpty(), QStringLiteral("non-canonical break slide b still emits slide marker"));
        expect(strictMidB.ok && !strictMidB.warnings.isEmpty(),
            QStringLiteral("validate keeps break slide b inside shape chain warning-only"));
        expect(lenientTailB.ok, QStringLiteral("timeline parse keeps lenient break slide b position"));
    }

    {
        const SimaiNativeParseResult strictFestival = SimaiNativeParser::validateSyntax(QStringLiteral("1-5[8:1]-1[8:2],\nE"));
        // Per-segment ("分段") timing is still flagged as a syntax error in
        // strict mode, but the note is no longer dropped — it folds into the
        // equivalent total-duration slide so the chart still parses.
        expect(!strictFestival.ok, QStringLiteral("validate flags per-segment duration for festival slide"));

        const SimaiNativeParseResult lenientFestival = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5[8:1]-1[8:2],\nE"));
        const SimaiNativeParseResult totalDuration = SimaiNativeParser::parseForTimeline(QStringLiteral("1-5-1[8:3],\nE"));
        expect(lenientFestival.ok, QStringLiteral("timeline parse accepts per-segment duration for festival slide"));
        expect(totalDuration.ok, QStringLiteral("timeline parse accepts total duration festival slide"));

        const TimelineNoteMarker* strictMarker = firstSlideLikeMarker(strictFestival);
        const TimelineNoteMarker* totalMarker = firstSlideLikeMarker(totalDuration);
        expect(strictMarker != nullptr, QStringLiteral("strict per-segment festival slide still emits a marker"));
        if (strictMarker != nullptr && totalMarker != nullptr) {
            expect(nearlyEqual(strictMarker->slideTraceSecond, totalMarker->slideTraceSecond),
                QStringLiteral("strict per-segment festival wait matches the equivalent total slide"));
            expect(nearlyEqual(strictMarker->endSecond, totalMarker->endSecond),
                QStringLiteral("strict per-segment festival end matches the equivalent total slide"));
        }

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
        expect(strictBeatInvalid.ok && strictBeatInvalid.errors.isEmpty(),
            QStringLiteral("validate keeps beat value that is not a positive divisor of 384 warning-only"));
        expect(
            !strictBeatInvalid.warnings.isEmpty()
                && strictBeatInvalid.warnings.constFirst().message.contains(QStringLiteral("positive divisor of 384")),
            QStringLiteral("non-divisor beat value emits 384-divisor warning"));
        expect(strictBeatValid.ok && strictBeatValid.warnings.isEmpty(),
            QStringLiteral("validate accepts beat value that divides 384"));
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
        // A '/' or '`' each-separator must have a note on each side. A dangling
        // divider (",7/," / ",/7,") is a strict-mode syntax error but stays a
        // lenient (timeline) no-op for editing tolerance.
        const SimaiNativeParseResult lenientTrailingSlash = SimaiNativeParser::parseForTimeline(QStringLiteral("1,7/,\nE"));
        const SimaiNativeParseResult strictTrailingSlash = SimaiNativeParser::validateSyntax(QStringLiteral("1,7/,\nE"));
        const SimaiNativeParseResult lenientLeadingSlash = SimaiNativeParser::parseForTimeline(QStringLiteral("1,/7,\nE"));
        const SimaiNativeParseResult strictLeadingSlash = SimaiNativeParser::validateSyntax(QStringLiteral("1,/7,\nE"));
        const SimaiNativeParseResult strictEndOfLineSlash = SimaiNativeParser::validateSyntax(QStringLiteral("1,7/\nE"));
        const SimaiNativeParseResult strictTrailingBacktick = SimaiNativeParser::validateSyntax(QStringLiteral("1,7`,\nE"));
        const SimaiNativeParseResult strictLeadingBacktick = SimaiNativeParser::validateSyntax(QStringLiteral("1,`7,\nE"));

        expect(lenientTrailingSlash.ok, QStringLiteral("lenient parse tolerates trailing slash separator"));
        expect(!strictTrailingSlash.ok, QStringLiteral("validate rejects slash separator missing its right operand"));
        expect(lenientLeadingSlash.ok, QStringLiteral("lenient parse tolerates leading slash separator"));
        expect(!strictLeadingSlash.ok, QStringLiteral("validate rejects slash separator missing its left operand"));
        expect(!strictEndOfLineSlash.ok, QStringLiteral("validate rejects slash separator at end of line"));
        expect(!strictTrailingBacktick.ok, QStringLiteral("validate rejects backtick separator missing its right operand"));
        expect(!strictLeadingBacktick.ok, QStringLiteral("validate rejects backtick separator missing its left operand"));

        // Well-formed each-groups stay valid.
        const SimaiNativeParseResult strictSlashEach = SimaiNativeParser::validateSyntax(QStringLiteral("1/2,\nE"));
        const SimaiNativeParseResult strictBacktickEach = SimaiNativeParser::validateSyntax(QStringLiteral("1`2,\nE"));
        const SimaiNativeParseResult strictTripleEach = SimaiNativeParser::validateSyntax(QStringLiteral("1/2/3,\nE"));
        const SimaiNativeParseResult strictSlideEach = SimaiNativeParser::validateSyntax(QStringLiteral("1-5[8:1]/2-6[8:1],\nE"));
        expect(strictSlashEach.ok, QStringLiteral("validate accepts a well-formed slash each-group"));
        expect(strictBacktickEach.ok, QStringLiteral("validate accepts a well-formed backtick each-group"));
        expect(strictTripleEach.ok, QStringLiteral("validate accepts a chained slash each-group"));
        expect(strictSlideEach.ok, QStringLiteral("validate accepts slash-paired slides"));

        const SimaiNativeValidationReport trailingSlashReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("1,7/,\nE"),
            SimaiNativeValidationLocale::English
        );
        const SimaiNativeValidationReport leadingSlashReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("1,/7,\nE"),
            SimaiNativeValidationLocale::English
        );
        expect(!trailingSlashReport.ok, QStringLiteral("validation report treats trailing slash separator as error"));
        expect(trailingSlashReport.errorCount == 1, QStringLiteral("trailing slash separator counts as one validation error"));
        expect(!leadingSlashReport.ok, QStringLiteral("validation report treats leading slash separator as error"));
        expect(leadingSlashReport.errorCount == 1, QStringLiteral("leading slash separator counts as one validation error"));
    }

    {
        // A directive ({beats}, (bpm), <HS*N>) directly following a note with no
        // ',' in between is a strict-mode warning — usually a forgotten beat
        // separator. The reference repro spans a line break: line 1 ends on a
        // bare note, line 2 opens with the next {beats} block.
        const QString missingCommaPrefix = QStringLiteral("Missing ',' between note and directive: ");
        const SimaiNativeParseResult strictBeatDirective = SimaiNativeParser::validateSyntax(
            QStringLiteral("{16}1,1,1,1,1\n{16},,,\nE"));
        expect(strictBeatDirective.ok && strictBeatDirective.errors.isEmpty(),
            QStringLiteral("note directly before {beats} directive stays warning-only"));
        expect(
            strictBeatDirective.warnings.size() == 1
                && strictBeatDirective.warnings.constFirst().message == missingCommaPrefix + QStringLiteral("{16}"),
            QStringLiteral("note directly before {beats} directive emits one missing-comma warning"));
        if (strictBeatDirective.warnings.size() == 1) {
            expect(
                strictBeatDirective.warnings.constFirst().line == 2
                    && strictBeatDirective.warnings.constFirst().col == 1,
                QStringLiteral("missing-comma warning points at the directive position"));
        }

        const SimaiNativeParseResult strictBeatDirectiveOk = SimaiNativeParser::validateSyntax(
            QStringLiteral("{16}1,1,1,1,1,\n{16},,,\nE"));
        expect(strictBeatDirectiveOk.ok && strictBeatDirectiveOk.warnings.isEmpty(),
            QStringLiteral("trailing ',' before the next {beats} directive stays warning-free"));

        const SimaiNativeParseResult strictBpmDirective = SimaiNativeParser::validateSyntax(
            QStringLiteral("1(120)2,\nE"));
        expect(
            strictBpmDirective.warnings.size() == 1
                && strictBpmDirective.warnings.constFirst().message == missingCommaPrefix + QStringLiteral("(120)"),
            QStringLiteral("note directly before (bpm) directive emits missing-comma warning"));

        const SimaiNativeParseResult strictHsDirective = SimaiNativeParser::validateSyntax(
            QStringLiteral("1<HS*2>2,\nE"));
        expect(
            strictHsDirective.warnings.size() == 1
                && strictHsDirective.warnings.constFirst().message == missingCommaPrefix + QStringLiteral("<HS*2>"),
            QStringLiteral("note directly before <HS*N> directive emits missing-comma warning"));

        // A directive run after one note is a single forgotten ',', not two.
        const SimaiNativeParseResult strictDirectiveRun = SimaiNativeParser::validateSyntax(
            QStringLiteral("1{16}(120)2,\nE"));
        expect(strictDirectiveRun.warnings.size() == 1,
            QStringLiteral("directive run after a note emits one missing-comma warning"));

        // Lenient (timeline) parsing stays quiet.
        const SimaiNativeParseResult lenientBeatDirective = SimaiNativeParser::parseForTimeline(
            QStringLiteral("{16}1,1,1,1,1\n{16},,,\nE"));
        expect(lenientBeatDirective.ok && lenientBeatDirective.warnings.isEmpty(),
            QStringLiteral("lenient parse stays quiet for note directly before directive"));

        const SimaiNativeValidationReport zhDirectiveReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("{16}1,1,1,1,1\n{16},,,\nE"),
            SimaiNativeValidationLocale::Chinese
        );
        expect(
            !zhDirectiveReport.issues.isEmpty()
                && zhDirectiveReport.issues.constFirst().displayMessage.contains(QStringLiteral("缺少分隔符")),
            QStringLiteral("zh report localizes missing-comma-before-directive warning"));
    }

    {
        // A '*' same-head slide branch must omit the slide head: `5q2[4:1]*p8[4:1]`
        // is canonical, `5q2[4:1]*5p8[4:1]` is a strict syntax error (the digit
        // after '*' is not a new head — lenient parsing substitutes the shared
        // head lane, so `*4p8[4:1]` silently becomes 5p8[4:1]).
        const QString starBranchPrefix = QStringLiteral("Invalid '*' slide branch (must omit the slide head): ");
        const SimaiNativeParseResult strictHeadless = SimaiNativeParser::validateSyntax(
            QStringLiteral("5q2[4:1]*p8[4:1],\nE"));
        expect(strictHeadless.ok && strictHeadless.errors.isEmpty(),
            QStringLiteral("validate accepts headless '*' slide branch"));

        const SimaiNativeParseResult strictSameHead = SimaiNativeParser::validateSyntax(
            QStringLiteral("5q2[4:1]*5p8[4:1],\nE"));
        expect(!strictSameHead.ok, QStringLiteral("validate rejects '*' branch repeating the same head digit"));
        expect(
            strictSameHead.errors.size() == 1
                && strictSameHead.errors.constFirst().message.startsWith(starBranchPrefix),
            QStringLiteral("'*' branch with repeated head emits one star-branch error"));

        const SimaiNativeParseResult strictOtherHead = SimaiNativeParser::validateSyntax(
            QStringLiteral("5q2[4:1]*4p8[4:1],\nE"));
        expect(!strictOtherHead.ok, QStringLiteral("validate rejects '*' branch with a different head digit"));

        // Lenient keeps the historical substitution so the chart still previews.
        const SimaiNativeParseResult lenientSameHead = SimaiNativeParser::parseForTimeline(
            QStringLiteral("5q2[4:1]*5p8[4:1],\nE"));
        expect(lenientSameHead.ok, QStringLiteral("lenient parse keeps accepting headed '*' branch"));
        int starSlideCount = 0;
        for (const TimelineNoteMarker& marker : lenientSameHead.noteMarkers) {
            if (marker.type == QLatin1String("slide")) {
                ++starSlideCount;
                expect(marker.lane == 5, QStringLiteral("lenient headed '*' branch keeps the shared head lane"));
            }
        }
        expect(starSlideCount == 2, QStringLiteral("lenient headed '*' branch still emits both slides"));

        const SimaiNativeValidationReport zhStarReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("5q2[4:1]*5p8[4:1],\nE"),
            SimaiNativeValidationLocale::Chinese
        );
        expect(
            !zhStarReport.issues.isEmpty()
                && zhStarReport.issues.constFirst().displayMessage.contains(QStringLiteral("省略 slide 头部")),
            QStringLiteral("zh report localizes '*' slide branch error"));
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
        const SimaiNativeValidationReport zhWarningReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("{10}1,\nE"),
            SimaiNativeValidationLocale::Chinese
        );
        expect(zhWarningReport.ok, QStringLiteral("zh validation report stays ok on warning-only strict issue"));
        if (!zhWarningReport.issues.isEmpty()) {
            expect(
                zhWarningReport.issues.constFirst().displayMessage.startsWith(QStringLiteral("[警告]")),
                QStringLiteral("zh report uses chinese warning prefix for strict-only issue")
            );
            expect(
                zhWarningReport.issues.constFirst().displayMessage.contains(QStringLiteral("分拍数值可能导致转谱错误")),
                QStringLiteral("zh report localizes strict warning detail")
            );
        }

        // Missing beat separator stays a hard error — validation severity is
        // whatever the strict parser emits; there is no lenient downgrade.
        const SimaiNativeValidationReport zhErrorReport = SimaiNativeParser::buildValidationReport(
            QStringLiteral("1\nE"),
            SimaiNativeValidationLocale::Chinese
        );
        expect(!zhErrorReport.ok, QStringLiteral("zh validation report fails on missing beat separator"));
        if (!zhErrorReport.issues.isEmpty()) {
            expect(
                zhErrorReport.issues.constFirst().displayMessage.startsWith(QStringLiteral("[错误]")),
                QStringLiteral("zh report uses chinese error prefix for strict error")
            );
            expect(
                zhErrorReport.issues.constFirst().displayMessage.contains(QStringLiteral("缺少拍间分隔符")),
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
        // Mine notes (simai `m`). The `m` is accepted on tap / hold / touch /
        // touch-hold / slide; slides set trackMine + headMine. Mines must NOT
        // turn the chart unparseable (the historical motivation for this work).
        const SimaiNativeParseResult tap = SimaiNativeParser::parseForTimeline(QStringLiteral("1m,2bm,3xm,\nE"));
        expect(tap.ok, QStringLiteral("mine taps `1m` / `2bm` / `3xm` parse ok"));
        expect(tap.noteMarkers.size() == 3, QStringLiteral("mine tap chart emits three markers"));
        if (tap.noteMarkers.size() == 3) {
            expect(tap.noteMarkers.at(0).isMine && tap.noteMarkers.at(0).type == QLatin1String("tap"),
                   QStringLiteral("`1m` sets isMine on a tap"));
            expect(tap.noteMarkers.at(1).isMine && tap.noteMarkers.at(1).isBreak,
                   QStringLiteral("`2bm` keeps break + mine together"));
            expect(tap.noteMarkers.at(2).isMine && tap.noteMarkers.at(2).isEx,
                   QStringLiteral("`3xm` keeps ex + mine together"));
        }

        const SimaiNativeParseResult hold = SimaiNativeParser::parseForTimeline(QStringLiteral("1hm[4:1],\nE"));
        expect(hold.ok, QStringLiteral("mine hold `1hm[4:1]` parses ok"));
        const TimelineNoteMarker* holdMarker = firstMarkerOfType(hold, QStringLiteral("hold"));
        expect(holdMarker != nullptr && holdMarker->isMine, QStringLiteral("`1hm` sets isMine on a hold"));

        const SimaiNativeParseResult touch = SimaiNativeParser::parseForTimeline(QStringLiteral("A1m,C2hm[4:1],\nE"));
        expect(touch.ok, QStringLiteral("mine touch `A1m` and touch-hold `C2hm[4:1]` parse ok"));
        const TimelineNoteMarker* touchMarker = firstMarkerOfType(touch, QStringLiteral("touch"));
        const TimelineNoteMarker* touchHoldMarker = firstMarkerOfType(touch, QStringLiteral("touch_hold"));
        expect(touchMarker != nullptr && touchMarker->isMine, QStringLiteral("`A1m` sets isMine on a touch"));
        expect(touchHoldMarker != nullptr && touchHoldMarker->isMine,
               QStringLiteral("`C2hm` sets isMine on a touch-hold"));

        const SimaiNativeParseResult slide = SimaiNativeParser::parseForTimeline(QStringLiteral("1-3[2:1]m,\nE"));
        expect(slide.ok, QStringLiteral("mine slide `1-3[2:1]m` parses ok"));
        const TimelineNoteMarker* slideMarker = firstSlideLikeMarker(slide);
        expect(slideMarker != nullptr, QStringLiteral("mine slide emits a slide marker"));
        if (slideMarker != nullptr) {
            expect(slideMarker->trackMine, QStringLiteral("`1-3[2:1]m` sets trackMine on the slide"));
            expect(slideMarker->headMine, QStringLiteral("`1-3[2:1]m` sets headMine on the slide head star"));
            expect(!slideMarker->slideDisplayKey.contains(QLatin1Char('m')),
                   QStringLiteral("mine `m` is stripped from the slide shape lookup key"));
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
        // Q7: negative HS is accepted by DEFAULT (negative-HS is ON) — it
        // parses and freezes the signed multiplier onto the marker.
        const SimaiNativeParseResult neg = SimaiNativeParser::parseForTimeline(
            QStringLiteral("<HS*-2>1,2,E"));
        expect(neg.ok, QStringLiteral("<HS*-2> parses ok by default (negative HS on)"));
        if (neg.noteMarkers.size() >= 1) {
            expect(nearlyEqual(neg.noteMarkers.at(0).hsMultiplier, -2.0),
                   QStringLiteral("note under <HS*-2> freezes hsMultiplier = -2.0 by default"));
        }
    }

    {
        // Opt-out escape hatch: disabling negative HS restores the strict
        // reject-hs<=0 stance; zero is rejected in BOTH modes. Restore the
        // default (on) afterward so later cases are unaffected.
        SimaiNativeParser::setAllowNegativeHsEnabled(false);
        const SimaiNativeParseResult rejected = SimaiNativeParser::parseForTimeline(
            QStringLiteral("<HS*-2>1,E"));
        bool sawNegError = false;
        for (const SimaiNativeMessage& err : rejected.errors) {
            if (err.message.contains(QStringLiteral("HS"))) {
                sawNegError = true;
                break;
            }
        }
        expect(sawNegError, QStringLiteral("<HS*-2> rejected when negative HS is disabled (opt-out)"));

        SimaiNativeParser::setAllowNegativeHsEnabled(true);
        const SimaiNativeParseResult zero = SimaiNativeParser::parseForTimeline(
            QStringLiteral("<HS*0>1,E"));
        bool sawZeroError = false;
        for (const SimaiNativeMessage& err : zero.errors) {
            if (err.message.contains(QStringLiteral("HS"))) {
                sawZeroError = true;
                break;
            }
        }
        expect(sawZeroError, QStringLiteral("<HS*0> stays rejected even with negative HS on"));
    }

    {
        // Optional real-chart repro: set MIACODE_SIMAI_REPRO_CHART to a
        // maidata.txt containing difficulty 5. This keeps the public test
        // binary deterministic while still allowing local corpus checks.
        QTextStream debug(stdout);
        const QString reproChartPath = QString::fromLocal8Bit(qgetenv("MIACODE_SIMAI_REPRO_CHART"));
        QFile f(reproChartPath);
        if (!reproChartPath.isEmpty() && f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QString full = QString::fromUtf8(f.readAll());
            const int diffStart = full.indexOf(QStringLiteral("&inote_5="));
            if (diffStart >= 0) {
                const int chartStart = diffStart + QString(QStringLiteral("&inote_5=")).length();
                int chartEnd = full.indexOf(QStringLiteral("\nE"), chartStart);
                if (chartEnd < 0) chartEnd = full.size();
                const QString chartText = full.mid(chartStart, chartEnd - chartStart + 2);
                const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(chartText);
                debug << "[debug] optional diff5 repro: ok=" << (parsed.ok ? 1 : 0)
                      << " notes=" << parsed.noteMarkers.size()
                      << " errors=" << parsed.errors.size() << "\n";
                for (const auto& err : parsed.errors) {
                    debug << "  err L" << err.line << ":C" << err.col << " " << err.message << "\n";
                }
                QHash<double, int> hsHist;
                for (const auto& m : parsed.noteMarkers) hsHist[m.hsMultiplier]++;
                debug << "  hs histogram:";
                for (auto it = hsHist.constBegin(); it != hsHist.constEnd(); ++it) {
                    debug << " " << it.key() << "x" << it.value();
                }
                debug << "\n";
                bool sawHs1AfterReset = false;
                bool sawHs2 = false;
                int lastHs1Line = -1;
                for (const auto& m : parsed.noteMarkers) {
                    if (nearlyEqual(m.hsMultiplier, 2.0)) sawHs2 = true;
                    if (sawHs2 && nearlyEqual(m.hsMultiplier, 1.0)) {
                        sawHs1AfterReset = true;
                        lastHs1Line = m.sourceLine;
                        break;
                    }
                }
                expect(sawHs1AfterReset,
                       QStringLiteral("optional diff5 repro: HS=1 reset region has a marker with hsMultiplier=1.0"));
                debug << "  HS=1-after-HS=2 reset note at sourceLine=" << lastHs1Line << "\n";
            } else {
                debug << "[debug] optional diff5 repro: &inote_5= not found, skipping\n";
            }
            f.close();
        }
    }

    {
        // Multi-line repro mirroring the real-chart failure shape: directive on
        // its own line, blank line before, beat block on next line. Reported
        // bug: after `<HS*1>`, notes still rendered at 2x.
        const QString chart = QStringLiteral(
            "(174)\n"
            "{16} 1,,,,,,,,\n"
            "\n"
            "<HS*0.5>\n"
            "{16} 1,,,,,,,,\n"
            "\n"
            "<HS*2>\n"
            "{16} 1,,,,,,,,\n"
            "\n"
            "<HS*1>\n"
            "{16} 1,,,,,,,,\n"
            "E\n"
        );
        const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(chart);
        QTextStream debug(stdout);
        debug << "[debug] multi-line repro: ok=" << (parsed.ok ? 1 : 0)
              << " notes=" << parsed.noteMarkers.size()
              << " errors=" << parsed.errors.size() << "\n";
        for (const auto& err : parsed.errors) {
            debug << "  err L" << err.line << ":C" << err.col << " " << err.message << "\n";
        }
        for (int i = 0; i < parsed.noteMarkers.size(); ++i) {
            const auto& m = parsed.noteMarkers[i];
            debug << "  note[" << i << "] type=" << m.type
                  << " sec=" << m.second
                  << " hs=" << m.hsMultiplier
                  << " L" << m.sourceLine << "C" << m.sourceCol << "\n";
        }
        // The 4 notes should have hsMultipliers: 1.0, 0.5, 2.0, 1.0
        if (parsed.noteMarkers.size() == 4) {
            expect(nearlyEqual(parsed.noteMarkers.at(0).hsMultiplier, 1.0),
                   QStringLiteral("multi-line: note before any HS has hs=1.0"));
            expect(nearlyEqual(parsed.noteMarkers.at(1).hsMultiplier, 0.5),
                   QStringLiteral("multi-line: note after <HS*0.5> has hs=0.5"));
            expect(nearlyEqual(parsed.noteMarkers.at(2).hsMultiplier, 2.0),
                   QStringLiteral("multi-line: note after <HS*2> has hs=2.0"));
            expect(nearlyEqual(parsed.noteMarkers.at(3).hsMultiplier, 1.0),
                   QStringLiteral("multi-line: note after <HS*1> has hs=1.0 (the reported bug)"));
        } else {
            expect(false, QStringLiteral("multi-line: expected 4 notes"));
        }
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
