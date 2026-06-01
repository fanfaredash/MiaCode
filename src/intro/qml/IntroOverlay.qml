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
// Timeline @ 60 fps (DurationFrames = cycle2End = 291 ≈ 4.85s):
//   0..64     cycle 1: held full pattern -> retract (enter sweep trimmed)
//   48..84    card + backdrop dissolve in (ease-out, ~0.6s)
//   84..167   card-alone hold
//   168..290  cycle 2: cover re-enters (chart starts under) -> hold -> retract
//   226       hideAbs: card + backdrop cut out so the chart shows through

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

    // Dim tint over the blurred backdrop so the crisp card reads.
    property color backdropColor: "#0A0414"

    implicitWidth: 1920
    implicitHeight: 1080
    width: implicitWidth
    height: implicitHeight

    readonly property url effectiveJacket:
        (backgroundImage.toString().length > 0) ? backgroundImage
                                                 : (logoImage.toString().length > 0 ? logoImage : "")

    // ---------- Cycle layout ----------
    readonly property int cycleDuration: 123
    // Frames of the enter sweep skipped so cycle 1 opens at the full-cover hold.
    readonly property int cycle1EnterTrim: 58

    readonly property int cycle1Start: 0
    readonly property int cycle1End:   65                          // retract done
    readonly property int cycle2Start: 168
    readonly property int cycle2End:   cycle2Start + cycleDuration // 291 == DurationFrames
    readonly property int durationFrames: cycle2End

    // ---------- Reveal timing (tune these) ----------
    readonly property int cardStartAbs:     48  // dissolve begins as cover clears
    readonly property int cardRevealFrames: 36  // ~0.6s ease-out dissolve
    readonly property int hideAbs:          cycle2Start + 58 // cut behind cycle-2 cover (226)

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

    // The opaque black base stays up through cycle 1 + reveal + hold (so the
    // intro fully covers whatever is behind it), then cuts at hideAbs so the
    // chart rendered underneath shows through cycle 2's retracting wipe.
    function baseOpacity() {
        return frame < hideAbs ? 1.0 : 0.0
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

    // 1) Blurred 曲绘 background filling the whole 16:9 frame.
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
        anchors.fill: parent
        source: bgFill
        blurEnabled: true
        blur: 1.0
        blurMax: 64
        opacity: root.cardOpacity()
        visible: opacity > 0 && bgFill.status === Image.Ready
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
        jacketImage: root.backgroundImage
        logoImage: root.logoImage
        trackOverrides: root.bannerTrack
        opacity: root.cardOpacity()
        visible: opacity > 0
    }

    MaimaiTransition {
        id: transition
        anchors.fill: parent
        assetsRoot: "qrc:/intro/assets"
        frame: root.frame
        cycleStartFrame: root.currentCycleStart()
        enterTrimFrames: root.currentCycleStart() === root.cycle1Start ? root.cycle1EnterTrim : 0
        fps: root.fps
    }
}
