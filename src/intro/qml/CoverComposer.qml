// Cover composer — a live WYSIWYG scene for the difficulty-card cover export.
//
// One QSG scene that is BOTH the editor preview and the export source: it draws
// a full-bleed background layer (chart 曲绘 / custom image / transparent, blurred
// or crisp + dim) and a set of free-floating, draggable LAYERS on top. Two layer
// kinds ship: the difficulty card (MaimaiBannerCard in transparent mode) and an
// optional chart-frame still (a square playfield grab served by the "coverchart"
// image provider, rendered in-process by SceneFrameRenderer). The Repeater +
// CoverLayoutModel design carries z-order and extra `kind`s so badge layers slot
// in later.
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
import MiaCode.Preview

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
    // Full-bleed backdrop brightness (0..1). The dim overlay opacity = 1 − this,
    // so 0.45 reproduces the old fixed dimOpacity 0.55. (C++ CoverComposerInputs.)
    property real coverBgBrightness: 0.45
    property bool cardShadowEnabled: false
    // B1 — chart-frame inner-ring background. When enabled, the chart-frame disk
    // shows the cover background (曲绘/custom) crisp, circular-masked to the playfield
    // ring, dimmed by chartFrameBgBrightness; the outer ring stays transparent.
    property bool chartFrameBgEnabled: false
    property real chartFrameBgTransparency: 0.5
    property real chartFrameBgBrightness: 0.8   // 0..1; MultiEffect.brightness = this − 1
    property real chartFrameDiskDiameter: 0.0   // ring diameter / square side (0 = no disk)
    property string activeChartFrameKey: ""      // only this chart frame hosts the live scene
    // §4 — which layer wears the selection chrome (any kind, incl. the card). Driven
    // two-way: C++ pushes it (list / inspector selection → blue box moves), and a
    // canvas tap / drag pushes it back via selectionBinder.selectLayerKey. The
    // editor's selectedIndex is DERIVED from this (key-based → reorder/add/remove safe).
    property string selectedKey: ""
    property bool editable: true            // false in the export render (no chrome/handlers)
    // The v2 page provides the QML-facing cover session as the binder facade for
    // the one active live chart scene; export rendering leaves it null and uses
    // the cached still image.
    property var selectionBinder: null
    // The page uses this callback to route canvas selection to its inspector tab.
    // Selection itself remains owned by selectionBinder.
    property var layerSelectionCallback: null
    // A2 — a live-only consumer can set this so the chart-frame layer hosts a
    // PreviewQuickSceneRoot instead of the static grab Image:
    // scrubbing/playback then only moves the shared playhead (zero readback). Stays
    // null in the export render, where the static grab Image is used instead.
    property var chartSceneBinder: null
    readonly property bool liveChartSceneBound:
        chartSceneBinder !== null && chartSceneBinder.liveChartSceneBound === true

    // ---- Editor state ----
    // Derived from selectedKey so reordering / adding / removing layers never points
    // the chrome at the wrong delegate. C++ owns the selection (selectedKey).
    readonly property int selectedIndex: {
        if (!coverLayout || selectedKey === "")
            return -1
        var layers = coverLayout.layers
        for (var i = 0; i < layers.length; i++)
            if (layers[i] && layers[i].key === selectedKey)
                return i
        return -1
    }
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
        if (!l)
            return layerContentH(l)
        if (l.kind === "card")
            return layerContentH(l) * cardAspect
        // Image / text layers keep their true proportions: box width = height ×
        // intrinsic (image) / laid-out (text) aspect. Both store it in contentAspect.
        if (l.kind === "image" || l.kind === "text")
            return layerContentH(l) * (l.contentAspect > 0 ? l.contentAspect : 1.0)
        return layerContentH(l)
    }

    // Absolute filesystem path → file URL (custom image / text-layer font). Empty
    // stays empty; an already-schemed value is used verbatim.
    function localFileUrl(p) {
        if (!p) return ""
        var s = p.toString()
        if (s.length === 0) return ""
        if (s.indexOf("://") >= 0) return s
        if (s.charAt(0) === "/") return "file://" + encodeURI(s)
        return "file:///" + encodeURI(s)   // Windows drive path (C:/…)
    }

    function selectLayerKey(key) {
        if (!key)
            return
        if (canvas.selectionBinder)
            canvas.selectionBinder.selectLayerKey(key)
        else if (canvas.chartSceneBinder)
            canvas.chartSceneBinder.selectLayerKey(key)
        if (canvas.layerSelectionCallback)
            canvas.layerSelectionCallback.call(canvas, key)
    }
    // Text-layer font: the layer's custom fontPath (absolute) if set, else the
    // bundled Heavy display font.
    function fontSourceUrlForLayer(ld) {
        var p = ld ? ld.fontPath : ""
        if (p && p.toString().length > 0)
            return canvas.localFileUrl(p)
        return "qrc:/intro/assets/fonts/ResourceHanRoundedCN-Heavy.ttf"
    }
    // Write the text's true aspect (from an unconstrained probe) back onto the
    // layer so its box hugs the glyphs. The probe's pixelSize is fixed, so this
    // never feeds back into the probe → no binding loop.
    function writeTextAspect(ld, probe) {
        if (!ld || !probe) return
        var w = probe.contentWidth
        var h = probe.contentHeight
        if (w > 0.5 && h > 0.5)
            ld.contentAspect = w / h
    }
    function pointInLayer(l, px, py) {
        if (!l || !l.visible)
            return false
        var w = layerContentW(l)
        var h = layerContentH(l)
        var left = l.nx * canvas.width - w / 2
        var top = l.ny * canvas.height - h / 2
        return px >= left && px <= left + w && py >= top && py <= top + h
    }
    function scaleHandleSize() { return Math.max(44, canvas.height * 0.04) }
    function pointInSelectionScaleHandle(px, py) {
        var l = selectedLayer
        if (!editable || !l || !l.visible || l.locked)
            return false
        var s = scaleHandleSize()
        var left = l.nx * canvas.width + layerContentW(l) / 2 - s / 2
        var top = l.ny * canvas.height + layerContentH(l) / 2 - s / 2
        return px >= left && px <= left + s && py >= top && py <= top + s
    }
    function topHitLayerAt(px, py) {
        if (!coverLayout)
            return null
        if (pointInSelectionScaleHandle(px, py))
            return selectedLayer
        var layers = coverLayout.layers
        var best = null
        for (var i = 0; i < layers.length; ++i) {
            var l = layers[i]
            if (!pointInLayer(l, px, py))
                continue
            if (best === null || l.z > best.z)
                best = l
        }
        return best
    }
    function dragLayerAt(px, py) {
        if (pointInSelectionScaleHandle(px, py))
            return null
        var layer = topHitLayerAt(px, py)
        // A locked top layer still owns the hit. It may be selected, but a
        // drag must not tunnel through it to a visually lower layer.
        return layer && !layer.locked ? layer : null
    }
    function hitKeyAt(px, py) {
        var l = topHitLayerAt(px, py)
        return l ? l.key : ""
    }
    readonly property var selectedLayer:
        (coverLayout && selectedIndex >= 0 && selectedIndex < coverLayout.layers.length)
            ? coverLayout.layers[selectedIndex] : null

    function clearGuides() { guideX = -1; guideY = -1 }

    function commitGeometry() {
        if (canvas.selectionBinder && canvas.selectionBinder.commitActiveLayerGeometry)
            canvas.selectionBinder.commitActiveLayerGeometry()
    }

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

    // Initial selection is owned by C++: the panel sets its active layer (the card)
    // and pushes selectedKey on load, so no QML-side auto-select is needed. The
    // export path (editable=false) never receives a selectedKey → no chrome baked in.

    TapHandler {
        enabled: canvas.editable
        gesturePolicy: TapHandler.WithinBounds
        onTapped: {
            canvas.selectLayerKey(canvas.hitKeyAt(point.position.x, point.position.y))
        }
    }

    // Canvas-level move drag. This must live above the layer delegates because a
    // lower selected layer's own handler does not receive pointer events through an
    // overlapping upper layer. Taps still use visual z-order; drags use dragLayerAt().
    DragHandler {
        id: moveDrag
        target: null
        enabled: canvas.editable
        grabPermissions: PointerHandler.CanTakeOverFromAnything
                         | PointerHandler.ApprovesTakeOverByAnything
                         | PointerHandler.ApprovesCancellation
        property var dragLayer: null
        property real startNx: 0.5
        property real startNy: 0.5
        // Cursor position (scene px) AT ACTIVATION — the drag reference.
        property real grabSceneX: 0
        property real grabSceneY: 0
        onActiveChanged: {
            if (active) {
                dragLayer = canvas.dragLayerAt(
                        centroid.position.x,
                        centroid.position.y)
                if (!dragLayer) {
                    dragLayer = null
                    return
                }
                canvas.selectLayerKey(dragLayer.key)
                startNx = dragLayer.nx
                startNy = dragLayer.ny
                // Reference the delta from the centroid AT ACTIVATION, not the
                // press: a DragHandler only activates AFTER the cursor passes the
                // drag threshold, so press-referenced movement would jump.
                grabSceneX = centroid.position.x
                grabSceneY = centroid.position.y
            } else {
                var hadDragLayer = dragLayer !== null
                dragLayer = null
                canvas.clearGuides()
                if (hadDragLayer)
                    canvas.commitGeometry()
            }
        }
        onCentroidChanged: {
            if (!active || !dragLayer) return
            var dx = centroid.position.x - grabSceneX
            var dy = centroid.position.y - grabSceneY
            var w = canvas.layerContentW(dragLayer)
            var h = canvas.layerContentH(dragLayer)
            var cx = startNx * canvas.width + dx
            var cy = startNy * canvas.height + dy
            var snapped = canvas.applySnap(cx, cy, w, h)
            // Keep >=25% of the layer on the clipped canvas so it can't be stranded.
            var sx = canvas.clampCentre(snapped.x, w, canvas.width)
            var sy = canvas.clampCentre(snapped.y, h, canvas.height)
            dragLayer.nx = sx / canvas.width
            dragLayer.ny = sy / canvas.height
        }
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
        // Cap the decoded texture to the canvas size — at export the canvas IS the
        // output resolution (full quality), at preview it's small (saves VRAM); a
        // huge custom backdrop no longer decodes at its native megapixels.
        sourceSize: canvas.width > 1 && canvas.height > 1
                    ? Qt.size(Math.ceil(canvas.width), Math.ceil(canvas.height))
                    : undefined
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
        // User-adjustable backdrop dim (was the fixed template dimOpacity). The tint
        // colour stays from the template; brightness only drives how much shows.
        opacity: Math.max(0, Math.min(1, 1 - canvas.coverBgBrightness))
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
            readonly property bool isChartFrame: ld && ld.kind === "chartFrame"
            readonly property bool isImage: ld && ld.kind === "image"
            readonly property bool isText: ld && ld.kind === "text"
            readonly property bool isActiveChartFrame:
                isChartFrame && layerItem.ld && layerItem.ld.key === canvas.activeChartFrameKey
            readonly property bool frameBgEnabled:
                isChartFrame && layerItem.ld && layerItem.ld.frameBgEnabled !== undefined
                    ? layerItem.ld.frameBgEnabled : canvas.chartFrameBgEnabled
            readonly property string frameBgMode:
                isChartFrame && layerItem.ld && layerItem.ld.frameBgMode !== undefined
                    ? layerItem.ld.frameBgMode : (layerItem.frameBgEnabled ? "image" : "transparent")
            readonly property real frameBgBrightness:
                isChartFrame && layerItem.ld && layerItem.ld.frameBgBrightness !== undefined
                    ? layerItem.ld.frameBgBrightness : canvas.chartFrameBgBrightness
            readonly property real frameBgTransparency:
                isChartFrame && layerItem.ld && layerItem.ld.frameBgTransparency !== undefined
                    ? layerItem.ld.frameBgTransparency : canvas.chartFrameBgTransparency
            // B1 disk background: shared gate for its image + dim overlay.
            readonly property bool showsImageDiskBg:
                isChartFrame && layerItem.frameBgMode === "image"
                && canvas.chartFrameDiskDiameter > 0
                && canvas.backdropSourceUrl.toString().length > 0
            readonly property bool showsTransparentDiskBg:
                isChartFrame && layerItem.frameBgMode === "transparent"
                && canvas.chartFrameDiskDiameter > 0
                && layerItem.frameBgTransparency < 1.0

            width: canvas.layerContentW(ld)
            height: canvas.layerContentH(ld)
            x: (ld ? ld.nx : 0.5) * canvas.width - width / 2
            y: (ld ? ld.ny : 0.5) * canvas.height - height / 2
            z: ld ? ld.z : 0
            visible: ld ? ld.visible : true
            opacity: layerItem.isChartFrame ? 1.0 : (ld && ld.opacity !== undefined ? ld.opacity : 1.0)

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
                // B1 — chart-frame inner-ring background, BEHIND the overlay. The
                // crisp cover background, circular-masked to the playfield disk and
                // dimmed (MultiEffect.brightness); only the BG is masked, so the
                // overlay (notes/ring/effects) still extends across the square as in
                // A2. Hidden for the card layer / transparent bg. The source Image is
                // a native texture provider while hidden; the circular mask is
                // captured via ShaderEffectSource (the repo's proven hideSource
                // pattern, cf. IntroOverlay.qml). Declared first → paints behind.
                Image {
                    id: chartBgDiskImage
                    anchors.fill: parent
                    visible: false
                    source: layerItem.isChartFrame ? canvas.backdropSourceUrl : ""
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: false
                    smooth: true
                    mipmap: true
                }
                Item {
                    id: chartBgDiskMaskContent
                    anchors.fill: parent
                    Rectangle {
                        anchors.centerIn: parent
                        width: canvas.chartFrameDiskDiameter * parent.width
                        height: width
                        radius: width / 2
                        antialiasing: true
                        color: "#FFFFFF"
                    }
                }
                ShaderEffectSource {
                    id: chartBgDiskMaskTex
                    anchors.fill: parent
                    sourceItem: chartBgDiskMaskContent
                    hideSource: true
                    live: layerItem.isChartFrame
                    visible: false
                }
                MultiEffect {
                    anchors.fill: parent
                    visible: layerItem.showsImageDiskBg
                    source: chartBgDiskImage
                    maskEnabled: true
                    maskSource: chartBgDiskMaskTex
                    maskThresholdMin: 0.5
                }
                // Brightness = the SAME multiplicative dim the realtime preview's
                // 内圈亮度 uses (stage-background draws black at alpha 1−brightness
                // → media × brightness). MultiEffect.brightness is ADDITIVE
                // (crushes dark pixels straight to black) so it is NOT used here.
                Rectangle {
                    visible: layerItem.showsImageDiskBg
                    anchors.centerIn: parent
                    width: canvas.chartFrameDiskDiameter * parent.width
                    height: width
                    radius: width / 2
                    antialiasing: true
                    color: "#000000"
                    opacity: Math.max(0, Math.min(1, 1.0 - layerItem.frameBgBrightness))
                }
                Rectangle {
                    visible: layerItem.showsTransparentDiskBg
                    anchors.centerIn: parent
                    width: canvas.chartFrameDiskDiameter * parent.width
                    height: width
                    radius: width / 2
                    antialiasing: true
                    color: "#000000"
                    opacity: Math.max(0, Math.min(1, 1.0 - layerItem.frameBgTransparency))
                }
                Loader {
                    anchors.fill: parent
                    active: layerItem.isCard && canvas.coverTemplate && canvas.coverTemplate.card
                    sourceComponent: cardComponent
                }
                // Chart-frame layer. In edit mode (A2) it hosts a LIVE
                // PreviewQuickSceneRoot fed the shared frame state, so scrubbing /
                // playback only moves the playhead with zero readback. The export
                // render (editable=false → chartSceneBinder stays null) instead
                // shows the static grab Image below.
                Loader {
                    id: liveChartLoader
                    anchors.fill: parent
                    opacity: layerItem.ld && layerItem.ld.opacity !== undefined ? layerItem.ld.opacity : 1.0
                    active: layerItem.isChartFrame && canvas.editable
                            && canvas.chartSceneBinder !== null
                            && layerItem.isActiveChartFrame
                            && layerItem.ld && layerItem.ld.visible
                    sourceComponent: liveChartComponent
                    property var boundItem: null
                    property var boundBinder: null
                    function syncLiveChartBinding() {
                        if (boundBinder && boundItem && boundBinder.unbindLiveChartScene)
                            boundBinder.unbindLiveChartScene(boundItem)
                        boundBinder = canvas.chartSceneBinder
                        boundItem = item
                        if (boundBinder && boundItem && boundBinder.bindLiveChartScene)
                            boundBinder.bindLiveChartScene(boundItem)
                    }
                    onItemChanged: syncLiveChartBinding()
                    Connections {
                        target: canvas
                        function onChartSceneBinderChanged() {
                            liveChartLoader.syncLiveChartBinding()
                        }
                    }
                }
                // Static grab still: a square playfield grab served by the
                // "coverchart" C++ image provider, keyed by the layer key. Used by
                // the export render and as a fallback when no live scene is bound.
                // The imageRevision suffix busts QML's URL cache on each re-grab; an
                // imageRevision < 0 means "not rendered yet" → blank source.
                Image {
                    anchors.fill: parent
                    opacity: layerItem.ld && layerItem.ld.opacity !== undefined ? layerItem.ld.opacity : 1.0
                    visible: layerItem.isChartFrame
                             && !(layerItem.isActiveChartFrame && canvas.liveChartSceneBound)
                             && layerItem.ld && layerItem.ld.imageRevision >= 0
                    source: (layerItem.isChartFrame && layerItem.ld && layerItem.ld.imageRevision >= 0)
                            ? ("image://coverchart/" + layerItem.ld.key + "?r=" + layerItem.ld.imageRevision)
                            : ""
                    fillMode: Image.PreserveAspectFit
                    asynchronous: false
                    cache: false
                    smooth: true
                    mipmap: true
                }

                // Custom image layer. The box aspect already tracks the image's
                // intrinsic aspect (CoverLayer.contentAspect), so PreserveAspectFit
                // exactly fills the box with no letterbox / distortion. Direct child
                // (visible-gated), mirroring the chart-frame still Image above.
                Image {
                    anchors.fill: parent
                    visible: layerItem.isImage
                    source: (layerItem.isImage && layerItem.ld && layerItem.ld.imagePath)
                            ? canvas.localFileUrl(layerItem.ld.imagePath) : ""
                    fillMode: Image.PreserveAspectFit
                    // Cap the decoded texture to the on-screen size (preview small,
                    // export full-res) so a huge source doesn't decode at native
                    // megapixels.
                    sourceSize: (layerItem.isImage && width > 1 && height > 1)
                                ? Qt.size(Math.ceil(width), Math.ceil(height)) : undefined
                    asynchronous: false
                    cache: false
                    smooth: true
                    mipmap: true
                }

                // Custom text layer. A hidden probe measures the glyphs' true aspect
                // (unconstrained, fixed pixelSize) and writes it back onto the layer;
                // the visible Text then Fits the aspect-correct box. Empty text (any
                // non-text layer) yields a zero-width probe → writeTextAspect no-ops.
                FontLoader {
                    id: textFont
                    source: canvas.fontSourceUrlForLayer(layerItem.ld)
                }
                Text {
                    id: textProbe
                    visible: false
                    text: (layerItem.isText && layerItem.ld) ? layerItem.ld.text : ""
                    font.family: textFont.name
                    font.bold: (layerItem.isText && layerItem.ld) ? layerItem.ld.textBold : false
                    font.pixelSize: 100
                    wrapMode: Text.NoWrap
                    maximumLineCount: 1
                    onContentWidthChanged: { if (layerItem.isText) canvas.writeTextAspect(layerItem.ld, textProbe) }
                    onContentHeightChanged: { if (layerItem.isText) canvas.writeTextAspect(layerItem.ld, textProbe) }
                }
                Text {
                    anchors.fill: parent
                    visible: layerItem.isText
                    text: (layerItem.isText && layerItem.ld) ? layerItem.ld.text : ""
                    color: (layerItem.ld && layerItem.ld.textColor) ? layerItem.ld.textColor : "#FFFFFF"
                    font.family: textFont.name
                    font.bold: (layerItem.isText && layerItem.ld) ? layerItem.ld.textBold : false
                    font.pixelSize: Math.max(1, Math.round(parent.height))
                    minimumPixelSize: 1
                    fontSizeMode: Text.Fit
                    wrapMode: Text.NoWrap
                    maximumLineCount: 1
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }

            TapHandler {
                enabled: canvas.editable
                onTapped: {
                    if (canvas.selectionBinder || canvas.chartSceneBinder) {
                        var p = layerItem.mapToItem(canvas, point.position.x, point.position.y)
                        canvas.selectLayerKey(canvas.hitKeyAt(p.x, p.y))
                    }
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

    // Live chart-frame scene (edit mode, A2). A bare PreviewQuickSceneRoot whose
    // layer flags / shared frame state are wired in C++ by
    // A live scene binding (overlay layers only over transparent).
    // anchors.fill tracks the layer's drag/scale.
    Component {
        id: liveChartComponent
        PreviewQuickSceneRoot {
            anchors.fill: parent
            // The export grab clips overlay geometry to its square framebuffer
            // (SceneFrameRenderer renders into a side×side window). Clip the live
            // scene to the same square box so out-of-bounds effects (fireworks /
            // slide trails / muri actions) can't bleed over the background/card in
            // the preview — keeps preview == export at the frame edges.
            clip: true
        }
    }

    // ===================== Selection chrome (editor only) =====================
    // Drawn at canvas level so it always sits on top and the scale handle is a
    // direct child of the canvas (guaranteed input delivery, no parent-bounds clip).
    Rectangle {
        id: selectionBorder
        readonly property var l: canvas.selectedLayer
        // l.visible: a layer un-ticked from the dialog (card / chart frame) must
        // not keep showing its selection chrome.
        visible: canvas.editable && l !== null && l.visible
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
    // Scale handle — visual indicator + its own DragHandler for the full hit zone.
    // The handle is a canvas-level hit target, and hitKeyAt() also treats it as the
    // selected layer so overlapping layers cannot become active during resize.
    Item {
        id: scaleHandle
        readonly property var l: canvas.selectedLayer
        visible: canvas.editable && l !== null && l.visible && !l.locked
        width: canvas.scaleHandleSize()
        height: width
        z: 9001
        x: l ? (l.nx * canvas.width + canvas.layerContentW(l) / 2 - width / 2) : -100
        y: l ? (l.ny * canvas.height + canvas.layerContentH(l) / 2 - height / 2) : -100
        // Visual indicator (same 18×18 blue square, centred inside the larger hit zone)
        Rectangle {
            anchors.centerIn: parent
            width: 18
            height: 18
            radius: 4
            color: "#3DA9FC"
            border.color: "#FFFFFF"
            border.width: 2
        }
        DragHandler {
            id: scaleDrag
            target: null
            enabled: scaleHandle.visible
            grabPermissions: PointerHandler.CanTakeOverFromAnything
                             | PointerHandler.ApprovesCancellation
            // Start state captured AT ACTIVATION so the scale is a delta from the
            // grab, not an absolute |cursor − centre|. This avoids the activation
            // jump (drag threshold + wherever on the handle you grabbed) and the
            // old Math.abs flip (growing again when dragged past the centre).
            property real grabSceneY: 0
            property real startHeightPx: 0
            onActiveChanged: {
                if (active && scaleHandle.l) {
                    grabSceneY = centroid.position.y
                    startHeightPx = scaleHandle.l.sizeFraction * canvas.height
                } else {
                    canvas.clearGuides()
                    if (!active)
                        canvas.commitGeometry()
                }
            }
            onCentroidChanged: {
                if (!active || !scaleHandle.l) return
                // The layer scales about its centre, so the bottom handle tracks the
                // cursor 1:1 while the height changes by 2× the cursor's vertical delta.
                var dy = centroid.position.y - grabSceneY
                var newH = Math.max(canvas.height * 0.05, startHeightPx + 2 * dy)
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
