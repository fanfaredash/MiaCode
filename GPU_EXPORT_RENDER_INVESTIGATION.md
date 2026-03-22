# GPU Export Render Investigation

## Goal

This note summarizes the investigation into why GPU export renders, especially `slide` / `wifi`, look darker than realtime preview.

The target reference is the realtime preview path.

## High-level conclusion

The main divergence is not:

- chart timing data
- image resource loading
- sprite selection
- draw-call ordering
- PBO readback by itself

The main divergence is:

- realtime preview renders through `QPainter(this)` on a real `QOpenGLWindow`
- offscreen export paths render through `QOpenGLPaintDevice`

All tested FBO-based export paths currently belong to the `QOpenGLPaintDevice` side of that split, so they cluster together visually and stay darker than realtime preview.

## Key confirmed conclusions

### 1. The upper-layer render data is already aligned

For the disputed frame `<local-chart-dir>\maidata.txt` `MAS`, around `80.633333s`, object-level dumps matched across preview/export experiments:

- same object count
- same object types
- same positions
- same angles
- same scale
- same opacity
- same image content hash

So the mismatch starts after upper-layer render data has already been prepared.

### 2. Image reads are not the root cause

Both preview-side debug canvases and export canvases copy render state from the same source:

```cpp
exportCanvas.copyRenderStateFrom(*sourceCanvas);
diagWindowCompareCanvas.copyRenderStateFrom(*sourceCanvas);
```

And draw-call investigation showed the same image payload was used on both sides.

### 3. PBO readback from the window default framebuffer can match `grabFramebuffer()`

The default-framebuffer window PBO path is captured inside `paintGL()`:

```cpp
if (windowReadbackCaptureRequested_ && extra != nullptr) {
    const QSize pixelSize(
        qMax(1, qRound(size().width() * devicePixelRatioF())),
        qMax(1, qRound(size().height() * devicePixelRatioF()))
    );
    QString ignoredError;
    if (ensureWindowReadbackPbo(pixelSize, &ignoredError)) {
        GLint previousReadBuffer = GL_BACK;
        extra->glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
        extra->glPixelStorei(GL_PACK_ALIGNMENT, 4);
        extra->glReadBuffer(GL_BACK);
        extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, windowReadbackPbo_);
        extra->glReadPixels(
            0,
            0,
            pixelSize.width(),
            pixelSize.height(),
            GL_RGBA,
            GL_UNSIGNED_BYTE,
            nullptr
        );
        extra->glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        extra->glReadBuffer(previousReadBuffer);
        windowReadbackCapturedPixelSize_ = pixelSize;
        windowReadbackCaptureReady_ = true;
        windowReadbackIssuedSerial_ = windowReadbackRequestSerial_;
    }
    windowReadbackCaptureRequested_ = false;
}
```

After normalizing alpha, this PBO capture matched `grabFramebuffer()` for the same window path.

So PBO itself is not the remaining reason for the visual mismatch.

### 4. The old darker window reference was captured too early

The preview-window capture helper now supports waiting for a minimum swap count:

```cpp
const bool usePboReadback =
    envFlagEnabled(QStringLiteral("MIACODE_EXPORT_DEBUG_WINDOW_USE_PBO"));
bool minSwapsOk = false;
const int minSwapCountRequested =
    qEnvironmentVariableIntValue("MIACODE_EXPORT_DEBUG_WINDOW_MIN_SWAPS", &minSwapsOk);
const int minSwapCount = qMax(1, minSwapsOk ? minSwapCountRequested : 1);
```

And the wait loop uses that threshold:

```cpp
while (waitTimer.elapsed() < 1200) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    if (!bestCandidate.image.isNull() && swapCount >= minSwapCount) {
        break;
    }
    if (swapCount >= 2 && !lastCandidate.image.isNull()) {
        break;
    }
    if (waitTimer.elapsed() - lastForcedUpdateMs >= 33) {
        canvas->update();
        lastForcedUpdateMs = waitTimer.elapsed();
    }
}
```

With more swaps allowed, the window reference becomes much brighter and stable. That means the earlier, darker reference frame was not the true steady-state preview result.

### 5. Realtime preview and `QOpenGLPaintDevice` are the real split

The realtime window path in `paintGL()` is:

```cpp
if (forcePaintDeviceRenderingForDebug_) {
    QOpenGLPaintDevice paintDevice(size());
    paintDevice.setDevicePixelRatio(devicePixelRatioF());
    QPainter painter(&paintDevice);
    if (overlayOnlyForDebug_) {
        renderCanvas(painter, size(), false, false, false);
    } else {
        renderCanvas(painter);
    }
} else {
    QPainter painter(this);
    if (overlayOnlyForDebug_) {
        renderCanvas(painter, size(), false, false, false);
    } else {
        renderCanvas(painter);
    }
}
```

The important fact is that the normal realtime path is the `else` branch:

```cpp
QPainter painter(this);
renderCanvas(painter);
```

That is the visually correct reference we should follow.

### 6. Offscreen FBO paths currently all render through `QOpenGLPaintDevice`

Transparent overlay FBO:

```cpp
glRenderer_.beginFrame(safeSize, offscreenDpr);
{
    QOpenGLPaintDevice paintDevice(safeSize);
    paintDevice.setDevicePixelRatio(offscreenDpr);
    QPainter painter(&paintDevice);
    renderCanvas(painter, safeSize, false, false, false);
}
glRenderer_.endFrame();
```

Opaque full-frame FBO:

```cpp
glRenderer_.beginFrame(safeSize, offscreenDpr);
{
    QOpenGLPaintDevice paintDevice(safeSize);
    paintDevice.setDevicePixelRatio(offscreenDpr);
    QPainter painter(&paintDevice);
    renderCanvas(painter, safeSize, true, true, false);
}
glRenderer_.endFrame();
```

So even when the framebuffer policy changes, the painter backend does not. That explains why multiple offscreen routes still cluster near the darker result.
