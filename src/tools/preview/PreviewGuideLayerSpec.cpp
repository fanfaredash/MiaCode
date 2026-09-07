// Covers the note-guide each-connector selection in PreviewGuideLayerState.
//
// The connector art is chosen by each-group SIZE (3+ members => the full ring,
// EachLine4.png) and, for pairs, by lane distance. A '*' same-head slide
// expands to one marker per branch, so the group has to be counted in logical
// head stars — not in markers — or a two-note each silently renders as the
// 3+ ring. The assertions identify the picked art by the skin image's width,
// which the fixture makes unique per slot.

#include <QCoreApplication>
#include <QImage>
#include <QRectF>
#include <QTextStream>

#include "core/chart/parser/SimaiNativeParser.h"
#include "core/scene/PreviewActiveMarkerView.h"
#include "core/scene/PreviewGuideLayerState.h"
#include "core/scene/PreviewOpacityCurves.h"
#include "core/scene/PreviewSceneMath.h"

namespace {

using miacode::preview::scene::PreviewActiveMarkerView;
using miacode::preview::scene::PreviewFrameState;
using miacode::preview::scene::PreviewSpriteDescriptors;
using miacode::preview::scene::PreviewTapTiming;

// Fixture widths, one per each-connector slot. `EachLineN` => 50 + N.
constexpr int kEachLine1Width = 51;
constexpr int kEachLine2Width = 52;
constexpr int kEachLine3Width = 53;
constexpr int kEachLine4Width = 54;
constexpr qreal kEpsilon = 1e-3;

bool require(bool condition, const QString& message, QTextStream& err)
{
    if (!condition) {
        err << message << Qt::endl;
        return false;
    }
    return true;
}

QImage solidImage(int width, int height)
{
    QImage image(width, height, QImage::Format_ARGB32_Premultiplied);
    image.fill(qRgba(255, 255, 255, 255));
    return image;
}

void installGuideSkin(PreviewFrameState* state)
{
    state->skin.noteGuideNormalImage = solidImage(40, 40);
    state->skin.noteGuideEachImage = solidImage(41, 41);
    state->skin.noteGuideSlideImage = solidImage(42, 42);
    state->skin.noteGuideHoldEndImage = solidImage(43, 43);
    state->skin.noteGuideHoldEachEndImage = solidImage(44, 44);
    state->skin.noteGuideEachLine1Image = solidImage(kEachLine1Width, kEachLine1Width);
    state->skin.noteGuideEachLine2Image = solidImage(kEachLine2Width, kEachLine2Width);
    state->skin.noteGuideEachLine3Image = solidImage(kEachLine3Width, kEachLine3Width);
    state->skin.noteGuideEachLine4Image = solidImage(kEachLine4Width, kEachLine4Width);
}

struct GuideOutcome {
    int connectorCount = 0;
    int connectorWidth = 0;
    qreal connectorRotationDegrees = 0.0;
    int noteGuideCount = 0;
};

// Builds the guide layer for `chart` with the playhead parked mid-flight
// before the first note, and reports which each-connector art came out.
bool buildGuideOutcome(const QString& chart, GuideOutcome* outcome, QTextStream& err)
{
    const SimaiNativeParseResult parsed = SimaiNativeParser::parseForTimeline(chart);
    if (!require(parsed.ok, QStringLiteral("chart parses: %1").arg(chart), err)) {
        return false;
    }
    if (!require(!parsed.noteMarkers.isEmpty(), QStringLiteral("chart emits notes: %1").arg(chart), err)) {
        return false;
    }

    PreviewFrameState state;
    state.noteMarkers = parsed.noteMarkers;
    installGuideSkin(&state);

    double firstSecond = parsed.noteMarkers.constFirst().second;
    for (const TimelineNoteMarker& marker : parsed.noteMarkers) {
        firstSecond = qMin(firstSecond, marker.second);
    }
    const PreviewTapTiming tapTiming =
        miacode::preview::scene::previewTapTimingForFlowSpeed(static_cast<qreal>(state.render.tapFlowSpeed));
    state.playheadSeconds = firstSecond - tapTiming.flyDurationSeconds * 0.5;

    const PreviewActiveMarkerView markers(state.noteMarkers);
    const QRectF playfieldRect(0.0, 0.0, 1080.0, 1080.0);
    const PreviewSpriteDescriptors sprites =
        miacode::preview::scene::buildPreviewGuideLayerSprites(state, markers, playfieldRect);

    for (const auto& sprite : sprites) {
        if (sprite.image == nullptr) {
            continue;
        }
        const int width = sprite.image->width();
        const bool isConnector = width >= kEachLine1Width && width <= kEachLine4Width;
        if (!isConnector) {
            ++outcome->noteGuideCount;
            continue;
        }
        ++outcome->connectorCount;
        outcome->connectorWidth = width;
        outcome->connectorRotationDegrees = sprite.rotationDegrees;
    }
    return true;
}

// `5h[16:3]/4-2[16:1]*-1[16:1]` is a hold on lane 5 plus ONE head star on
// lane 4 that shoots two slide branches. The each is two logical notes one
// lane apart, so it must draw the EachLine1 connector — the pre-fix layer
// counted both branch markers and drew the 3+ full ring instead.
bool verifyStarBranchSlideCountsAsOneEachMember(QTextStream& err)
{
    GuideOutcome starBranch;
    if (!buildGuideOutcome(
            QStringLiteral("(120){8}5h[16:3]/4-2[16:1]*-1[16:1],\nE"), &starBranch, err)) {
        return false;
    }

    // Baseline: the same lane pair with a plain tap instead of the '*' slide.
    GuideOutcome plainPair;
    if (!buildGuideOutcome(QStringLiteral("(120){8}5h[16:3]/4,\nE"), &plainPair, err)) {
        return false;
    }

    return require(
               starBranch.connectorCount == 1,
               QStringLiteral("'*' same-head each draws exactly one connector (got %1)")
                   .arg(starBranch.connectorCount),
               err)
        && require(
               starBranch.connectorWidth == kEachLine1Width,
               QStringLiteral("'*' same-head each uses the adjacent-lane connector, not the 3+ ring (got width %1)")
                   .arg(starBranch.connectorWidth),
               err)
        && require(
               plainPair.connectorWidth == kEachLine1Width,
               QStringLiteral("baseline hold+tap pair uses the adjacent-lane connector (got width %1)")
                   .arg(plainPair.connectorWidth),
               err)
        && require(
               qAbs(starBranch.connectorRotationDegrees - plainPair.connectorRotationDegrees) <= kEpsilon,
               QStringLiteral("'*' same-head each orients like the equivalent plain pair (%1 vs %2)")
                   .arg(starBranch.connectorRotationDegrees)
                   .arg(plainPair.connectorRotationDegrees),
               err)
        && require(
               starBranch.noteGuideCount == plainPair.noteGuideCount,
               QStringLiteral("'*' same-head star draws one approach guide, like a single note (%1 vs %2)")
                   .arg(starBranch.noteGuideCount)
                   .arg(plainPair.noteGuideCount),
               err);
}

// Guard the other side of the same branch: genuine 3+ eaches and genuine
// opposite-lane pairs must keep the full ring.
bool verifyFullRingStillUsedForRealGroups(QTextStream& err)
{
    GuideOutcome tripleTap;
    if (!buildGuideOutcome(QStringLiteral("(120){8}1/4/7,\nE"), &tripleTap, err)) {
        return false;
    }
    GuideOutcome oppositePair;
    if (!buildGuideOutcome(QStringLiteral("(120){8}1/5,\nE"), &oppositePair, err)) {
        return false;
    }
    // Two DIFFERENT head stars in one each stay two members.
    GuideOutcome twoSlides;
    if (!buildGuideOutcome(QStringLiteral("(120){8}1-3[16:1]/5-7[16:1],\nE"), &twoSlides, err)) {
        return false;
    }

    return require(
               tripleTap.connectorCount == 1 && tripleTap.connectorWidth == kEachLine4Width,
               QStringLiteral("three-note each keeps the full ring (count %1, width %2)")
                   .arg(tripleTap.connectorCount)
                   .arg(tripleTap.connectorWidth),
               err)
        && require(
               oppositePair.connectorWidth == kEachLine4Width,
               QStringLiteral("opposite-lane pair keeps the diameter connector (width %1)")
                   .arg(oppositePair.connectorWidth),
               err)
        && require(
               twoSlides.connectorCount == 1 && twoSlides.connectorWidth == kEachLine4Width,
               QStringLiteral("two distinct slide heads stay a two-member each (count %1, width %2)")
                   .arg(twoSlides.connectorCount)
                   .arg(twoSlides.connectorWidth),
               err);
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    QTextStream out(stdout);

    if (!verifyStarBranchSlideCountsAsOneEachMember(err)) {
        return 1;
    }
    if (!verifyFullRingStillUsedForRealGroups(err)) {
        return 1;
    }

    out << "preview_guide_layer_spec ok" << Qt::endl;
    return 0;
}
