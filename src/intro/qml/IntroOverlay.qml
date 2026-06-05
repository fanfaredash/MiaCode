import QtQuick
import QtQuick.Effects

// Integrated chart-export intro overlay: maimai wipe -> live banner card ->
// maimai wipe, composited OVER the chart export frame (mounted by the export
// session in P3). For now it is self-contained (renders over its own backdrop).
//
// Difference vs the tools/intro_remotion LvCardIntro prototype:
//   • the card is the LIVE MaimaiBannerCard (driven by `bannerTrack` from the
//     chart) rendered transparent and composited here — no pre-rendered PNG.
//   • P0 micro-tweak: cycle 1 starts already at the held "complete maimai
//     pattern" pause (MaimaiTransition.enterTrimFrames skips the enter sweep),
//     then retracts to reveal the card. Cycle 2 plays the full enter→exit.
//
// Layering (bottom → top):
//   1. black base                 — opaque through the intro-only phase, then
//      cut at hideAbs so the chart underneath shows during cycle 2.
//   2. blurred 曲绘 backdrop + dim — fade in/out with the card (cardOpacity).
//   3. MaimaiBannerCard           — transparent card, centered, cardOpacity.
//   4. MaimaiTransition           — the wipe, on top throughout.
//
// Timeline @ 60 fps (DurationFrames = cycle2End = 349 ≈ 5.82s):
//   0..35     cycle 1: held full pattern -> retract (enter sweep trimmed, ~0.6s)
//   15..27    card + backdrop dissolve in (ease-out, 0.25s..0.45s)
//   36..194   card-alone hold (0.6s..3.25s)
//   195       hideAbs: card HARD-CUT -> whole screen flips to the flat base-plate
//             colour (transitionBaseColor) in one frame; the cycle-2 wipe begins on it
//   195..348  cycle 2 (3.25s..5.817s): wipe enters over the flat backing -> hold ->
//             retract; spans 154 frames = 2.567s (the given 转场.mp4). The backing
//             stays behind the wipe through the whole cover so the plate seam never
//             leaks a dark line.
//   326       revealStart: flat backing + black base drop at the retract onset, so
//             the chart PLAYFIELD is uncovered as the wipe opens
//   349       cycle2End == hand-off; chart "曲绘" bg fade (5.6s..6.6s) is applied
//             DOWNSTREAM in ffmpeg and runs past this point.

Item {
    id: root

    property int frame: 0
    property real fps: 60

    // The chart 曲绘 (background image). Empty -> miacode logo fallback.
    property url backgroundImage: ""
    property url logoImage: "qrc:/icons/app.png"

    // Banner payload pulled from the chart (title/artist/designer/level/
    // difficulty/bpm/mode), forwarded to the embedded MaimaiBannerCard.
    property var bannerTrack: ({})
    property url bannerTemplate: "qrc:/intro/templates/maimai_banner.json"
    // Parsed banner template, injected from C++ (avoids the async XHR that never
    // completes under the headless export render loop).
    property var bannerTemplateData: null

    // Dim tint over the blurred backdrop so the crisp card reads.
    property color backdropColor: "#0A0414"

    // Flat fill the maimai wipe composites OVER (its own base-plate colour, sampled
    // from transition/base_TL.png). Used as the cycle-2 hard-cut colour AND as the
    // backing behind the wipe in BOTH cycles, so the plate-seam never leaks a dark
    // line (the 981bdf8 seam-seal only zeroes leak at exact closure; matching the
    // backdrop to the plate makes any residual leak invisible regardless).
    property color transitionBaseColor: "#CEA5FC"

    // Animated micro-motion texture on the blurred backdrop (backdrop layer ONLY,
    // below the card). 0 = grain, 1 = warp, 2 = caustic. bgTexAmp scales strength.
    property int  bgTexMode: 2
    property real bgTexAmp:  1.5

    implicitWidth: 1920
    implicitHeight: 1080
    width: implicitWidth
    height: implicitHeight

    readonly property url effectiveJacket:
        (backgroundImage.toString().length > 0) ? backgroundImage
                                                 : (logoImage.toString().length > 0 ? logoImage : "")

    // ---------- Cycle layout ----------
    // The wipe authors its inner animation across `cycleDuration` (123) frames;
    // each cycle is time-scaled to occupy `cycleNSpanFrames` REAL frames via
    // MaimaiTransition.cycleSpanFrames (so cycle 2 can match the 2.567s 转场.mp4).
    readonly property int cycleDuration: 123
    // Frames of the enter sweep skipped so cycle 1 opens at the full-cover hold.
    readonly property int cycle1EnterTrim: 58

    readonly property int cycle1Start:      0
    readonly property int cycle1SpanFrames: 68                     // full-cycle real span; trimmed part plays ~0.6s
    readonly property int cycle1End:        36                     // retract done (~0.6s)
    readonly property int cycle2Start:      195                    // 3.25s
    readonly property int cycle2SpanFrames: 154                    // 2.567s == given 转场.mp4
    readonly property int cycle2End:        cycle2Start + cycle2SpanFrames // 349, wipe fully retracted (5.817s)

    // ---------- Reveal timing ----------
    // cardStartAbs / cardRevealFrames drive the blurred 曲绘 BACKDROP + dim only
    // (they ease in under the wipe and are revealed as it retracts).
    readonly property int cardStartAbs:     15  // backdrop ease-in begins (0.25s)
    readonly property int cardRevealFrames: 12  // backdrop ease-in dur
    // The CARD itself builds via a STAGGERED per-part fade-in (frame -> jacket ->
    // level -> text, see MaimaiBannerCard.partOpacity). It begins AFTER the cycle-1
    // wipe has nearly retracted so the assembly happens in FULL VIEW (like the maimai
    // reference), not hidden under the cover. ~34..50.
    readonly property int cardRevealStart:  34
    // At cycle 2 the card is INSTANT-CUT (no dissolve): frame 195 flips the whole
    // screen to the solid transition bg (backdropColor), the wipe plays on it, and
    // at the merge (full cover) the solid bg + black base drop to reveal the chart.
    readonly property int hideAbs:          cycle2Start              // 195, card hard-cut at 3.25s
    readonly property int mergeFrame:       cycle2Start + 75         // 270, wipe fully covers (merge)
    // The solid transition bg backs the wipe through the WHOLE cover (no dark seam),
    // then it + the opaque black base both drop at the cycle-2 retract onset so the
    // chart PLAYFIELD is uncovered as the wipe opens. Between merge (270) and retract
    // the wipe fully covers, so the playfield stays hidden either way — keeping the
    // flat backing to the retract just seals the seam during the hold. The "曲绘"
    // fade-from-black is DOWNSTREAM in ffmpeg and extends past the hand-off
    // (KEEP IN SYNC with IntroConfig.h).
    readonly property int revealStart:      cycle2Start + 131        // 326, wipe retract onset (~5.43s)
    readonly property int durationFrames:   cycle2End                // 349 (hand-off == cycle2End)

    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
    function easeOutCubic(t)  { t = clamp(t, 0, 1); return 1 - Math.pow(1 - t, 3) }

    // Ease-out dissolve that plays as the cover clears, hold, then hard-cut out
    // behind cycle 2 so the card/backdrop give way to the chart.
    function cardOpacity() {
        if (frame < cardStartAbs) return 0.0
        if (frame < cardStartAbs + cardRevealFrames)
            return easeOutCubic((frame - cardStartAbs) / cardRevealFrames)
        if (frame < hideAbs) return 1.0
        return 0.0
    }

    // The opaque black base fills gaps behind the wipe + card while the screen is
    // covered, then drops at the cycle-2 retract so the wipe uncovers the chart
    // playfield (outline + notes + HUD). The background itself is blacked/faded
    // downstream in the ffmpeg base, so the playfield/HUD here are unaffected.
    function baseOpacity() {
        return frame < revealStart ? 1.0 : 0.0
    }

    function currentCycleStart() {
        if (frame < cycle1End) return cycle1Start     // [0, 65)
        if (frame < cycle2Start) return -1            // card-alone window
        if (frame < cycle2End) return cycle2Start     // [168, 291)
        return -1
    }

    // ---------- Layers (bottom -> top) ----------
    Rectangle {
        id: blackBase
        anchors.fill: parent
        color: "#000000"
        opacity: root.baseOpacity()
        visible: opacity > 0
    }

    // Flat base-plate-coloured backing the wipe composites OVER. It sits ABOVE the
    // black base but BELOW the card/playfield so it only ever shows THROUGH the wipe
    // (at the diagonal plate seam) — making any residual seam leak the plate colour
    // instead of black. Opaque through both wipe COVER phases:
    //   • cycle 1 [0, cycle1End): card has not dissolved in yet, so frame 0's held
    //     full-cover composites over lavender, not black (no first-frame seam line).
    //   • cycle 2 [cycle2Start, revealStart): doubles as the 3.25s HARD-CUT colour
    //     (card cut to 0 at hideAbs) and seals the seam through the whole hold.
    // It drops at the cycle-2 retract onset together with the black base.
    Rectangle {
        id: transitionBg
        anchors.fill: parent
        color: root.transitionBaseColor
        opacity: ((root.frame < root.cycle1End)
                  || (root.frame >= root.cycle2Start && root.frame < root.revealStart)) ? 1.0 : 0.0
        visible: opacity > 0
    }

    // 1) Blurred 曲绘 background filling the whole 16:9 frame, with a subtle ANIMATED
    //    micro-motion texture (grain/warp/caustic). The texture is applied ONLY to
    //    this backdrop layer (rendered below the card), so the difficulty card is
    //    never affected. Pipeline: jacket -> blur (to texture) -> shader -> draw.
    Image {
        id: bgFill
        anchors.fill: parent
        source: root.effectiveJacket
        fillMode: Image.PreserveAspectCrop
        visible: false
        asynchronous: false
        smooth: true
        mipmap: true
    }
    MultiEffect {
        id: blurredBg
        anchors.fill: parent
        source: bgFill
        blurEnabled: true
        blur: 1.0
        blurMax: 64
        // Captured by blurredTex below (hideSource); not drawn directly.
    }
    ShaderEffectSource {
        id: blurredTex
        anchors.fill: parent
        sourceItem: blurredBg
        live: true
        hideSource: true
        visible: false
    }
    ShaderEffect {
        anchors.fill: parent
        property variant source: blurredTex
        property real time: root.frame / Math.max(1, root.fps)
        property real amp: root.bgTexAmp
        property int mode: root.bgTexMode
        opacity: root.cardOpacity()
        visible: opacity > 0 && bgFill.status === Image.Ready
        fragmentShader: "qrc:/src/intro/shaders/bg_texture.frag.qsb"
    }
    Rectangle {
        anchors.fill: parent
        color: root.backdropColor
        opacity: root.cardOpacity() * 0.45
        visible: opacity > 0
    }

    // 2) Live banner card, centered at a square the height of the frame,
    //    floating on the backdrop. Renders transparent (template flag), so the
    //    blurred backdrop shows around it.
    MaimaiBannerCard {
        id: cardImg
        anchors.centerIn: parent
        height: parent.height
        width: parent.height
        frame: root.frame
        fps: root.fps
        templateSource: root.bannerTemplate
        externalTemplate: root.bannerTemplateData
        jacketImage: root.backgroundImage
        logoImage: root.logoImage
        trackOverrides: root.bannerTrack
        // The card reveals via a STAGGERED per-part fade-in (frame -> jacket ->
        // level -> text), driven internally from cardRevealStart (AFTER the wipe
        // clears, so it's in view) — the master opacity here only gates presence +
        // the cycle-2 hard-cut, NOT the fade-in (else the two would multiply). The
        // blurred backdrop/dim use cardOpacity() (earlier, under the wipe).
        revealStartFrame: root.cardRevealStart
        opacity: (root.frame >= root.cardRevealStart && root.frame < root.hideAbs) ? 1.0 : 0.0
        visible: opacity > 0
    }

    MaimaiTransition {
        id: transition
        anchors.fill: parent
        assetsRoot: "qrc:/intro/assets"
        frame: root.frame
        cycleStartFrame: root.currentCycleStart()
        enterTrimFrames: root.currentCycleStart() === root.cycle1Start ? root.cycle1EnterTrim : 0
        cycleSpanFrames: root.currentCycleStart() === root.cycle1Start ? root.cycle1SpanFrames : root.cycle2SpanFrames
        fps: root.fps
    }
}
