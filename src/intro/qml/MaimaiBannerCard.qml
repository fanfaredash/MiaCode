// Maimai chart keyframe banner using the trackstart asset family
// (UI_TST_MBase_*) from Resources/process/trackstart/sprites. These ship full
// per-difficulty cards (5 colors), per-difficulty LV pills, baked-in difficulty
// labels ("MASTER" / "ADVANCED" etc), and the rainbow DX badge — so the only
// dynamic elements are: jacket, LV number, title, artist, designer, BPM.
//
// Layout coords are expressed in the TST card's native 420×636 space, scaled
// up to fill `card.heightRatio` of the 1080×1080 canvas.
//
// Integrated-export edge cases (vs the tools/intro_remotion prototype):
//   • title / artist / designer auto-shrink to a width cap then elide, and any
//     newline is filtered to a space (oneLine()), so a runaway metadata string
//     can never break the layout or wrap to a second line.
//   • LV accepts only [0-9] and '+' (lvDigitSequence drops everything else);
//     the prefab atlas has no glyph for arbitrary text.
//   • a missing 曲绘 (jacketImage) falls back to the miacode logo (logoImage).

import QtQuick
import QtQuick.Effects
import QtQuick.Window

Item {
    id: root

    // Exporter contract — same property names as the prototype.
    property int frame: 0
    property real fps: 30
    // Authoring frame at which the staggered per-part card reveal begins; -1 shows
    // the card fully (no fade-in). The maimai track-start card does NOT fade as one
    // unit — frame/jacket/level/text appear with different delays (see partOpacity).
    property int revealStartFrame: -1
    property url templateSource: Qt.resolvedUrl("../templates/maimai_banner.json")
    property url backgroundImage: ""
    property url jacketImage: ""
    // Fallback art when no 曲绘 is available (see effectiveJacket).
    property url logoImage: "qrc:/icons/app.png"
    property var trackOverrides: ({})
    // When the host supplies a parsed template object (the integrated export
    // injects it from C++ so it doesn't depend on an async XMLHttpRequest that
    // never fires under the headless QQuickRenderControl loop), use it verbatim
    // and skip the templateSource XHR entirely.
    property var externalTemplate: null
    property var template: defaultTemplate()

    // When true, the outer blurred-jacket backdrop + dim + black base are skipped,
    // leaving only the card (rounded frame + tab) with alpha around it. Used to
    // composite the card over IntroOverlay's own blurred 曲绘 backdrop. Read from
    // the template (the vendored maimai_banner.json sets transparentBackground).
    property bool transparentBackground: (root.template && root.template.transparentBackground === true)

    // Device-pixel ratio of the surface we render onto (2.0 on a HiDPI grab,
    // 1.0 on a plain export). The jacket-frame snap below folds this in so the
    // four edges land on whole DEVICE pixels, not just whole logical pixels.
    readonly property real renderDpr: Math.max(1, Screen.devicePixelRatio)

    // Resolved jacket: the chart 曲绘 if present, else the miacode logo.
    readonly property url effectiveJacket:
        (jacketImage.toString().length > 0) ? jacketImage
                                            : (logoImage.toString().length > 0 ? logoImage : "")

    // FontLoaders for the free OFL substitute of the prefab's SEGA proprietary
    // fonts. Resource Han Rounded CN (思源圆体, rounded Source Han) natively covers
    // simplified Chinese (no system fallback) and approximates SEGA's rounded look.
    // Title (display) uses Heavy for prominence; body uses Bold.
    FontLoader {
        id: displayFont
        source: Qt.resolvedUrl(
            (root.template.fontsRoot || "qrc:/intro/assets/fonts") + "/" +
            (root.template.fonts ? root.template.fonts.display : "ResourceHanRoundedCN-Heavy.ttf")
        )
    }
    FontLoader {
        id: bodyFont
        source: Qt.resolvedUrl(
            (root.template.fontsRoot || "qrc:/intro/assets/fonts") + "/" +
            (root.template.fonts ? root.template.fonts.body : "ResourceHanRoundedCN-Bold.ttf")
        )
    }

    implicitWidth: template.canvas.width
    implicitHeight: template.canvas.height
    width: implicitWidth
    height: implicitHeight

    function defaultTemplate() {
        return {
            canvas: { width: 1080, height: 1080, fps: 30, durationFrames: 1 },
            card: {
                nativeWidth: 420, nativeHeight: 636,
                heightRatio: 0.86, topMargin: 0.05,
                shadowBlur: 56, shadowColor: "#000000", shadowOpacity: 0.55
            },
            assetsRoot: "qrc:/intro/assets/trackstart",
            lvAtlasRoot: "qrc:/intro/assets/lv_atlas",
            fontsRoot: "qrc:/intro/assets/fonts",
            assets: {
                frame: {}, tab: {}, lvPill: {},
                plateDeluxe: "", plateStandard: ""
            },
            layout: {
                mbaseTab:      { x: 0,   y: -36,  w: 420, h: 96 },
                plateDeluxe:   { x: 13,  y: -31,  w: 200, h: 64 },
                plateStandard: { x: 207, y: -31,  w: 200, h: 64 },
                jacketSlot:    { x: 50,  y: 43,   w: 320, h: 320 },
                lvPill:        { x: 223, y: 307,  w: 180, h: 112 },
                lvLabel:       { x: 272, y: 354,  w: 48,  h: 60 },
                lvNumber:      { x: 315, y: 358,  w: 70,  h: 55 },
                lvPlus:        { x: 380, y: 343,  w: 28,  h: 36 },
                titleArea:     { x: 17,  y: 432,  w: 386, h: 33 },
                artistArea:    { x: 17,  y: 480,  w: 386, h: 20 },
                uiAchievement: { x: 26,  y: 518,  w: 161, h: 30 },
                uiRank:        { x: 185, y: 518,  w: 85,  h: 30 },
                imgApFcMedal:  { x: 262, y: 511,  w: 80,  h: 80 },
                imgSyncMedal:  { x: 329, y: 511,  w: 80,  h: 80 },
                designerLabel: { x: 28,  y: 572,  w: 121, h: 25 },
                designerName:  { x: 28,  y: 599,  w: 243, h: 16 },
                bpmText:       { x: 302, y: 599,  w: 100, h: 17 }
            },
            colors: {
                titleOnDark: "#FFFFFF", artistOnDark: "#E8E4F5",
                designerOnWhite: "#1A1A1A", bpmOnWhite: "#0E2A60",
                labelOnWhite: "#0E2A60",
                lvNumber: "#FFFFFF", lvNumberShadow: "#3A1060",
                placeholderFill: "#DDD9D2", placeholderEdge: "#DDD9D2"
            },
            background: { useJacketAsBackdrop: true, dimColor: "#0A0414", dimOpacity: 0.55, blurAmount: 0.9 },
            font: { family: "Yu Gothic UI" },
            track: {
                title: "(title)", artist: "(artist)", designer: "(designer)",
                level: "?", difficulty: "MASTER", bpm: "0", mode: "DX"
            },
            transparentBackground: true
        }
    }

    function loadTemplate() {
        var xhr = new XMLHttpRequest()
        xhr.open("GET", templateSource)
        xhr.onreadystatechange = function() {
            if (xhr.readyState === XMLHttpRequest.DONE && xhr.status === 200) {
                template = JSON.parse(xhr.responseText)
            }
        }
        xhr.send()
    }

    function trackValue(name) {
        if (trackOverrides && trackOverrides[name] !== undefined)
            return trackOverrides[name]
        return template.track[name]
    }

    // Collapse any newline (CR/LF) to a single space and trim — a metadata
    // string with embedded line breaks must never wrap or break the card.
    function oneLine(value) {
        return (value === undefined || value === null ? "" : value.toString())
            .replace(/[\r\n]+/g, " ")
            .replace(/\s+/g, " ")
            .trim()
    }

    // ----- Staggered per-part reveal -----
    // Replicates the maimai track-start card fade-in: structural parts (frame, then
    // jacket) appear first, then the LV, then the text/info rows — each its own
    // delayed ease-out ramp rather than the whole card fading as one unit. Delays /
    // durations are in AUTHORING frames (the `frame` property), relative to
    // `revealStartFrame`. Returns 1.0 when no reveal is set (revealStartFrame < 0).
    function _clamp01(v) { return Math.max(0, Math.min(1, v)) }
    // smoothstep (ease-in-out) — gentle start AND end, no abrupt onset.
    function _smooth(t) { t = _clamp01(t); return t * t * (3 - 2 * t) }
    function partOpacity(delayF, durF) {
        if (revealStartFrame < 0) return 1.0
        return _smooth((frame - revealStartFrame - delayF) / durF)
    }

    // Per-part reveal stages: [delayFrames, durationFrames] relative to
    // revealStartFrame (authoring fps). Tuned to the maimai reference: a smooth,
    // HEAVILY-OVERLAPPING materialize (long durations, small delay spread) so there
    // are no choppy pops — structure (frame) leads only slightly, with the title
    // essentially alongside it. Tune here in one place.
    readonly property var revealStage: ({
        "frame":  [0, 11],
        "jacket": [0, 12],
        "title":  [1, 11],
        "tab":    [0, 11],
        "level":  [4, 11],
        "artist": [4, 11],
        "bars":   [6, 12],
        "info":   [7, 12]
    })
    function stageOpacity(name) {
        var s = revealStage[name]
        return s ? partOpacity(s[0], s[1]) : 1.0
    }

    // ----- Card pop (scale up then settle) -----
    // The whole card overshoots slightly larger as it materialises, then settles to
    // 1.0 (calibrated to the maimai reference's subtle ~3-4% bump). Centred on the
    // card so it grows/shrinks in place. Tune amplitude/timing here.
    property real cardPopStart: 0.95   // scale at reveal start
    property real cardPopPeak:  1.05   // overshoot peak (>1)
    property int  cardPopGrowFrames:   8
    property int  cardPopSettleFrames: 9
    function cardPopScale() {
        if (revealStartFrame < 0) return 1.0
        var f = frame - revealStartFrame
        if (f <= 0) return cardPopStart
        if (f < cardPopGrowFrames)
            return cardPopStart + (cardPopPeak - cardPopStart) * _smooth(f / cardPopGrowFrames)
        var g = f - cardPopGrowFrames
        if (g < cardPopSettleFrames)
            return cardPopPeak + (1.0 - cardPopPeak) * _smooth(g / cardPopSettleFrames)
        return 1.0
    }

    function assetUrl(filename) {
        if (!filename) return ""
        var dir = template.assetsRoot || ""
        if (dir.length > 0 && dir.charAt(dir.length - 1) !== "/")
            dir += "/"
        return dir + filename
    }

    // ----- LV digit sprite atlas helpers -----
    // Atlas layout: 4×4 grid of 48×60 cells (192×240 total).
    // Cell index → char: 0-9 digits, 10='+', 11='-', 12=',', 13='·', 14='LV'.
    function lvAtlasUrl() {
        var diff = trackValue("difficulty")
        var atlasName = (template.lvAtlas && template.lvAtlas[diff]) || ""
        if (!atlasName) return ""
        return Qt.resolvedUrl((template.lvAtlasRoot || "") + "/" + atlasName)
    }

    function lvCellRect(index) {
        var cw = (template.lvAtlas && template.lvAtlas.cellW) || 48
        var ch = (template.lvAtlas && template.lvAtlas.cellH) || 60
        var cols = (template.lvAtlas && template.lvAtlas.gridCols) || 4
        var col = index % cols
        var row = Math.floor(index / cols)
        return Qt.rect(col * cw, row * ch, cw, ch)
    }

    // Per-cell glyph bounds [leftCol, glyphWidth] within each 48-wide cell.
    // Originally measured from the atlas alpha channel (see probe_lv_cell.py).
    // Visually tuned per-user feedback (each entry independently nudged):
    //   • 0/3/9 : shave 1 px each side (40 → 38) — drop anti-aliasing tail
    //   • 1     : leftCol 6 (tight, no transparent padding on left),
    //             width 30 (+2 transparent padding on right vs alpha 33)
    //   • 4/5   : shave 2 px each side from raw alpha bound
    //   • 8     : extend left by +1 px (col 4) for breathing room, right at 41
    //   • 14(Lv): shave 1 px from left, extend right by +1 (37 → 38) for the
    //             wider "v" tail
    // Other glyphs (2/6/7/+) use the raw alpha bounds.
    function lvGlyphBounds(index) {
        var b = {
             0: [4, 38],  1: [8, 27],  2: [5, 38],  3: [3, 38],
             4: [3, 38],  5: [4, 37],  6: [3, 40],  7: [5, 38],
             8: [4, 38],  9: [4, 38], 10: [9, 26], 11: [0, 48],
            12: [0, 48], 13: [0, 48], 14: [5, 39]
        }
        return b[index] || [0, 48]
    }

    function lvCellRectTight(index) {
        var cw = (template.lvAtlas && template.lvAtlas.cellW) || 48
        var ch = (template.lvAtlas && template.lvAtlas.cellH) || 60
        var cols = (template.lvAtlas && template.lvAtlas.gridCols) || 4
        var col = index % cols
        var row = Math.floor(index / cols)
        var b = lvGlyphBounds(index)
        return Qt.rect(col * cw + b[0], row * ch, b[1], ch)
    }

    function lvGlyphWidth(index) {
        return lvGlyphBounds(index)[1]
    }

    // Digit + "+" cell indices only. The integrated banner accepts ONLY the
    // characters the prefab atlas can render for a maimai level: [0-9] and '+'.
    // Every other character (including '-', spaces, letters, full-width forms)
    // is dropped so an out-of-charset level string can't request a missing glyph.
    //   "13+" -> [1, 3, 10]
    //   "7+"  -> [7, 10]
    //   "4"   -> [4]
    function lvDigitSequence(lvStr) {
        var s = (lvStr === undefined || lvStr === null ? "" : lvStr.toString())
        var arr = []
        for (var i = 0; i < s.length; i++) {
            var c = s[i]
            if (c >= "0" && c <= "9") arr.push(parseInt(c))
            else if (c === "+") arr.push(10)
        }
        return arr
    }

    Component.onCompleted: {
        if (externalTemplate) {
            template = externalTemplate
        } else {
            loadTemplate()
        }
    }
    onExternalTemplateChanged: {
        if (externalTemplate) {
            template = externalTemplate
        }
    }
    onTemplateSourceChanged: {
        if (!externalTemplate) {
            loadTemplate()
        }
    }

    QtObject {
        id: geom
        readonly property real cardH: root.template.card.heightRatio * root.height
        readonly property real cardScale: cardH / root.template.card.nativeHeight
        readonly property real cardW: root.template.card.nativeWidth * cardScale
        readonly property real cardX: (root.width - cardW) / 2
        readonly property real cardY: root.template.card.topMargin * root.height
    }

    // ----- Backdrop: blurred jacket + dim -----
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        visible: !root.transparentBackground
    }

    Image {
        id: backdropSource
        anchors.fill: parent
        source: root.effectiveJacket
        fillMode: Image.PreserveAspectCrop
        visible: false
        asynchronous: false
    }

    MultiEffect {
        anchors.fill: backdropSource
        source: backdropSource
        blurEnabled: true
        blur: root.template.background.blurAmount
        blurMax: 96
        visible: source !== null && root.effectiveJacket.toString().length > 0 && !root.transparentBackground
    }

    Rectangle {
        anchors.fill: parent
        color: root.template.background.dimColor
        opacity: root.template.background.dimOpacity
        visible: !root.transparentBackground
    }

    // ----- Card stage: width/height in NATIVE 420×636, then Scale to fit. -----
    // Children are positioned in native coords; the Scale transform renders
    // them up to fill `card.heightRatio` of the output. MBase_Tab and its
    // plates sit at NEGATIVE y in card-native (they extend above the card).
    Item {
        id: card
        x: geom.cardX
        y: geom.cardY
        width: root.template.card.nativeWidth
        height: root.template.card.nativeHeight
        clip: false
        transform: [
            // Pop overshoot, centred on the card (native centre) so it scales in place.
            Scale {
                origin.x: root.template.card.nativeWidth / 2
                origin.y: root.template.card.nativeHeight / 2
                xScale: root.cardPopScale()
                yScale: root.cardPopScale()
            },
            // Layout fit: native 420×636 -> heightRatio of the canvas.
            Scale {
                origin.x: 0
                origin.y: 0
                xScale: geom.cardScale
                yScale: geom.cardScale
            }
        ]

        // Z-order matches the Unity prefab: MusicBase's own Image (the frame)
        // renders FIRST, then its children render on top, in declaration order.
        // So MBase_Tab + Plates sit ON TOP of the frame's top edge — that's
        // what blends the Tab into the frame seamlessly at the upper-left.

        // 1) Card frame — drawn first (back), as MusicBase's Image component.
        Image {
            anchors.fill: parent
            opacity: root.stageOpacity("frame")
            source: root.assetUrl(root.template.assets.frame[root.trackValue("difficulty")])
            smooth: true
            mipmap: true
        }

        // Shared, device-pixel-snapped jacket rectangle. BOTH the clipped 曲绘
        // (block 2) and the thin black frame (block 4b) read THIS one rect, so
        // the frame's inset border always lands exactly on the jacket's clip
        // edge — there is no per-edge mismatch. Previously the frame snapped its
        // own rect while the jacket clipped at its raw sub-pixel edge; on the
        // bottom/right the snap could fall just INSIDE the jacket, leaving a ~1
        // device-px sliver of 曲绘 peeking past the frame. The card is scaled by a
        // non-integer factor (geom.cardScale), so each edge is snapped in
        // ABSOLUTE device space (folding in the fractional geom.cardX/Y AND the
        // device-pixel ratio) and mapped back to card-native coords. (During the
        // intro pop the snap is briefly broken — fine for that ~0.3 s transient;
        // the still cover export has pop = identity.)
        QtObject {
            id: jacketBox
            readonly property var jb: root.template.layout.jacketSlot
            readonly property real s: geom.cardScale
            readonly property real dpr: root.renderDpr
            // native coord -> nearest device-pixel-aligned native coord (X / Y).
            function snapX(n) { return (Math.round((geom.cardX + n * s) * dpr) / dpr - geom.cardX) / s }
            function snapY(n) { return (Math.round((geom.cardY + n * s) * dpr) / dpr - geom.cardY) / s }
            readonly property real nx:  snapX(jb.x)
            readonly property real ny:  snapY(jb.y)
            readonly property real nx2: snapX(jb.x + jb.w)
            readonly property real ny2: snapY(jb.y + jb.h)
            readonly property real bw: nx2 - nx
            readonly property real bh: ny2 - ny
        }

        // 2) Jacket inside the slot (on top of frame's black slot placeholder).
        Item {
            id: jacketSlot
            x: jacketBox.nx; y: jacketBox.ny; width: jacketBox.bw; height: jacketBox.bh
            clip: true
            opacity: root.stageOpacity("jacket")

            Image {
                anchors.fill: parent
                source: root.effectiveJacket
                fillMode: Image.PreserveAspectCrop
                visible: root.effectiveJacket.toString().length > 0
                smooth: true
                mipmap: true
            }
            Rectangle {
                anchors.fill: parent
                color: "#1F1F26"
                visible: root.effectiveJacket.toString().length === 0
            }
        }

        // 3) MBase_Tab shoulder — sits ABOVE the frame at the top, overlapping
        //    the frame's top edge a few pixels to blend seamlessly.
        Image {
            property var b: root.template.layout.mbaseTab
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("tab")
            source: root.assetUrl(root.template.assets.tab[root.trackValue("difficulty")])
            smooth: true
            mipmap: true
        }

        // 4) Plate_Deluxe / Plate_Standard — sits on the Tab. Only one shows
        //    based on `mode`. Per prefab: Deluxe on left, Standard on right.
        Image {
            property var b: root.template.layout.plateDeluxe
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("tab")
            source: root.assetUrl(root.template.assets.plateDeluxe)
            visible: root.trackValue("mode") !== "Standard"
            smooth: true
            mipmap: true
        }
        Image {
            property var b: root.template.layout.plateStandard
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("tab")
            source: root.assetUrl(root.template.assets.plateStandard)
            visible: root.trackValue("mode") === "Standard"
            smooth: true
            mipmap: true
        }

        // 4b) Thin black frame around the 曲绘/jacket. Declared AFTER the tab +
        // plates but BEFORE the LV pill, so its top edge isn't occluded by the
        // tab shoulder, while the pill still draws on top of the overlapped
        // bottom-right corner. Reads the SAME device-snapped rect as the jacket
        // slot (jacketBox), so the inset stroke hugs the jacket's clip edge on
        // all four sides with no peeking sliver. Stroke is 2/scale native = ~2
        // device px, drawn without AA so it stays crisp under geom.cardScale.
        Item {
            x: jacketBox.nx; y: jacketBox.ny; width: jacketBox.bw; height: jacketBox.bh
            opacity: root.stageOpacity("jacket")
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                border.color: "#000000"
                border.width: 2 / jacketBox.s
                antialiasing: false
            }
        }

        // 5) LV pill — sits between jacket and difficulty bar, right-aligned.
        Image {
            property var b: root.template.layout.lvPill
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("level")
            source: root.assetUrl(root.template.assets.lvPill[root.trackValue("difficulty")])
            smooth: true
            mipmap: true
        }

        // 4) LV display — both LV label and digit sprites render at the
        //    sprite atlas's native cell size (48×60). Prefab dimensions:
        //      Lv container = 48×60 → LV label sprite at native cell size.
        //      LvNum bbox = 94×55 (single) / 141×55 (double) → these are
        //        the LAYOUT CONTAINERS, not sprite render sizes. The custom
        //        MonoBehaviour script (4d46ba93...) draws the digit sprite
        //        at its native 48×60 cell size within those containers.
        //    So both LV and digits render at 60-tall (matching the atlas).
        //    Center-aligned, group right-aligned in the pill, tight spacing.
        Item {
            id: lvDisplay
            property var b: root.template.layout.lvPill
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("level")

            // LV group — proportional glyph widths + tight spacing,
            // CENTERED horizontally in the visible pill.
            //
            // sourceClipRect on each Image is tightened to that glyph's actual
            // alpha bounds (see lvGlyphBounds), so digits render proportionally:
            // "1" = 28 wide, "4" = 42, "+" = 29, "Lv" = 39 (native px).
            //
            // Spacings: outer -1 px ("Lv" ↔ digits), inner -2 px (digit-to-
            // digit). Negative values close the small soft-alpha tail gap so
            // glyphs visually butt up against each other (alpha threshold for
            // the bounds was >20, so columns 1-2 px beyond have anti-aliasing
            // tails that overlap harmlessly). "Lv 13+" total = 132 native.
            //
            // anchors.horizontalCenterOffset = 22 places the centerline at
            // pill-internal native x=112, the midpoint between the two
            // boundary lines the user marked on the ReMASTER output
            // (left native=42.9, right native=180.4). With ReMASTER width
            // 131, the group sits with ~3 px margin on each side.
            Row {
                id: lvGroup
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.horizontalCenterOffset: 22
                anchors.verticalCenter: parent.verticalCenter
                anchors.verticalCenterOffset: 21
                height: 60
                spacing: -1   // outer: "Lv" → digits (close soft-alpha gap)

                Image {
                    id: lvLabelSprite
                    height: 60
                    width: root.lvGlyphWidth(14)
                    source: root.lvAtlasUrl()
                    sourceClipRect: root.lvCellRectTight(14)
                    smooth: true
                    mipmap: true
                }
                Row {
                    id: lvDigitsRow
                    height: 60
                    spacing: -2   // inner: digit-to-digit (tight overlap of soft-alpha tails)

                    Repeater {
                        model: root.lvDigitSequence(root.trackValue("level"))
                        Image {
                            height: 60
                            width: root.lvGlyphWidth(modelData)
                            source: root.lvAtlasUrl()
                            sourceClipRect: root.lvCellRectTight(modelData)
                            smooth: true
                            mipmap: true
                        }
                    }
                }
            }
        }

        // 5) Title — prefab uses SEGA-NewRodinN v2 EB (display) → M PLUS 1p Black.
        //    HorizontalFit shrinks an over-long title toward minimumPixelSize,
        //    then ElideRight clips; NoWrap + single line + oneLine() guard it.
        Text {
            property var b: root.template.layout.titleArea
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("title")
            text: root.oneLine(root.trackValue("title"))
            color: root.template.colors.titleOnDark
            font.family: displayFont.name
            font.pixelSize: Math.round(b.h * 0.82)
            minimumPixelSize: Math.round(b.h * 0.42)
            fontSizeMode: Text.HorizontalFit
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // 6) Artist — prefab uses SEGA_MaruGothic-DB (body) → M PLUS Rounded 1c Bold.
        Text {
            property var b: root.template.layout.artistArea
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("artist")
            text: root.oneLine(root.trackValue("artist"))
            color: root.template.colors.artistOnDark
            font.family: bodyFont.name
            font.pixelSize: Math.round(b.h * 0.82)
            minimumPixelSize: Math.round(b.h * 0.42)
            fontSizeMode: Text.HorizontalFit
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // 6a-d) Achievement-row placeholders — shown when chart hasn't been
        //       played. Per prefab: UI_Achivement (rounded rect, left half),
        //       UI_Rank (rounded rect, right), IMG_APFCMedal + IMG_SyncMedal
        //       (circles on the right edge).
        Rectangle {
            property var b: root.template.layout.uiAchievement
            x: b.x; y: b.y; width: b.w; height: b.h
            radius: 4
            opacity: root.stageOpacity("bars")
            color: root.template.colors.placeholderFill
            antialiasing: true
        }
        Rectangle {
            property var b: root.template.layout.uiRank
            x: b.x; y: b.y; width: b.w; height: b.h
            radius: 4
            opacity: root.stageOpacity("bars")
            color: root.template.colors.placeholderFill
            antialiasing: true
        }
        // Medal slot is 80x80 per prefab but actual medal sprite is much smaller.
        // Render placeholder circle at ~36x36 centered in the slot.
        Rectangle {
            property var b: root.template.layout.imgApFcMedal
            readonly property real innerSize: 52
            x: b.x + (b.w - innerSize) / 2
            y: b.y + (b.h - innerSize) / 2
            width: innerSize
            height: innerSize
            radius: width / 2
            opacity: root.stageOpacity("info")
            color: root.template.colors.placeholderFill
            antialiasing: true
        }
        Rectangle {
            property var b: root.template.layout.imgSyncMedal
            readonly property real innerSize: 52
            x: b.x + (b.w - innerSize) / 2
            y: b.y + (b.h - innerSize) / 2
            width: innerSize
            height: innerSize
            radius: width / 2
            opacity: root.stageOpacity("info")
            color: root.template.colors.placeholderFill
            antialiasing: true
        }

        // 7) NOTES DESIGNER label — prefab Text_Title uses NewRodin EB → display font.
        Text {
            property var b: root.template.layout.designerLabel
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("info")
            text: "NOTES DESIGNER"
            color: root.template.colors.labelOnWhite
            font.family: displayFont.name
            font.pixelSize: Math.round(b.h * 0.55)
            font.letterSpacing: 0.5
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
        }

        // 8) Designer name — prefab TMP_NoteName uses MaruGothic DB → body font.
        Text {
            property var b: root.template.layout.designerName
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("info")
            text: {
                var d = root.oneLine(root.trackValue("designer"))
                return d.length > 0 ? d : "—"
            }
            color: root.template.colors.designerOnWhite
            font.family: bodyFont.name
            font.pixelSize: Math.round(b.h * 0.96)
            minimumPixelSize: Math.round(b.h * 0.5)
            fontSizeMode: Text.HorizontalFit
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }

        // 9) BPM — prefab TMP_BPM uses MaruGothic DB → body font.
        //     m_text in the actual prefab is "BPM:999"; we render with a space
        //     instead of a colon to match the reference image.
        Text {
            property var b: root.template.layout.bpmText
            x: b.x; y: b.y; width: b.w; height: b.h
            opacity: root.stageOpacity("info")
            text: "BPM " + root.oneLine(root.trackValue("bpm"))
            color: root.template.colors.bpmOnWhite
            font.family: bodyFont.name
            font.pixelSize: Math.round(b.h * 0.92)
            minimumPixelSize: Math.round(b.h * 0.5)
            fontSizeMode: Text.HorizontalFit
            wrapMode: Text.NoWrap
            maximumLineCount: 1
            horizontalAlignment: Text.AlignLeft
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
}
