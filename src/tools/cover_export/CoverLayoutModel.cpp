#include "tools/cover_export/CoverLayoutModel.h"

#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QSet>
#include <QJsonValue>
#include <QSize>

#include <algorithm>
#include <limits>

namespace miacode::cover_export {

namespace {
constexpr char kCardKey[] = "card";
constexpr char kChartFrameKey[] = "chartFrame";
constexpr char kChartFrameKeyPrefix[] = "chartFrame";
constexpr char kImageKey[] = "image";
constexpr char kTextKey[] = "text";

// Default geometry for a freshly added custom image / text layer: centred, ~half
// the canvas height, in front of everything.
constexpr qreal kFreeLayerDefaultNx = 0.5;
constexpr qreal kFreeLayerDefaultNy = 0.5;
constexpr qreal kImageDefaultSizeFraction = 0.5;
constexpr qreal kTextDefaultSizeFraction = 0.12;

// Intrinsic width/height of `path`, or 1.0 when it can't be read.
qreal imageAspectForPath(const QString& path)
{
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        return 1.0;
    }
    QImageReader reader(path);
    const QSize size = reader.size();
    if (size.width() > 0 && size.height() > 0) {
        return static_cast<qreal>(size.width()) / static_cast<qreal>(size.height());
    }
    return 1.0;
}

// The difficulty card centred, sized so its body (≈0.85 of canvas height incl.
// the tab shoulder) roughly matches the legacy card-fills-canvas cover. z=1 keeps
// it in FRONT of the chart-frame backdrop (z=0).
constexpr qreal kCardDefaultNx = 0.5;
constexpr qreal kCardDefaultNy = 0.5;
constexpr qreal kCardDefaultSizeFraction = 0.85;
constexpr int kCardDefaultZ = 1;

// Default geometry for newly added chart-frame stills: a square playfield grab,
// centred and large enough for the common "playfield hero + difficulty-card
// badge" composition.
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

qreal clampUnit(qreal v)
{
    if (v < 0.0) return 0.0;
    if (v > 1.0) return 1.0;
    return v;
}

bool isChartFrameKind(const QString& kind)
{
    return kind == QString::fromLatin1(kChartFrameKey);
}

QString normalizedFrameBgMode(const QString& mode)
{
    if (mode == QStringLiteral("transparent") || mode == QStringLiteral("clear")) {
        return QStringLiteral("transparent");
    }
    if (mode == QStringLiteral("image") || mode == QStringLiteral("jacket")) {
        return QStringLiteral("image");
    }
    return QStringLiteral("image");
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

void CoverLayer::setOpacity(qreal v)
{
    v = clampUnit(v);
    if (qFuzzyCompare(opacity_, v)) return;
    opacity_ = v;
    emit opacityChanged();
}

void CoverLayer::setFrameSeconds(double v)
{
    if (v < 0.0) v = 0.0;
    if (qFuzzyCompare(frameSeconds_, v)) return;
    frameSeconds_ = v;
    emit frameSecondsChanged();
}

void CoverLayer::setFrameBgEnabled(bool v)
{
    if (frameBgEnabled_ != v) {
        frameBgEnabled_ = v;
        emit frameBgEnabledChanged();
    }
    if (v) {
        setFrameBgMode(QStringLiteral("image"));
    } else {
        setFrameBgMode(QStringLiteral("transparent"));
        setFrameBgTransparency(1.0);
    }
}

void CoverLayer::setFrameBgMode(const QString& v)
{
    const QString next = normalizedFrameBgMode(v);
    if (frameBgMode_ == next) return;
    frameBgMode_ = next;
    emit frameBgModeChanged();
    const bool nextEnabled = next == QStringLiteral("image");
    if (frameBgEnabled_ != nextEnabled) {
        frameBgEnabled_ = nextEnabled;
        emit frameBgEnabledChanged();
    }
}

void CoverLayer::setFrameBgBrightness(qreal v)
{
    v = clampUnit(v);
    if (qFuzzyCompare(frameBgBrightness_, v)) return;
    frameBgBrightness_ = v;
    emit frameBgBrightnessChanged();
}

void CoverLayer::setFrameBgTransparency(qreal v)
{
    v = clampUnit(v);
    if (qFuzzyCompare(frameBgTransparency_, v)) return;
    frameBgTransparency_ = v;
    emit frameBgTransparencyChanged();
}

void CoverLayer::setFrameStyle(const QString& v)
{
    if (frameStyle_ == v) return;
    frameStyle_ = v;
    emit frameStyleChanged();
}

void CoverLayer::setImagePath(const QString& v)
{
    if (imagePath_ != v) {
        imagePath_ = v;
        emit imagePathChanged();
    }
    // Refresh the intrinsic aspect whenever the file is readable; a missing file
    // keeps the last-known aspect (cross-machine: box stays sane, no blank grab).
    if (!v.isEmpty() && QFileInfo::exists(v)) {
        setContentAspect(imageAspectForPath(v));
    }
}

void CoverLayer::setText(const QString& v)
{
    if (text_ == v) return;
    text_ = v;
    emit textChanged();
}

void CoverLayer::setFontPath(const QString& v)
{
    if (fontPath_ == v) return;
    fontPath_ = v;
    emit fontPathChanged();
}

void CoverLayer::setTextColor(const QString& v)
{
    if (textColor_ == v) return;
    textColor_ = v;
    emit textColorChanged();
}

void CoverLayer::setTextBold(bool v)
{
    if (textBold_ == v) return;
    textBold_ = v;
    emit textBoldChanged();
}

void CoverLayer::setContentAspect(qreal v)
{
    if (v < 0.02) v = 0.02;
    if (v > 50.0) v = 50.0;
    if (qFuzzyCompare(contentAspect_, v)) return;
    contentAspect_ = v;
    emit contentAspectChanged();
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

CoverLayer* CoverLayoutModel::addLayerFromJson(const QJsonObject& obj)
{
    const QString key = obj.value(QStringLiteral("key")).toString();
    const QString kind = obj.value(QStringLiteral("kind")).toString();
    if (key.isEmpty() || kind.isEmpty() || layer(key) != nullptr) {
        return nullptr;
    }
    CoverLayer* out = addLayer(key, kind, obj.value(QStringLiteral("label")).toString());
    if (out->label_.isEmpty()) {
        out->label_ = isChartFrameKind(kind)
            ? QStringLiteral("Chart frame")
            : kind;
    }
    applyDefaults(out);
    return out;
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
        layer->opacity_ = 1.0;
    } else if (layer->kind_ == QString::fromLatin1(kChartFrameKey)) {
        layer->nx_ = kChartFrameDefaultNx;
        layer->ny_ = kChartFrameDefaultNy;
        layer->sizeFraction_ = kChartFrameDefaultSizeFraction;
        layer->z_ = kChartFrameDefaultZ;
        layer->visible_ = false;   // opt-in via "add chart frame"
        layer->locked_ = false;
        layer->opacity_ = 1.0;
        layer->frameBgEnabled_ = true;
        layer->frameBgMode_ = QStringLiteral("image");
        layer->frameBgBrightness_ = 0.8;
        layer->frameBgTransparency_ = 0.5;
    } else if (layer->kind_ == QString::fromLatin1(kImageKey)) {
        layer->nx_ = kFreeLayerDefaultNx;
        layer->ny_ = kFreeLayerDefaultNy;
        layer->sizeFraction_ = kImageDefaultSizeFraction;
        layer->z_ = 2;   // factory raises to front via setZ(maxZ()+1)
        layer->visible_ = true;
        layer->locked_ = false;
        layer->opacity_ = 1.0;
    } else if (layer->kind_ == QString::fromLatin1(kTextKey)) {
        layer->nx_ = kFreeLayerDefaultNx;
        layer->ny_ = kFreeLayerDefaultNy;
        layer->sizeFraction_ = kTextDefaultSizeFraction;
        layer->z_ = 2;   // factory raises to front via setZ(maxZ()+1)
        layer->visible_ = true;
        layer->locked_ = false;
        layer->opacity_ = 1.0;
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

QString CoverLayoutModel::cardKey()
{
    return QString::fromLatin1(kCardKey);
}

QString CoverLayoutModel::legacyChartFrameKey()
{
    return QString::fromLatin1(kChartFrameKey);
}

bool CoverLayoutModel::chartFrameEnabled() const
{
    CoverLayer* l = layer(QString::fromLatin1(kChartFrameKey));
    return l != nullptr && l->visible();
}

void CoverLayoutModel::setChartFrameEnabled(bool enabled)
{
    CoverLayer* l = layer(QString::fromLatin1(kChartFrameKey));
    if (l == nullptr && enabled) {
        l = addChartFrameLayer(0.0);
    }
    if (l != nullptr) l->setVisible(enabled);
}

CoverLayer* CoverLayoutModel::addChartFrameLayer(double frameSeconds)
{
    const QString key = nextChartFrameKey();
    CoverLayer* chartFrame = addLayer(key,
                                      QString::fromLatin1(kChartFrameKey),
                                      QStringLiteral("Chart frame"));
    applyDefaults(chartFrame);
    chartFrame->setFrameSeconds(frameSeconds);
    chartFrame->setVisible(true);
    chartFrame->setZ(maxZ() + 1);
    emit layersChanged();
    return chartFrame;
}

CoverLayer* CoverLayoutModel::addChartFrameLayerFromTemplate(const CoverLayer* source, double fallbackFrameSeconds)
{
    CoverLayer* chartFrame = addChartFrameLayer(source != nullptr ? source->frameSeconds() : fallbackFrameSeconds);
    if (chartFrame == nullptr || source == nullptr || !isChartFrameKind(source->kind_)) {
        return chartFrame;
    }

    chartFrame->setSizeFraction(source->sizeFraction());
    chartFrame->setVisible(source->visible());
    chartFrame->setLocked(source->locked());
    chartFrame->setOpacity(source->opacity());
    chartFrame->setFrameBgMode(source->frameBgMode());
    chartFrame->setFrameBgBrightness(source->frameBgBrightness());
    chartFrame->setFrameBgTransparency(source->frameBgTransparency());
    chartFrame->setFrameStyle(source->frameStyle());
    return chartFrame;
}

CoverLayer* CoverLayoutModel::addImageLayer(const QString& imagePath)
{
    const QString key = nextKeyWithPrefix(QString::fromLatin1(kImageKey));
    CoverLayer* image = addLayer(key, QString::fromLatin1(kImageKey),
                                 QStringLiteral("Image"));
    applyDefaults(image);
    image->setImagePath(imagePath);   // also seeds contentAspect from the file
    image->setVisible(true);
    image->setZ(maxZ() + 1);
    emit layersChanged();
    return image;
}

CoverLayer* CoverLayoutModel::addTextLayer(const QString& text)
{
    const QString key = nextKeyWithPrefix(QString::fromLatin1(kTextKey));
    CoverLayer* layer = addLayer(key, QString::fromLatin1(kTextKey),
                                 QStringLiteral("Text"));
    applyDefaults(layer);
    layer->setText(text);
    // Rough seed until QML lays the glyphs out and writes the true aspect back.
    layer->setContentAspect(qMax<qreal>(1.0, text.length() * 0.55));
    layer->setVisible(true);
    layer->setZ(maxZ() + 1);
    emit layersChanged();
    return layer;
}

CoverLayer* CoverLayoutModel::duplicateLayer(const QString& key)
{
    CoverLayer* src = layer(key);
    if (src == nullptr || src->kind_ == QString::fromLatin1(kCardKey)) {
        return nullptr;
    }
    CoverLayer* copy = addLayer(nextKeyWithPrefix(src->kind_), src->kind_, src->label_);
    copy->nx_ = src->nx_;
    copy->ny_ = src->ny_;
    copy->sizeFraction_ = src->sizeFraction_;
    copy->z_ = maxZ() + 1;
    copy->visible_ = src->visible_;
    copy->locked_ = src->locked_;
    copy->opacity_ = src->opacity_;
    copy->frameImage_ = src->frameImage_;
    copy->imageRevision_ = src->imageRevision_;
    copy->frameSeconds_ = src->frameSeconds_;
    copy->frameBgEnabled_ = src->frameBgEnabled_;
    copy->frameBgMode_ = src->frameBgMode_;
    copy->frameBgBrightness_ = src->frameBgBrightness_;
    copy->frameBgTransparency_ = src->frameBgTransparency_;
    copy->frameStyle_ = src->frameStyle_;
    copy->imagePath_ = src->imagePath_;
    copy->text_ = src->text_;
    copy->fontPath_ = src->fontPath_;
    copy->textColor_ = src->textColor_;
    copy->textBold_ = src->textBold_;
    copy->contentAspect_ = src->contentAspect_;
    emit layersChanged();
    return copy;
}

bool CoverLayoutModel::removeLayer(const QString& key)
{
    if (key == QString::fromLatin1(kCardKey)) {
        return false;
    }
    const int index = indexOfKey(key);
    if (index < 0) {
        return false;
    }
    CoverLayer* removed = layers_.takeAt(index);
    removed->deleteLater();
    normalizeZOrder();
    emit layersChanged();
    return true;
}

QString CoverLayoutModel::selectionAfterRemoval(const QString& key) const
{
    // Mirror the list view order (CoverLayerListModel::layerAt): z descending.
    QList<CoverLayer*> ordered = layers_;
    std::sort(ordered.begin(), ordered.end(), [](const CoverLayer* a, const CoverLayer* b) {
        return a->z() > b->z();
    });
    int idx = -1;
    for (int i = 0; i < ordered.size(); ++i) {
        if (ordered.at(i)->key_ == key) {
            idx = i;
            break;
        }
    }
    if (idx < 0) {
        return cardKey();
    }
    // After removing row idx, the row below (idx+1) shifts up into idx — select it;
    // if idx was the last row, fall back to the previous; else the stable card.
    if (idx + 1 < ordered.size()) {
        return ordered.at(idx + 1)->key_;
    }
    if (idx - 1 >= 0) {
        return ordered.at(idx - 1)->key_;
    }
    return cardKey();
}

QList<CoverLayer*> CoverLayoutModel::chartFrameLayers() const
{
    QList<CoverLayer*> out;
    for (CoverLayer* layer : layers_) {
        if (layer != nullptr && isChartFrameKind(layer->kind_)) {
            out.append(layer);
        }
    }
    return out;
}

QList<CoverLayer*> CoverLayoutModel::visibleChartFrameLayers() const
{
    QList<CoverLayer*> out;
    for (CoverLayer* layer : chartFrameLayers()) {
        if (layer->visible()) {
            out.append(layer);
        }
    }
    return out;
}

bool CoverLayoutModel::moveLayerBefore(const QString& key, const QString& beforeKey)
{
    normalizeZOrder();
    const int from = indexOfKey(key);
    int to = indexOfKey(beforeKey);
    if (from < 0 || to < 0 || from == to) {
        return false;
    }
    layers_.move(from, to > from ? to - 1 : to);
    assignZOrderFromList();
    emit layersChanged();
    return true;
}

bool CoverLayoutModel::moveLayerAfter(const QString& key, const QString& afterKey)
{
    normalizeZOrder();
    const int from = indexOfKey(key);
    int to = indexOfKey(afterKey);
    if (from < 0 || to < 0 || from == to) {
        return false;
    }
    layers_.move(from, to < from ? to + 1 : to);
    assignZOrderFromList();
    emit layersChanged();
    return true;
}

void CoverLayoutModel::moveByViewRows(int fromRow, int toRow)
{
    // View order = z descending (mirror CoverLayerListModel::layerAt).
    QList<CoverLayer*> view = layers_;
    std::sort(view.begin(), view.end(), [](const CoverLayer* a, const CoverLayer* b) {
        return a->z() > b->z();
    });
    const int n = view.size();
    if (fromRow < 0 || fromRow >= n) {
        return;
    }
    int dest = toRow;
    if (dest < 0) {
        dest = 0;   // drop above the first row -> send to front
    }
    if (dest > n) {
        dest = n;   // drop past the end → send to back
    }
    CoverLayer* moving = view.takeAt(fromRow);
    if (dest > fromRow) {
        dest -= 1;   // removing fromRow shifted everything after it left by one
    }
    view.insert(qBound(0, dest, view.size()), moving);
    // Reassign z so the new view order holds: view row 0 = highest z (front).
    for (int i = 0; i < view.size(); ++i) {
        view.at(i)->setZ(view.size() - 1 - i);
    }
    emit layersChanged();
}

void CoverLayoutModel::normalizeZOrder()
{
    std::sort(layers_.begin(), layers_.end(), [](const CoverLayer* a, const CoverLayer* b) {
        if (a->z() == b->z()) {
            return a->key() < b->key();
        }
        return a->z() < b->z();
    });
    for (int i = 0; i < layers_.size(); ++i) {
        layers_.at(i)->setZ(i);
    }
}

void CoverLayoutModel::assignZOrderFromList()
{
    for (int i = 0; i < layers_.size(); ++i) {
        layers_.at(i)->setZ(i);
    }
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
    normalizeZOrder();
    emit layersChanged();
}

void CoverLayoutModel::sendToBack(int index)
{
    if (index < 0 || index >= layers_.size()) return;
    layers_.at(index)->setZ(minZ() - 1);
    normalizeZOrder();
    emit layersChanged();
}

void CoverLayoutModel::raiseLayer(int index)
{
    if (index < 0 || index >= layers_.size()) return;
    CoverLayer* moving = layers_.at(index);
    normalizeZOrder();
    index = layers_.indexOf(moving);
    if (index < 0) return;
    if (index + 1 >= layers_.size()) return;
    layers_.swapItemsAt(index, index + 1);
    assignZOrderFromList();
    emit layersChanged();
}

void CoverLayoutModel::lowerLayer(int index)
{
    if (index < 0 || index >= layers_.size()) return;
    CoverLayer* moving = layers_.at(index);
    normalizeZOrder();
    index = layers_.indexOf(moving);
    if (index < 0) return;
    if (index - 1 < 0) return;
    layers_.swapItemsAt(index, index - 1);
    assignZOrderFromList();
    emit layersChanged();
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
        emit layer->opacityChanged();
        emit layer->frameBgEnabledChanged();
        emit layer->frameBgModeChanged();
        emit layer->frameBgBrightnessChanged();
        emit layer->frameBgTransparencyChanged();
    }
    emit layersChanged();
}

QJsonObject CoverLayoutModel::toJson() const
{
    QJsonArray arr;
    for (CoverLayer* layer : layers_) {
        QJsonObject o;
        o.insert(QStringLiteral("key"), layer->key_);
        o.insert(QStringLiteral("kind"), layer->kind_);
        o.insert(QStringLiteral("label"), layer->label_);
        o.insert(QStringLiteral("nx"), layer->nx_);
        o.insert(QStringLiteral("ny"), layer->ny_);
        o.insert(QStringLiteral("sizeFraction"), layer->sizeFraction_);
        o.insert(QStringLiteral("z"), layer->z_);
        o.insert(QStringLiteral("visible"), layer->visible_);
        o.insert(QStringLiteral("locked"), layer->locked_);
        o.insert(QStringLiteral("opacity"), layer->opacity_);
        o.insert(QStringLiteral("frameSeconds"), layer->frameSeconds_);
        o.insert(QStringLiteral("frameBgEnabled"), layer->frameBgEnabled_);
        o.insert(QStringLiteral("frameBgMode"), layer->frameBgMode_);
        o.insert(QStringLiteral("frameBgBrightness"), layer->frameBgBrightness_);
        o.insert(QStringLiteral("frameBgTransparency"), layer->frameBgTransparency_);
        o.insert(QStringLiteral("frameStyle"), layer->frameStyle_);
        // Custom image / text layer fields (absent for card / chart-frame layers,
        // read back with defaults by fromJson).
        if (layer->kind_ == QString::fromLatin1(kImageKey)) {
            o.insert(QStringLiteral("imagePath"), layer->imagePath_);
            o.insert(QStringLiteral("contentAspect"), layer->contentAspect_);
        } else if (layer->kind_ == QString::fromLatin1(kTextKey)) {
            o.insert(QStringLiteral("text"), layer->text_);
            o.insert(QStringLiteral("fontPath"), layer->fontPath_);
            o.insert(QStringLiteral("textColor"), layer->textColor_);
            o.insert(QStringLiteral("textBold"), layer->textBold_);
            o.insert(QStringLiteral("contentAspect"), layer->contentAspect_);
        }
        arr.append(o);
    }
    QJsonObject root;
    root.insert(QStringLiteral("layers"), arr);
    return root;
}

void CoverLayoutModel::fromJson(const QJsonObject& obj)
{
    const QJsonArray arr = obj.value(QStringLiteral("layers")).toArray();
    QSet<QString> seenKeys;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        QString key = o.value(QStringLiteral("key")).toString();
        QString kind = o.value(QStringLiteral("kind")).toString();
        if (kind.isEmpty() && key == QString::fromLatin1(kChartFrameKey)) {
            kind = QString::fromLatin1(kChartFrameKey);
        }
        if (kind.isEmpty() && key == QString::fromLatin1(kCardKey)) {
            kind = QString::fromLatin1(kCardKey);
        }
        CoverLayer* layer = this->layer(key);
        if (layer == nullptr && !key.isEmpty() && !kind.isEmpty()) {
            QJsonObject copy = o;
            copy.insert(QStringLiteral("kind"), kind);
            layer = addLayerFromJson(copy);
        }
        if (layer == nullptr) continue;
        seenKeys.insert(layer->key_);
        if (o.contains(QStringLiteral("label"))) {
            layer->label_ = o.value(QStringLiteral("label")).toString(layer->label_);
        }
        layer->setNx(o.value(QStringLiteral("nx")).toDouble(layer->nx_));
        layer->setNy(o.value(QStringLiteral("ny")).toDouble(layer->ny_));
        layer->setSizeFraction(o.value(QStringLiteral("sizeFraction")).toDouble(layer->sizeFraction_));
        layer->setZ(o.value(QStringLiteral("z")).toInt(layer->z_));
        layer->setVisible(o.value(QStringLiteral("visible")).toBool(layer->visible_));
        layer->setLocked(o.value(QStringLiteral("locked")).toBool(layer->locked_));
        layer->setOpacity(o.value(QStringLiteral("opacity")).toDouble(layer->opacity_));
        layer->setFrameSeconds(o.value(QStringLiteral("frameSeconds")).toDouble(layer->frameSeconds_));
        if (o.contains(QStringLiteral("frameBgMode"))) {
            layer->setFrameBgMode(o.value(QStringLiteral("frameBgMode")).toString(layer->frameBgMode_));
        } else {
            layer->setFrameBgEnabled(o.value(QStringLiteral("frameBgEnabled")).toBool(layer->frameBgEnabled_));
        }
        layer->setFrameBgBrightness(o.value(QStringLiteral("frameBgBrightness")).toDouble(layer->frameBgBrightness_));
        layer->setFrameBgTransparency(o.value(QStringLiteral("frameBgTransparency")).toDouble(layer->frameBgTransparency_));
        layer->setFrameStyle(o.value(QStringLiteral("frameStyle")).toString(layer->frameStyle_));
        // Custom image / text layer fields. setImagePath refreshes contentAspect
        // from the file when present; apply the stored aspect afterwards so a
        // MISSING image (cross-machine) still restores its saved proportions.
        if (layer->kind_ == QString::fromLatin1(kImageKey)) {
            if (o.contains(QStringLiteral("imagePath"))) {
                layer->setImagePath(o.value(QStringLiteral("imagePath")).toString(layer->imagePath_));
            }
            if (o.contains(QStringLiteral("contentAspect"))
                && !QFileInfo::exists(layer->imagePath())) {
                layer->setContentAspect(o.value(QStringLiteral("contentAspect")).toDouble(layer->contentAspect_));
            }
        } else if (layer->kind_ == QString::fromLatin1(kTextKey)) {
            layer->setText(o.value(QStringLiteral("text")).toString(layer->text_));
            layer->setFontPath(o.value(QStringLiteral("fontPath")).toString(layer->fontPath_));
            layer->setTextColor(o.value(QStringLiteral("textColor")).toString(layer->textColor_));
            layer->setTextBold(o.value(QStringLiteral("textBold")).toBool(layer->textBold_));
            layer->setContentAspect(o.value(QStringLiteral("contentAspect")).toDouble(layer->contentAspect_));
        }
    }
    for (int i = layers_.size() - 1; i >= 0; --i) {
        CoverLayer* l = layers_.at(i);
        if (l->key_ != QString::fromLatin1(kCardKey) && !seenKeys.contains(l->key_)) {
            layers_.removeAt(i);
            l->deleteLater();
        }
    }
    normalizeZOrder();
    emit layersChanged();
}

QString CoverLayoutModel::nextChartFrameKey() const
{
    const QString legacy = QString::fromLatin1(kChartFrameKey);
    if (layer(legacy) == nullptr) {
        return legacy;
    }
    for (int i = 2; i < 100000; ++i) {
        const QString key = QStringLiteral("%1%2").arg(QString::fromLatin1(kChartFrameKeyPrefix)).arg(i);
        if (layer(key) == nullptr) {
            return key;
        }
    }
    return QStringLiteral("%1%2").arg(QString::fromLatin1(kChartFrameKeyPrefix)).arg(layers_.size() + 1);
}

QString CoverLayoutModel::nextKeyWithPrefix(const QString& prefix) const
{
    if (layer(prefix) == nullptr) {
        return prefix;
    }
    for (int i = 2; i < 100000; ++i) {
        const QString key = QStringLiteral("%1%2").arg(prefix).arg(i);
        if (layer(key) == nullptr) {
            return key;
        }
    }
    return QStringLiteral("%1%2").arg(prefix).arg(layers_.size() + 1);
}

}  // namespace miacode::cover_export
