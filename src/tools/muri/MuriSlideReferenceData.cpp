#include "tools/muri/MuriSlideReferenceData.h"

#include <QFile>
#include <QJsonDocument>

namespace miacode::muri::detail {

const QJsonObject& slideRuntimeRoot()
{
    static const QJsonObject root = []() {
        QFile file(":/data/slide_data.json");
        if (!file.open(QIODevice::ReadOnly)) {
            return QJsonObject();
        }
        QJsonParseError error;
        const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject()) {
            return QJsonObject();
        }
        return doc.object();
    }();
    return root;
}

QVector<QStringList> loadPadAreaSequence(const QJsonArray& areaArray)
{
    QVector<QStringList> sequence;
    sequence.reserve(areaArray.size());
    for (const QJsonValue& areaValue : areaArray) {
        QStringList pads;
        for (const QJsonValue& padValue : areaValue.toArray()) {
            const QString pad = padValue.toString();
            if (!pad.isEmpty()) {
                pads.append(pad);
            }
        }
        if (!pads.isEmpty()) {
            sequence.append(pads);
        }
    }
    return sequence;
}

QVector<QVector<QStringList>> loadTriPadAreaSequence(const QJsonArray& laneArray)
{
    QVector<QVector<QStringList>> sequence;
    sequence.reserve(laneArray.size());
    for (const QJsonValue& value : laneArray) {
        sequence.append(loadPadAreaSequence(value.toArray()));
    }
    return sequence;
}

QVector<QPointF> loadRuntimeSlideActionPath(const QString& key)
{
    QVector<QPointF> points;
    const QJsonObject entry = slideRuntimeRoot().value(QStringLiteral("slides")).toObject().value(key).toObject();
    if (entry.isEmpty()) {
        return points;
    }

    const QJsonArray sampleArray = entry.value(QStringLiteral("real_path_samples")).toArray();
    points.reserve(sampleArray.size());
    for (const QJsonValue& sampleValue : sampleArray) {
        const QJsonObject sampleObject = sampleValue.toObject();
        points.append(QPointF(
            sampleObject.value(QStringLiteral("x")).toDouble(),
            sampleObject.value(QStringLiteral("y")).toDouble()));
    }
    return points;
}

QVector<QVector<QPointF>> loadRuntimeWifiActionPaths(const QString& key)
{
    QVector<QVector<QPointF>> paths;
    const QJsonObject entry = slideRuntimeRoot().value(QStringLiteral("wifi")).toObject().value(key).toObject();
    if (entry.isEmpty()) {
        return paths;
    }

    const QJsonArray pathArray = entry.value(QStringLiteral("di_real_path_samples")).toArray();
    paths.reserve(pathArray.size());
    for (const QJsonValue& pathValue : pathArray) {
        QVector<QPointF> points;
        const QJsonArray sampleArray = pathValue.toArray();
        points.reserve(sampleArray.size());
        for (const QJsonValue& sampleValue : sampleArray) {
            const QJsonObject sampleObject = sampleValue.toObject();
            points.append(QPointF(
                sampleObject.value(QStringLiteral("x")).toDouble(),
                sampleObject.value(QStringLiteral("y")).toDouble()));
        }
        if (!points.isEmpty()) {
            paths.append(points);
        }
    }
    return paths;
}

}  // namespace miacode::muri::detail
