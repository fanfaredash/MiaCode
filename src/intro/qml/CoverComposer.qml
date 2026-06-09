// Cover composer — a live WYSIWYG scene for the difficulty-card cover export.
//
// One QSG scene that is BOTH the editor preview and the export source: it draws
// a full-bleed background layer (chart 曲绘 / custom image / transparent, blurred
// or crisp + dim) and a set of free-floating, draggable LAYERS on top. v1 ships
// one draggable layer — the difficulty card (MaimaiBannerCard in transparent
// mode) — but the Repeater + CoverLayoutModel design carries z-order and extra
// `kind`s so the chart-frame / badge layers slot in later.
//
// Geometry is NORMALISED (CoverLayer.nx/ny centre, sizeFraction = content
// height / canvas height) so the small embedded preview and the full-resolution
// export render identically. C++ feeds inputs as root properties and the layout
// model as `coverLayout`; dragging/scaling writes straight back to the model
// (no readback) and the export path grabs this same scene at full size.
//
// ⚠ Headless/export note: this scene is static (frame 0). Drag/scale/snap chrome
// is gated behind `editable` (false for export). No QML NumberAnimation is used.

import QtQuick
import QtQuick.Effects

Item {
    id: canvas

    // ---- Inputs (set from C++; see CoverComposerInputs) ----
    property var coverLayout: null          // CoverLayoutModel*
    property var coverTemplate: ({})        // parsed maimai_banner.json
    property var trackOverrides: ({})       // title/artist/.../level/difficulty/bpm/mode/lvRenderMode/stillTextMode
    property url jacketImage: ""            // chart 曲绘 (jacket slot + default backdrop)
    property url backgroundImage: ""        // custom backdrop (Custom mode)
    property int backgroundMode: 0          // 0=jacket, 1=custom, 2=transparent
    property bool blurEnabled: true
    property bool cardShadowEnabled: false
    property bool editable: true            // false in the export render (no chrome/handlers)

    // ---- Editor state ----
    property int selectedIndex: -1
    property real guideX: -1                // active vertical snap guide (canvas px); <0 = none
    property real guideY: -1                // active horizontal snap guide

    readonly property bool transparentBg: backgroundMode === 2
    // Design reference height the fixed-pixel blur radii were tuned at. Blur radii
    // are scaled by canvas.height/this so the small live preview and the full-res
    // export render the SAME relative blur (preview == export).
    readonly property real blurReferenceHeight: 1080

    clip: true

    // ===================== Derived card geometry =====================
    // The card auto-centres + scales within its own bounds via heightRatio /
    // topMargin. For a free layer we want the card CONTENT (tab shoulder → card
    // bottom) to fill its wrapper exactly, so we clone the template and solve
    // heightRatio/topMargin for "content fills the box", and size the wrapper to
    // the matching aspect (cardAspect). Then dragging/snapping the wrapper == the
    // visible card.
    function tabTopNative(t) {
        if (t && t.layout && t.layout.mbaseTab && t.layout.mbaseTab.y !== undefined)
            return t.layout.mbaseTab.y
        return -54
    }
    function cardNativeW(t) { return (t && t.card && t.card.nativeWidth) ? t.card.nativeWidth : 420 }
    function cardNativeH(t) { return (t && t.card && t.card.nativeHeight) ? t.card.nativeHeight : 636 }
    function cardContentNativeH(t) { return cardNativeH(t) - tabTopNative(t) }

    readonly property real cardAspect: {
        var t = coverTemplate
        var denom = cardContentNativeH(t)
        return denom > 0 ? cardNativeW(t) / denom : (420.0 / 690.0)
    }

    readonly property var cardTemplate: {
        var t = coverTemplate
        if (!t || !t.card) return t
        var c = JSON.parse(JSON.stringify(t))
        var tabTop = tabTopNative(c)
        var contentH = c.card.nativeHeight - tabTop
        if (contentH > 0) {
            c.card.heightRatio = c.card.nativeHeight / contentH
            c.card.topMargin = (-tabTop) / contentH
        }
        c.transparentBackground = true   // card layer is always transparent; bg is its own layer
        return c
    }

    // ---- background helpers (read from template, defaults match the card) ----
    function dimColor() {
        return (coverTemplate && coverTemplate.background && coverTemplate.background.dimColor)
                ? coverTemplate.background.dimColor : "#0A0414"
    }
    function dimOpacity() {
        return (coverTemplate && coverTemplate.background && coverTemplate.background.dimOpacity !== undefined)
                ? coverTemplate.background.dimOpacity : 0.55
    }
    function blurAmount() {
        return (coverTemplate && coverTemplate.background && coverTemplate.background.blurAmount !== undefined)
                ? coverTemplate.background.blurAmount : 0.9
    }
    // ---- card drop-shadow helpers (template.cardShadow) ----
    function cardShadowColor() {
        return (coverTemplate && coverTemplate.cardShadow && coverTemplate.cardShadow.color)
                ? coverTemplate.cardShadow.color : "#99000000"
    }
    function cardShadowBlur() {
        return (coverTemplate && coverTemplate.cardShadow && coverTemplate.cardShadow.blur !== undefined)
                ? coverTemplate.cardShadow.blur : 0.6
    }
    function cardShadowOffsetY() {
        return (coverTemplate && coverTemplate.cardShadow && coverTemplate.cardShadow.offsetY !== undefined)
                ? coverTemplate.cardShadow.offsetY : 14
    }

    readonly property url backdropSourceUrl:
        backgroundMode === 1 ? backgroundImage
                             : (backgroundMode === 0 ? jacketImage : "")

    // ---- per-layer pixel geometry helpers (used by selection chrome) ----
    function layerContentH(l) { return (l ? l.sizeFraction : 0.85) * canvas.height }
    function layerContentW(l) {
        return layerContentH(l) * ((l && l.kind === "card") ? cardAspect : 1.0)
    }
    readonly property var selectedLayer:
        (coverLayout && selectedIndex >= 0 && selectedIndex < coverLayout.layers.length)
            ? coverLayout.layers[selectedIndex] : null

    function clearGuides() { guideX = -1; guideY = -1 }

    // Clamp a proposed CENTRE so at least 25% of a `size`-wide layer stays inside
    // [0, span] — a layer can never be dragged fully off the clipped canvas and
    // stranded un-grabbable.
    function clampCentre(c, size, span) {
        var keep = 0.25
        return Math.max(size * (keep - 0.5), Math.min(span + size * (0.5 - keep), c))
    }

    // Snap a proposed CENTRE (cx,cy) of a w×h layer to the canvas centre / edges.
    // Returns the (possibly snapped) centre and records the active guide lines.
    function applySnap(cx, cy, w, h) {
        var W = canvas.width, H = canvas.height
        var th = Math.max(6, W * 0.012)
        var gx = -1, gy = -1
        if (Math.abs(cx - W / 2) < th)            { cx = W / 2;     gx = W / 2 }
        else if (Math.abs(cx - w / 2) < th)       { cx = w / 2;     gx = 0 }
        else if (Math.abs((cx + w / 2) - W) < th) { cx = W - w / 2; gx = W }
        if (Math.abs(cy - H / 2) < th)            { cy = H / 2;     gy = H / 2 }
        else if (Math.abs(cy - h / 2) < th)       { cy = h / 2;     gy = 0 }
        else if (Math.abs((cy + h / 2) - H) < th) { cy = H - h / 2; gy = H }
        canvas.guideX = gx
        canvas.guideY = gy
        return Qt.point(cx, cy)
    }

    // Initial selection must fire when C++ ASSIGNS coverLayout — the host create()s
    // the scene (running Component.onCompleted) and only THEN sets coverLayout, so
    // onCompleted alone never sees the model. The `editable` guard keeps the export
    // path (editable=false) from ever auto-selecting / baking chrome into the image.
    onCoverLayoutChanged: {
        if (editable && coverLayout && coverLayout.layers.length > 0 && selectedIndex < 0)
            selectedIndex = 0
    }
    Component.onCompleted: {
        if (editable && coverLayout && coverLayout.layers.length > 0 && selectedIndex < 0)
            selectedIndex = 0
    }

    // ===================== Background fill layer =====================
    Rectangle {
        anchors.fill: parent
        color: "#000000"
        visible: !canvas.transparentBg
    }
    // Crisp backdrop (shown directly when blur is off); when blur is on it stays
    // hidden but still feeds the MultiEffect below as the blur source.
    Image {
        id: bgSrc
        anchors.fill: parent
        source: canvas.backdropSourceUrl
        fillMode: Image.PreserveAspectCrop
        // Gate on a successful load so a bad/missing source cleanly falls through
        // to the black+dim backdrop instead of showing a half/errored image.
        visible: !canvas.transparentBg && !canvas.blurEnabled
                 && source.toString().length > 0 && status === Image.Ready
        asynchronous: false
        smooth: true
        mipmap: true
    }
    MultiEffect {
        anchors.fill: bgSrc
        source: bgSrc
        blurEnabled: true
        blur: canvas.blurAmount()
        // The blur RADIUS is fixed-pixel in Qt MultiEffect, so scale blurMax by
        // canvas height vs the 1080 design reference — otherwise the small live
        // preview blurs ~2.7x stronger (relative) than the full-res export and
        // preview != export. (Mirrors the card-shadow offset/blur scaling below.)
        blurMax: Math.max(2, Math.round(96 * canvas.height / canvas.blurReferenceHeight))
        visible: !canvas.transparentBg && canvas.blurEnabled
                 && bgSrc.source.toString().length > 0 && bgSrc.status === Image.Ready
    }
    Rectangle {
        anchors.fill: parent
        color: canvas.dimColor()
        opacity: canvas.dimOpacity()
        visible: !canvas.transparentBg
    }

    // ===================== Draggable layers =====================
    Repeater {
        id: layerRepeater
        model: canvas.coverLayout ? canvas.coverLayout.layers : 0
        delegate: Item {
            id: layerItem
            required property var modelData
            required property int index
            readonly property var ld: modelData       // CoverLayer
            readonly property bool isCard: ld && ld.kind === "card"

            width: canvas.layerContentW(ld)
            height: canvas.layerContentH(ld)
            x: (ld ? ld.nx : 0.5) * canvas.width - width / 2
            y: (ld ? ld.ny : 0.5) * canvas.height - height / 2
            z: ld ? ld.z : 0
            visible: ld ? ld.visible : true

            // Card content, optionally drop-shadowed via a layer effect on the
            // content ONLY (so the selection chrome is never rasterised with it).
            // The shadow applies in EVERY background mode — over a backdrop, and
            // in Transparent mode where it casts a soft shadow onto the alpha PNG.
            Item {
                id: content
                anchors.fill: parent
                layer.enabled: layerItem.isCard && canvas.cardShadowEnabled
                layer.effect: MultiEffect {
                    shadowEnabled: true
                    shadowColor: canvas.cardShadowColor()
                    // Scale the shadow blur radius by the card's px-per-native scale
                    // (the SAME factor as the offset below) so softness, offset and
                    // geometry all track output resolution → preview == export.
                    blurMax: Math.max(2, Math.round(64 * layerItem.height
                                      / canvas.cardContentNativeH(canvas.coverTemplate)))
                    shadowBlur: canvas.cardShadowBlur()
                    // offsetY is card-native px; scale into wrapper px.
                    shadowVerticalOffset: canvas.cardShadowOffsetY()
                                          * (layerItem.height / canvas.cardContentNativeH(canvas.coverTemplate))
                    shadowHorizontalOffset: 0
                    autoPaddingEnabled: true
                }
                Loader {
                    anchors.fill: parent
                    active: layerItem.isCard && canvas.coverTemplate && canvas.coverTemplate.card
                    sourceComponent: cardComponent
                }
            }

            TapHandler {
                enabled: canvas.editable
                onTapped: canvas.selectedIndex = layerItem.index
            }

            // Drag to move, with canvas centre/edge snapping. target:null — we
            // write normalized coords back to the model from the centroid delta,
            // so the model stays the single source of truth (no binding fight).
            DragHandler {
                id: moveDrag
                target: null
                enabled: canvas.editable && layerItem.ld && !layerItem.ld.locked
                property real startNx: 0.5
                property real startNy: 0.5
                onActiveChanged: {
                    if (active) {
                        canvas.selectedIndex = layerItem.index
                        startNx = layerItem.ld.nx
                        startNy = layerItem.ld.ny
                    } else {
                        canvas.clearGuides()
                    }
                }
                onCentroidChanged: {
                    if (!active || !layerItem.ld) return
                    var dx = centroid.scenePosition.x - centroid.scenePressPosition.x
                    var dy = centroid.scenePosition.y - centroid.scenePressPosition.y
                    var cx = startNx * canvas.width + dx
                    var cy = startNy * canvas.height + dy
                    var snapped = canvas.applySnap(cx, cy, layerItem.width, layerItem.height)
                    // Keep ≥25% of the card on the clipped canvas so it can't be stranded.
                    var sx = canvas.clampCentre(snapped.x, layerItem.width, canvas.width)
                    var sy = canvas.clampCentre(snapped.y, layerItem.height, canvas.height)
                    layerItem.ld.nx = sx / canvas.width
                    layerItem.ld.ny = sy / canvas.height
                }
            }
        }
    }

    Component {
        id: cardComponent
        MaimaiBannerCard {
            anchors.fill: parent
            externalTemplate: canvas.cardTemplate
            trackOverrides: canvas.trackOverrides
            jacketImage: canvas.jacketImage
            revealStartFrame: -1
            frame: 0
        }
    }

    // ===================== Selection chrome (editor only) =====================
    // Drawn at canvas level so it always sits on top and the scale handle is a
    // direct child of the canvas (guaranteed input delivery, no parent-bounds clip).
    Rectangle {
        id: selectionBorder
        readonly property var l: canvas.selectedLayer
        visible: canvas.editable && l !== null
        x: l ? (l.nx * canvas.width - canvas.layerContentW(l) / 2) : 0
        y: l ? (l.ny * canvas.height - canvas.layerContentH(l) / 2) : 0
        width: l ? canvas.layerContentW(l) : 0
        height: l ? canvas.layerContentH(l) : 0
        color: "transparent"
        border.color: "#3DA9FC"
        border.width: 2
        antialiasing: true
        z: 9000
    }
    Rectangle {
        id: scaleHandle
        readonly property var l: canvas.selectedLayer
        visible: canvas.editable && l !== null && !l.locked
        width: 18
        height: 18
        radius: 4
        color: "#3DA9FC"
        border.color: "#FFFFFF"
        border.width: 2
        z: 9001
        x: l ? (l.nx * canvas.width + canvas.layerContentW(l) / 2 - width / 2) : -100
        y: l ? (l.ny * canvas.height + canvas.layerContentH(l) / 2 - height / 2) : -100
        DragHandler {
            target: null
            enabled: scaleHandle.visible
            onActiveChanged: if (!active) canvas.clearGuides()
            onCentroidChanged: {
                if (!active || !scaleHandle.l) return
                var cy = scaleHandle.l.ny * canvas.height
                var halfH = Math.abs(centroid.scenePosition.y - cy)
                var newH = Math.max(canvas.height * 0.05, 2 * halfH)
                scaleHandle.l.sizeFraction = newH / canvas.height
            }
        }
    }

    // ===================== Snap guide lines =====================
    Rectangle {
        visible: canvas.editable && canvas.guideX >= 0
        x: canvas.guideX - width / 2
        y: 0
        width: 1
        height: canvas.height
        color: "#FF4081"
        z: 9500
    }
    Rectangle {
        visible: canvas.editable && canvas.guideY >= 0
        x: 0
        y: canvas.guideY - height / 2
        width: canvas.width
        height: 1
        color: "#FF4081"
        z: 9500
    }
}
