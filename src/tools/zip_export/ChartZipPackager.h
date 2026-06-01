#pragma once

#include <QString>
#include <QStringList>

#include <functional>

// Packs the current chart project into a single .zip that can be dropped
// straight into a player / re-imported. Layout written into the archive:
//
//   maidata.txt        <- the chart body (SimaiDocument::toText), renamed
//   track.mp3          <- sibling track, if present
//   bg.{jpg,png,jpeg}  <- single sibling background image, if present
//   <video>.mp4        <- single PV / &video= target, if it lives next to
//                         the chart (originals keep their filename so the
//                         sibling-resolution rules still match on unpack)
//
// Compression library is vendored miniz (third_party/miniz). The packing
// itself is backend-neutral and has no Qt-widget dependency, so it stays a
// focused, unit-testable unit instead of living in a MainWindow god-file.
namespace miacode::zip_export {

struct ChartZipInput {
    // Chart body to write as maidata.txt (already field-applied).
    QString chartText;
    // Path of the chart on disk; used to resolve sibling assets and the
    // default package directory. May be empty for an unsaved chart, in
    // which case only maidata.txt is written.
    QString chartPath;
    // Raw value of the chart `&video=` field (SimaiDocument::videoPath).
    QString videoFieldValue;
    // Destination .zip path (already chosen by the caller's save dialog).
    QString outputZipPath;
};

struct ChartZipResult {
    bool ok = false;
    bool canceled = false;
    QString errorMessage;
    // Archive-relative names of everything that made it into the zip,
    // for the success popup.
    QStringList includedEntries;
};

// Progress callback: (oneBasedIndex, total, archiveEntryName). Return
// false to request cancellation; packing then aborts and the partial
// .zip is removed. Pass an empty std::function to skip progress.
using ChartZipProgressFn = std::function<bool(int current, int total, const QString& entryName)>;

ChartZipResult packChartToZip(const ChartZipInput& input, const ChartZipProgressFn& progress = {});

// File-name-safe stem derived from the chart title (mirrors the export
// pipeline's cleaning); falls back to "chart" when the title is empty or
// reduces to nothing after stripping reserved characters.
QString sanitizedZipStem(const QString& title);

}  // namespace miacode::zip_export
