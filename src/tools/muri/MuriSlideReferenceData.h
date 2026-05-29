#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QVector>

// Slide-runtime reference-data layer for the Muri analyzer (2026-05-29 god-file
// decomposition). Encapsulates the read-only `:/data/slide_data.json` resource
// and the typed accessors the slide/wifi judge simulation needs (per-area pad
// sequences and recorded real-path samples). Internal to the Muri analyzer —
// namespace miacode::muri::detail.
namespace miacode::muri::detail {

// Lazily-loaded, cached root object of `:/data/slide_data.json`.
const QJsonObject& slideRuntimeRoot();

// Decode one area array ([[pad,...], ...]) into a per-area pad-name sequence.
QVector<QStringList> loadPadAreaSequence(const QJsonArray& areaArray);
// Decode a per-lane array of area arrays (wifi tri-pad lanes).
QVector<QVector<QStringList>> loadTriPadAreaSequence(const QJsonArray& laneArray);

// Recorded real-path sample points for a slide key (slides[key].real_path_samples).
QVector<QPointF> loadRuntimeSlideActionPath(const QString& key);
// Recorded per-lane real-path samples for a wifi key (wifi[key].di_real_path_samples).
QVector<QVector<QPointF>> loadRuntimeWifiActionPaths(const QString& key);

}  // namespace miacode::muri::detail
