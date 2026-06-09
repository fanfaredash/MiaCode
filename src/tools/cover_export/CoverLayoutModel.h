#pragma once

#include <QImage>
#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

namespace miacode::cover_export {

// One free-floating element of the cover composition. Geometry is stored
// NORMALISED to the canvas (0..1) so the same model drives both the small live
// preview and the full-resolution export with no rescaling:
//   • nx / ny       — layer CENTRE, as a fraction of canvas width / height.
//   • sizeFraction  — the layer content's reference HEIGHT as a fraction of the
//                     canvas height. The layer's pixel width follows from its
//                     intrinsic aspect (the card is portrait ~0.609), so a layer
//                     keeps its shape across any output aspect ratio.
//   • z             — paint order (higher = on top). Drives QQuickItem.z, not
//                     list position, so reordering never rebuilds delegates.
//
// A `kind=="chartFrame"` layer additionally carries a rendered chart still
// (`frameImage`, set from C++ via CoverLayoutModel::setLayerImage). QML can't
// bind a QImage directly, so the image is served through the "coverchart" image
// provider keyed by `key`, and `imageRevision` (bumped on every set) is folded
// into the source URL to bust the cache. `frameSeconds` records which chart time
// the still was grabbed at (for persistence / the frame-picker slider).
//
// QML reads/writes these via the Repeater's `modelData`; each property carries a
// NOTIFY so two-way bindings in the delegate stay live while dragging/scaling.
class CoverLayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QString kind READ kind CONSTANT)
    Q_PROPERTY(QString label READ label CONSTANT)
    Q_PROPERTY(qreal nx READ nx WRITE setNx NOTIFY nxChanged)
    Q_PROPERTY(qreal ny READ ny WRITE setNy NOTIFY nyChanged)
    Q_PROPERTY(qreal sizeFraction READ sizeFraction WRITE setSizeFraction NOTIFY sizeFractionChanged)
    Q_PROPERTY(int z READ z WRITE setZ NOTIFY zChanged)
    Q_PROPERTY(bool visible READ visible WRITE setVisible NOTIFY visibleChanged)
    Q_PROPERTY(bool locked READ locked WRITE setLocked NOTIFY lockedChanged)
    // -1 = no still rendered yet (QML shows nothing); >= 0 bumps on each new grab.
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY imageRevisionChanged)

public:
    explicit CoverLayer(QObject* parent = nullptr) : QObject(parent) {}

    QString key() const { return key_; }
    QString kind() const { return kind_; }
    QString label() const { return label_; }
    qreal nx() const { return nx_; }
    qreal ny() const { return ny_; }
    qreal sizeFraction() const { return sizeFraction_; }
    int z() const { return z_; }
    bool visible() const { return visible_; }
    bool locked() const { return locked_; }
    int imageRevision() const { return imageRevision_; }
    QImage frameImage() const { return frameImage_; }
    double frameSeconds() const { return frameSeconds_; }
    void setFrameSeconds(double v) { frameSeconds_ = v; }

    void setNx(qreal v);
    void setNy(qreal v);
    void setSizeFraction(qreal v);
    void setZ(int v);
    void setVisible(bool v);
    void setLocked(bool v);

signals:
    void nxChanged();
    void nyChanged();
    void sizeFractionChanged();
    void zChanged();
    void visibleChanged();
    void lockedChanged();
    void imageRevisionChanged();

private:
    friend class CoverLayoutModel;
    QString key_;
    QString kind_;
    QString label_;
    qreal nx_ = 0.5;
    qreal ny_ = 0.5;
    qreal sizeFraction_ = 0.85;
    int z_ = 0;
    bool visible_ = true;
    bool locked_ = false;
    QImage frameImage_;
    int imageRevision_ = -1;
    double frameSeconds_ = 0.0;
};

// Ordered set of composition layers, exposed to QML. Seeds the difficulty card
// (always visible) plus the chart-frame layer (hidden until enabled); the design
// carries z-order + extra kinds so badge layers slot in later without touching
// this contract.
class CoverLayoutModel : public QObject
{
    Q_OBJECT
    // QList<QObject*> is directly usable as a Repeater `model`; each element's
    // Q_PROPERTYs are reachable via `modelData`.
    Q_PROPERTY(QList<QObject*> layers READ layersAsObjects NOTIFY layersChanged)

public:
    explicit CoverLayoutModel(QObject* parent = nullptr);

    // Seed the default composition (the difficulty card centred + a hidden
    // chart-frame layer behind it). Idempotent — only creates missing layers.
    void ensureDefaultLayers();

    QList<QObject*> layersAsObjects() const;
    const QList<CoverLayer*>& layers() const { return layers_; }
    CoverLayer* layer(const QString& key) const;
    int indexOfKey(const QString& key) const;

    // Chart-frame layer toggle (drives its `visible`). When off, the layer stays
    // in the model but the QML delegate (and the export grab) skip it.
    Q_INVOKABLE bool chartFrameEnabled() const;
    Q_INVOKABLE void setChartFrameEnabled(bool enabled);

    // Store a freshly-rendered chart still on `key`'s layer and bump its
    // imageRevision so the QML Image reloads it from the "coverchart" provider.
    void setLayerImage(const QString& key, const QImage& image);

    // Paint-order helpers (z based, list order untouched). Bounds-checked.
    Q_INVOKABLE void bringToFront(int index);
    Q_INVOKABLE void sendToBack(int index);
    Q_INVOKABLE void raiseLayer(int index);
    Q_INVOKABLE void lowerLayer(int index);
    // Restore every layer to its kind's default geometry/visibility.
    Q_INVOKABLE void resetLayout();

    // Serialisation for future per-chart persistence / presets (not yet wired
    // into settings — see the composer handoff Phase 3).
    QJsonObject toJson() const;
    void fromJson(const QJsonObject& obj);

signals:
    void layersChanged();

private:
    CoverLayer* addLayer(const QString& key, const QString& kind, const QString& label);
    void applyDefaults(CoverLayer* layer) const;
    int minZ() const;
    int maxZ() const;

    QList<CoverLayer*> layers_;
};

}  // namespace miacode::cover_export
