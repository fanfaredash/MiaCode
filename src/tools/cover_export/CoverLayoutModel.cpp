#include "tools/cover_export/CoverLayoutModel.h"

#include <QJsonArray>
#include <QJsonValue>

#include <limits>

namespace miacode::cover_export {

namespace {
constexpr char kCardKey[] = "card";
constexpr char kChartFrameKey[] = "chartFrame";

// The difficulty card centred, sized so its body (≈0.85 of canvas height incl.
// the tab shoulder) roughly matches the legacy card-fills-canvas cover. z=1 keeps
// it in FRONT of the chart-frame backdrop (z=0).
constexpr qreal kCardDefaultNx = 0.5;
constexpr qreal kCardDefaultNy = 0.5;
constexpr qreal kCardDefaultSizeFraction = 0.85;
constexpr int kCardDefaultZ = 1;

// The chart-frame still: a square playfield grab, seeded centred + slightly
// larger and BEHIND the card (z=0, above the background layers), so the common
// "playfield hero + difficulty-card badge" composition appears on enable. Hidden
// by default until the user ticks "add chart frame".
constexpr qreal kChartFrameDefaultNx = 0.5;
constexpr qreal kChartFrameDefaultNy = 0.5;
constexpr qreal kChartFrameDefaultSizeFraction = 0.82;
constexpr int kChartFrameDefaultZ = 0;

qreal clamp01ish(qreal v)
{
    // Allow a little overscan so a layer can be dragged partly off-canvas, but
    // keep it from running away entirely.
    if (v < -0.5) return -0.5;
    if (v > 1.5) return 1.5;
    return v;
}
}  // namespace

void CoverLayer::setNx(qreal v)
{
    v = clamp01ish(v);
    if (qFuzzyCompare(nx_, v)) return;
    nx_ = v;
    emit nxChanged();
}

void CoverLayer::setNy(qreal v)
{
    v = clamp01ish(v);
    if (qFuzzyCompare(ny_, v)) return;
    ny_ = v;
    emit nyChanged();
}

void CoverLayer::setSizeFraction(qreal v)
{
    if (v < 0.05) v = 0.05;
    if (v > 2.0) v = 2.0;
    if (qFuzzyCompare(sizeFraction_, v)) return;
    sizeFraction_ = v;
    emit sizeFractionChanged();
}

void CoverLayer::setZ(int v)
{
    if (z_ == v) return;
    z_ = v;
    emit zChanged();
}

void CoverLayer::setVisible(bool v)
{
    if (visible_ == v) return;
    visible_ = v;
    emit visibleChanged();
}

void CoverLayer::setLocked(bool v)
{
    if (locked_ == v) return;
    locked_ = v;
    emit lockedChanged();
}

CoverLayoutModel::CoverLayoutModel(QObject* parent) : QObject(parent)
{
    ensureDefaultLayers();
}

void CoverLayoutModel::ensureDefaultLayers()
{
    if (layer(QString::fromLatin1(kCardKey)) == nullptr) {
        CoverLayer* card = addLayer(QString::fromLatin1(kCardKey), QString::fromLatin1(kCardKey),
                                    QStringLiteral("Difficulty card"));
        applyDefaults(card);
    }
    if (layer(QString::fromLatin1(kChartFrameKey)) == nullptr) {
        CoverLayer* chartFrame = addLayer(QString::fromLatin1(kChartFrameKey),
                                          QString::fromLatin1(kChartFrameKey),
                                          QStringLiteral("Chart frame"));
        applyDefaults(chartFrame);
    }
    emit layersChanged();
}

CoverLayer* CoverLayoutModel::addLayer(const QString& key, const QString& kind, const QString& label)
{
    auto* layer = new CoverLayer(this);
    layer->key_ = key;
    layer->kind_ = kind;
    layer->label_ = label;
    layers_.append(layer);
    return layer;
}

void CoverLayoutModel::applyDefaults(CoverLayer* layer) const
{
    if (layer == nullptr) return;
    if (layer->kind_ == QString::fromLatin1(kCardKey)) {
        layer->nx_ = kCardDefaultNx;
        layer->ny_ = kCardDefaultNy;
        layer->sizeFraction_ = kCardDefaultSizeFraction;
        layer->z_ = kCardDefaultZ;
        layer->visible_ = true;
        layer->locked_ = false;
    } else if (layer->kind_ == QString::fromLatin1(kChartFrameKey)) {
        layer->nx_ = kChartFrameDefaultNx;
        layer->ny_ = kChartFrameDefaultNy;
        layer->sizeFraction_ = kChartFrameDefaultSizeFraction;
        layer->z_ = kChartFrameDefaultZ;
        layer->visible_ = false;   // opt-in via "add chart frame"
        layer->locked_ = false;
    }
}

QList<QObject*> CoverLayoutModel::layersAsObjects() const
{
    QList<QObject*> out;
    out.reserve(layers_.size());
    for (CoverLayer* layer : layers_) {
        out.append(layer);
    }
    return out;
}

CoverLayer* CoverLayoutModel::layer(const QString& key) const
{
    for (CoverLayer* layer : layers_) {
        if (layer->key_ == key) return layer;
    }
    return nullptr;
}

int CoverLayoutModel::indexOfKey(const QString& key) const
{
    for (int i = 0; i < layers_.size(); ++i) {
        if (layers_.at(i)->key_ == key) return i;
    }
    return -1;
}

bool CoverLayoutModel::chartFrameEnabled() const
{
    CoverLayer* l = layer(QString::fromLatin1(kChartFrameKey));
    return l != nullptr && l->visible();
}

void CoverLayoutModel::setChartFrameEnabled(bool enabled)
{
    CoverLayer* l = layer(QString::fromLatin1(kChartFrameKey));
    if (l != nullptr) l->setVisible(enabled);
}

void CoverLayoutModel::setLayerImage(const QString& key, const QImage& image)
{
    CoverLayer* l = layer(key);
    if (l == nullptr) return;
    l->frameImage_ = image;
    l->imageRevision_ += 1;
    emit l->imageRevisionChanged();
}

int CoverLayoutModel::minZ() const
{
    int z = std::numeric_limits<int>::max();
    for (CoverLayer* layer : layers_) z = qMin(z, layer->z());
    return layers_.isEmpty() ? 0 : z;
}

int CoverLayoutModel::maxZ() const
{
    int z = std::numeric_limits<int>::min();
    for (CoverLayer* layer : layers_) z = qMax(z, layer->z());
    return layers_.isEmpty() ? 0 : z;
}

void CoverLayoutModel::bringToFront(int index)
{
    if (index < 0 || index >= layers_.size()) return;
    layers_.at(index)->setZ(maxZ() + 1);
}

void CoverLayoutModel::sendToBack(int index)
{
    if (index < 0 || index >= layers_.size()) return;
    layers_.at(index)->setZ(minZ() - 1);
}

void CoverLayoutModel::raiseLayer(int index)
{
    if (index < 0 || index >= layers_.size()) return;
    layers_.at(index)->setZ(layers_.at(index)->z() + 1);
}

void CoverLayoutModel::lowerLayer(int index)
{
    if (index < 0 || index >= layers_.size()) return;
    layers_.at(index)->setZ(layers_.at(index)->z() - 1);
}

void CoverLayoutModel::resetLayout()
{
    for (CoverLayer* layer : layers_) {
        applyDefaults(layer);
        emit layer->nxChanged();
        emit layer->nyChanged();
        emit layer->sizeFractionChanged();
        emit layer->zChanged();
        emit layer->visibleChanged();
        emit layer->lockedChanged();
    }
}

QJsonObject CoverLayoutModel::toJson() const
{
    QJsonArray arr;
    for (CoverLayer* layer : layers_) {
        QJsonObject o;
        o.insert(QStringLiteral("key"), layer->key_);
        o.insert(QStringLiteral("nx"), layer->nx_);
        o.insert(QStringLiteral("ny"), layer->ny_);
        o.insert(QStringLiteral("sizeFraction"), layer->sizeFraction_);
        o.insert(QStringLiteral("z"), layer->z_);
        o.insert(QStringLiteral("visible"), layer->visible_);
        o.insert(QStringLiteral("locked"), layer->locked_);
        o.insert(QStringLiteral("frameSeconds"), layer->frameSeconds_);
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("layers"), arr);
    return root;
}

void CoverLayoutModel::fromJson(const QJsonObject& obj)
{
    const QJsonArray arr = obj.value(QStringLiteral("layers")).toArray();
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        CoverLayer* layer = this->layer(o.value(QStringLiteral("key")).toString());
        if (layer == nullptr) continue;
        layer->setNx(o.value(QStringLiteral("nx")).toDouble(layer->nx_));
        layer->setNy(o.value(QStringLiteral("ny")).toDouble(layer->ny_));
        layer->setSizeFraction(o.value(QStringLiteral("sizeFraction")).toDouble(layer->sizeFraction_));
        layer->setZ(o.value(QStringLiteral("z")).toInt(layer->z_));
        layer->setVisible(o.value(QStringLiteral("visible")).toBool(layer->visible_));
        layer->setLocked(o.value(QStringLiteral("locked")).toBool(layer->locked_));
        layer->frameSeconds_ = o.value(QStringLiteral("frameSeconds")).toDouble(layer->frameSeconds_);
    }
    emit layersChanged();
}

}  // namespace miacode::cover_export
