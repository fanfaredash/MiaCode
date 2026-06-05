#pragma once

#include <QSize>
#include <QString>

struct IntroBannerSpec;

namespace miacode::cover_export {

struct CoverExportResult {
    bool success = false;
    QString outputPath;       // absolute path of the saved image (on success)
    QString errorMessage;     // populated on failure
};

// Render the maimai difficulty banner card (src/intro/qml/MaimaiBannerCard.qml)
// to a single still image — the program-internal port of the standalone
// tools/intro_remotion render-banner-from-maidata.ps1 exporter.
//
//   • `banner` carries the card payload (title/artist/designer/level/difficulty/
//     bpm/mode/jacketPath); the same struct buildIntroBannerSpec produces for the
//     video-export intro pre-roll.
//   • `size` is the output canvas in pixels (independent of the video-export size).
//   • `transparentBackground` true → the card alone over a transparent canvas,
//     saved as PNG (JPEG cannot carry alpha); false → the card over its own
//     blurred-jacket backdrop, saved as JPEG.
//   • The file is written to `outputDirectory` as card.jpg / card.png, adding
//     "(1)", "(2)" … on collision.
CoverExportResult exportIntroCover(
    const IntroBannerSpec& banner,
    const QSize& size,
    bool transparentBackground,
    const QString& outputDirectory);

}  // namespace miacode::cover_export
