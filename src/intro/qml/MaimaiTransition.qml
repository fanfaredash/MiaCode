import QtQuick

// Maimai-style screen-wipe transition, recreated from gfdfdxc/maimai-transition.
//
// Timing model: faithful to the original CSS keyframes. Each layer has its
// own delay/duration inside a 2.04s cycle (≈ 123 frames @ 60 fps).
//
// CSS source-of-truth (in seconds):
//   chip   : delay 0.15 dur 0.62 (enter) / delay 1.62  dur 0.42  (exit)
//   base   : delay 0.17 dur 0.80         / delay 1.746 dur 0.294
//   purple : delay 0.17 dur 0.70         / delay 1.746 dur 0.294
//   white  : delay 0.17 dur 0.68         / delay 1.746 dur 0.294
//   slide  : delay 0.17 dur 0.75 (avg)   / delay ~1.60 dur 0.165
//
// The owner drives `cycleStartFrame` — set to the absolute frame at which the
// current cycle began. Set to -1 when no cycle is active (layers off-screen).
//
// `enterTrimFrames` shifts the local clock FORWARD so a cycle can start partway
// through the animation. IntroOverlay sets it for cycle 1 so that cycle begins
// already at the fully-covered "complete pattern" hold (the enter sweep is
// skipped); cycle 2 leaves it at 0 for the full enter→hold→exit.

Item {
    id: root

    property url assetsRoot: ""

    property int frame: 0
    property int cycleStartFrame: -1
    property real fps: 60.0

    // Skip this many enter-phase frames at the start of the current cycle.
    property int enterTrimFrames: 0

    // Real frames the current cycle should occupy. The inner animation is authored
    // across `cycleDuration` (123) frames; setting a different span time-scales the
    // whole cycle (e.g. 154 to stretch a cycle to the 2.567s 转场.mp4). <=0 -> 1:1.
    property int cycleSpanFrames: 123

    // ---------- Native geometry (extracted PNGs are 1920x1080) ----------
    readonly property real layerW: 1920
    readonly property real layerH: 1080
    // CSS off-screen translation values are in 3840x2160 coords; halve them.
    readonly property real baseOffX: 1200    // CSS 2400
    readonly property real baseOffY: 750     // CSS 1500
    readonly property real overShootX: 50    // CSS 100 (purple/white overshoot past 0)
    readonly property real overShootY: 30    // CSS 60
    readonly property real bounceBackX: -12.5 // CSS -25
    readonly property real bounceBackY: -7.5  // CSS -15
    readonly property real slideDiag: 1200   // CSS 2400
    readonly property real slideExitDiag: 1100 // CSS 2200

    // ---------- Timing (frames @ 60 fps) ----------
    // Cycle covers 0..123 frames. Card swap is intended around frame 76.
    readonly property int chipEnterStart:    9    // 0.15s
    readonly property int chipEnterDur:      37   // 0.62s
    readonly property int chipExitStart:     97   // 1.62s
    readonly property int chipExitDur:       25   // 0.42s

    readonly property int baseEnterStart:    10   // 0.17s
    readonly property int baseEnterDur:      48   // 0.80s
    readonly property int baseExitStart:     105  // 1.746s
    readonly property int baseExitDur:       18   // 0.294s

    readonly property int purpleEnterDur:    42   // 0.70s
    readonly property int whiteEnterDur:     41   // 0.68s

    readonly property int slideEnterStart:   10   // 0.17s
    readonly property int slideEnterDur:     45   // 0.75s (jitter centre)
    readonly property int slideExitStart:    95   // 1.583s
    readonly property int slideExitDur:      10   // ~0.165s

    readonly property int cycleDuration:     123  // ~2.04s total

    // ---------- Math helpers ----------
    function clamp(v, lo, hi) { return Math.max(lo, Math.min(hi, v)) }
    function easeOutQuart(t) { t = clamp(t, 0, 1); return 1 - Math.pow(1 - t, 4) }
    function easeOutQuint(t) { t = clamp(t, 0, 1); return 1 - Math.pow(1 - t, 5) }
    function easeInQuart(t)  { t = clamp(t, 0, 1); return t * t * t * t }
    function lerp(a, b, t)   { return a + (b - a) * clamp(t, 0, 1) }

    function localFrame() {
        if (cycleStartFrame < 0) return -1
        // Time-scale the inner 0..cycleDuration animation across cycleSpanFrames
        // real frames, then offset by enterTrimFrames (in inner-frame units).
        var span = cycleSpanFrames > 0 ? cycleSpanFrames : cycleDuration
        return (frame - cycleStartFrame) * (cycleDuration / span) + enterTrimFrames
    }

    // ---------- Base plate translate (TL: −baseOffX,−baseOffY; BR: mirror) ----------
    // CSS keyframes: 0% (off-screen) → 100% (0,0), easeOutQuart.
    // Exit: 0% (0,0) → 100% (off-screen), linear.
    function baseFactor() {
        var lf = localFrame()
        if (lf < 0) return 1.0
        if (lf < baseEnterStart) return 1.0
        if (lf < baseEnterStart + baseEnterDur) {
            var t = easeOutQuart((lf - baseEnterStart) / baseEnterDur)
            return 1.0 - t
        }
        if (lf < baseExitStart) return 0.0
        if (lf < baseExitStart + baseExitDur) {
            return (lf - baseExitStart) / baseExitDur
        }
        return 1.0
    }

    // ---------- Purple/white triangle translate (with overshoot+bounce) ----------
    // CSS converge-TL keyframes (relative to enter duration):
    //   0%   off-screen
    //   65%  overshoot past 0 by (overShoot)
    //   85%  bounce back to (bounceBack)
    //   100% settled at (0, 0)
    // Exit follows base plate exit.
    function _convergeTranslate(enterDur, lf) {
        if (lf < 0)                  return { x: -baseOffX, y: -baseOffY }
        if (lf < baseEnterStart)     return { x: -baseOffX, y: -baseOffY }
        var enterEnd = baseEnterStart + enterDur
        if (lf < enterEnd) {
            var t = (lf - baseEnterStart) / enterDur
            if (t < 0.65) {
                var s1 = easeOutQuart(t / 0.65)
                return { x: lerp(-baseOffX, overShootX, s1),
                         y: lerp(-baseOffY, overShootY, s1) }
            }
            if (t < 0.85) {
                var s2 = (t - 0.65) / 0.20
                return { x: lerp(overShootX, bounceBackX, s2),
                         y: lerp(overShootY, bounceBackY, s2) }
            }
            var s3 = (t - 0.85) / 0.15
            return { x: lerp(bounceBackX, 0, s3),
                     y: lerp(bounceBackY, 0, s3) }
        }
        if (lf < baseExitStart) return { x: 0, y: 0 }
        if (lf < baseExitStart + baseExitDur) {
            var et = (lf - baseExitStart) / baseExitDur
            return { x: -baseOffX * et, y: -baseOffY * et }
        }
        return { x: -baseOffX, y: -baseOffY }
    }

    function purpleTLTranslate() { return _convergeTranslate(purpleEnterDur, localFrame()) }
    function whiteTLTranslate()  { return _convergeTranslate(whiteEnterDur,  localFrame()) }

    // ---------- Slide / HOLD translate + opacity ----------
    // Enter: from diagonal off-screen → (0,0), easeOutQuint, opacity fades in at 18% mark.
    // Exit: from (0,0) → continue in same direction off-screen, easeInQuart, fade out.
    function holdTranslate() {
        var lf = localFrame()
        if (lf < 0) return { x: slideDiag, y: -slideDiag }
        if (lf < slideEnterStart) return { x: slideDiag, y: -slideDiag }
        if (lf < slideEnterStart + slideEnterDur) {
            var t = easeOutQuint((lf - slideEnterStart) / slideEnterDur)
            return { x: slideDiag * (1 - t), y: -slideDiag * (1 - t) }
        }
        if (lf < slideExitStart) return { x: 0, y: 0 }
        if (lf < slideExitStart + slideExitDur) {
            var et = easeInQuart((lf - slideExitStart) / slideExitDur)
            return { x: -slideExitDiag * et, y: slideExitDiag * et }
        }
        return { x: -slideExitDiag, y: slideExitDiag }
    }

    function slideTranslate() {
        var p = holdTranslate()
        return { x: -p.x, y: -p.y }
    }

    function slideOpacity() {
        var lf = localFrame()
        if (lf < 0 || lf < slideEnterStart) return 0
        if (lf < slideEnterStart + slideEnterDur) {
            var t = (lf - slideEnterStart) / slideEnterDur
            if (t < 0.18) return t / 0.18
            return 1.0
        }
        if (lf < slideExitStart) return 1.0
        if (lf < slideExitStart + slideExitDur) {
            return 1.0 - (lf - slideExitStart) / slideExitDur
        }
        return 0.0
    }

    // ---------- Chip (滴拉熊) ----------
    // Enter: 10-key bouncing rotate + scale + opacity.
    // Exit:  6-key inward-then-outward scale + fade.
    function chipInState(t) {
        // Keyframe table: [pct, rotation°, scale, opacity]
        var k = [
            [0.00, -180.0, 4.500, 0.00],
            [0.05, -170.0, 4.300, 0.55],
            [0.18, -140.0, 3.600, 0.85],
            [0.32, -100.0, 2.500, 0.95],
            [0.48,  -55.0, 1.400, 1.00],
            [0.62,  -22.0, 0.960, 1.00],
            [0.74,   -8.0, 0.910, 1.00],
            [0.86,   -2.0, 0.970, 1.00],
            [0.94,    0.0, 1.015, 1.00],
            [1.00,    0.0, 1.000, 1.00]
        ]
        for (var i = 1; i < k.length; i++) {
            if (t <= k[i][0]) {
                var u = (t - k[i-1][0]) / (k[i][0] - k[i-1][0])
                return { rot:   lerp(k[i-1][1], k[i][1], u),
                         scale: lerp(k[i-1][2], k[i][2], u),
                         op:    lerp(k[i-1][3], k[i][3], u) }
            }
        }
        return { rot: 0, scale: 1.0, op: 1.0 }
    }

    function chipOutState(t) {
        // Exit = scale up while fading out, with the OPACITY reaching 0 BEFORE the
        // base-plate retract opens the centre to the black playfield (~t 0.32 here,
        // = the retract onset). The 滴拉熊 centre is transparent, so once the centre
        // uncovers black, ANY drawn chip (op>0) reads as a black-filled bear through
        // that hole. Fading to 0 first means the bear has popped + dissolved over the
        // lavender pattern and is GONE before the reveal — no black bear.
        // Keyframes: [pct, scale, opacity].
        var k = [
            [0.00, 1.00, 1.00],
            [0.12, 1.35, 0.55],
            [0.22, 1.70, 0.22],
            [0.32, 2.05, 0.00],
            [1.00, 4.60, 0.00]
        ]
        for (var i = 1; i < k.length; i++) {
            if (t <= k[i][0]) {
                var u = (t - k[i-1][0]) / (k[i][0] - k[i-1][0])
                return { rot:   0,
                         scale: lerp(k[i-1][1], k[i][1], u),
                         op:    lerp(k[i-1][2], k[i][2], u) }
            }
        }
        return { rot: 0, scale: 4.6, op: 0.0 }
    }

    function chipState() {
        var lf = localFrame()
        if (lf < 0)                  return { rot: 0,    scale: 4.5, op: 0 }
        if (lf < chipEnterStart)     return { rot: -180, scale: 4.5, op: 0 }
        if (lf < chipEnterStart + chipEnterDur) {
            return chipInState((lf - chipEnterStart) / chipEnterDur)
        }
        if (lf < chipExitStart)      return { rot: 0, scale: 1.0, op: 1.0 }
        if (lf < chipExitStart + chipExitDur) {
            return chipOutState((lf - chipExitStart) / chipExitDur)
        }
        return { rot: 0, scale: 4.6, op: 0 }
    }

    // ---------- Layered rendering ----------
    // Bottom → top: base, hold, slide, purple, white, chip.
    Image {
        id: baseTL
        source: assetsRoot + "/transition/base_TL.png"
        width: root.layerW; height: root.layerH
        x: -root.baseOffX * root.baseFactor()
        y: -root.baseOffY * root.baseFactor()
        smooth: true; mipmap: true
    }
    Image {
        id: baseBR
        source: assetsRoot + "/transition/base_BR.png"
        width: root.layerW; height: root.layerH
        x: root.baseOffX * root.baseFactor()
        y: root.baseOffY * root.baseFactor()
        smooth: true; mipmap: true
    }

    Image {
        id: hold
        source: assetsRoot + "/transition/hold.png"
        width: root.layerW; height: root.layerH
        x: root.holdTranslate().x
        y: root.holdTranslate().y
        opacity: root.slideOpacity()
        smooth: true; mipmap: true
        visible: opacity > 0
    }
    Image {
        id: slide
        source: assetsRoot + "/transition/slide.png"
        width: root.layerW; height: root.layerH
        x: root.slideTranslate().x
        y: root.slideTranslate().y
        opacity: root.slideOpacity()
        smooth: true; mipmap: true
        visible: opacity > 0
    }

    Image {
        id: purpleTL
        source: assetsRoot + "/transition/purple_TL.png"
        width: root.layerW; height: root.layerH
        x: root.purpleTLTranslate().x
        y: root.purpleTLTranslate().y
        smooth: true; mipmap: true
    }
    Image {
        id: purpleBR
        source: assetsRoot + "/transition/purple_BR.png"
        width: root.layerW; height: root.layerH
        x: -root.purpleTLTranslate().x
        y: -root.purpleTLTranslate().y
        smooth: true; mipmap: true
    }

    Image {
        id: whiteTL
        source: assetsRoot + "/transition/white_TL.png"
        width: root.layerW; height: root.layerH
        x: root.whiteTLTranslate().x
        y: root.whiteTLTranslate().y
        smooth: true; mipmap: true
    }
    Image {
        id: whiteBR
        source: assetsRoot + "/transition/white_BR.png"
        width: root.layerW; height: root.layerH
        x: -root.whiteTLTranslate().x
        y: -root.whiteTLTranslate().y
        smooth: true; mipmap: true
    }

    Image {
        id: chip
        source: assetsRoot + "/transition/chip.png"
        width: root.layerW; height: root.layerH
        x: 0; y: 0
        opacity: root.chipState().op
        smooth: true; mipmap: true
        visible: opacity > 0
        transform: [
            Scale {
                origin.x: root.layerW / 2
                origin.y: root.layerH / 2
                xScale: root.chipState().scale
                yScale: root.chipState().scale
            },
            Rotation {
                origin.x: root.layerW / 2
                origin.y: root.layerH / 2
                angle: root.chipState().rot
            }
        ]
    }
}
