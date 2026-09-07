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
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity NOTIFY opacityChanged)
    Q_PROPERTY(double frameSeconds READ frameSeconds WRITE setFrameSeconds NOTIFY frameSecondsChanged)
    Q_PROPERTY(bool frameBgEnabled READ frameBgEnabled WRITE setFrameBgEnabled NOTIFY frameBgEnabledChanged)
    Q_PROPERTY(QString frameBgMode READ frameBgMode WRITE setFrameBgMode NOTIFY frameBgModeChanged)
    Q_PROPERTY(qreal frameBgBrightness READ frameBgBrightness WRITE setFrameBgBrightness NOTIFY frameBgBrightnessChanged)
    Q_PROPERTY(qreal frameBgTransparency READ frameBgTransparency WRITE setFrameBgTransparency NOTIFY frameBgTransparencyChanged)
    Q_PROPERTY(QString frameStyle READ frameStyle WRITE setFrameStyle NOTIFY frameStyleChanged)
    // -1 = no still rendered yet (QML shows nothing); >= 0 bumps on each new grab.
    Q_PROPERTY(int imageRevision READ imageRevision NOTIFY imageRevisionChanged)
    // ---- kind=="image" (custom image layer) ----
    // Absolute path to the source image; empty shows nothing.
    Q_PROPERTY(QString imagePath READ imagePath WRITE setImagePath NOTIFY imagePathChanged)
    // ---- kind=="text" (custom text layer) ----
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)
    // Absolute path to a custom font; empty == the bundled default (Heavy).
    Q_PROPERTY(QString fontPath READ fontPath WRITE setFontPath NOTIFY fontPathChanged)
    Q_PROPERTY(QString textColor READ textColor WRITE setTextColor NOTIFY textColorChanged)
    Q_PROPERTY(bool textBold READ textBold WRITE setTextBold NOTIFY textBoldChanged)
    // Content width / height. For images it is the intrinsic aspect (read from the
    // file in C++). For text it is written back from QML once the glyphs are laid
    // out. Drives the layer's box width (= box height × contentAspect) so image/text
    // layers keep their true proportions across preview and export.
    Q_PROPERTY(qreal contentAspect READ contentAspect WRITE setContentAspect NOTIFY contentAspectChanged)

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
    qreal opacity() const { return opacity_; }
    int imageRevision() const { return imageRevision_; }
    QImage frameImage() const { return frameImage_; }
    double frameSeconds() const { return frameSeconds_; }
    bool frameBgEnabled() const { return frameBgEnabled_; }
    QString frameBgMode() const { return frameBgMode_; }
    qreal frameBgBrightness() const { return frameBgBrightness_; }
    qreal frameBgTransparency() const { return frameBgTransparency_; }
    QString frameStyle() const { return frameStyle_; }
    QString imagePath() const { return imagePath_; }
    QString text() const { return text_; }
    QString fontPath() const { return fontPath_; }
    QString textColor() const { return textColor_; }
    bool textBold() const { return textBold_; }
    qreal contentAspect() const { return contentAspect_; }

    void setNx(qreal v);
    void setNy(qreal v);
    void setSizeFraction(qreal v);
    void setZ(int v);
    void setVisible(bool v);
    void setLocked(bool v);
    void setOpacity(qreal v);
    void setFrameSeconds(double v);
    void setFrameBgEnabled(bool v);
    void setFrameBgMode(const QString& v);
    void setFrameBgBrightness(qreal v);
    void setFrameBgTransparency(qreal v);
    void setFrameStyle(const QString& v);
    // Sets the path and, when the file exists, refreshes contentAspect from its
    // intrinsic size (via QImageReader) so the layer box matches the image.
    void setImagePath(const QString& v);
    void setText(const QString& v);
    void setFontPath(const QString& v);
    void setTextColor(const QString& v);
    void setTextBold(bool v);
    void setContentAspect(qreal v);

signals:
    void nxChanged();
    void nyChanged();
    void sizeFractionChanged();
    void zChanged();
    void visibleChanged();
    void lockedChanged();
    void opacityChanged();
    void frameSecondsChanged();
    void frameBgEnabledChanged();
    void frameBgModeChanged();
    void frameBgBrightnessChanged();
    void frameBgTransparencyChanged();
    void frameStyleChanged();
    void imageRevisionChanged();
    void imagePathChanged();
    void textChanged();
    void fontPathChanged();
    void textColorChanged();
    void textBoldChanged();
    void contentAspectChanged();

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
    qreal opacity_ = 1.0;
    QImage frameImage_;
    int imageRevision_ = -1;
    double frameSeconds_ = 0.0;
    bool frameBgEnabled_ = true;
    QString frameBgMode_ = QStringLiteral("image");
    qreal frameBgBrightness_ = 0.8;
    qreal frameBgTransparency_ = 0.5;
    QString frameStyle_;
    QString imagePath_;
    QString text_;
    QString fontPath_;
    QString textColor_ = QStringLiteral("#FFFFFF");
    bool textBold_ = false;
    qreal contentAspect_ = 1.0;
};

// Ordered set of composition layers, exposed to QML. Seeds the stable difficulty
// card and adds chart-frame layers on demand; the design carries z-order + extra
// kinds so badge layers slot in later without touching this contract.
class CoverLayoutModel : public QObject
{
    Q_OBJECT
    // QList<QObject*> is directly usable as a Repeater `model`; each element's
    // Q_PROPERTYs are reachable via `modelData`.
    Q_PROPERTY(QList<QObject*> layers READ layersAsObjects NOTIFY layersChanged)

public:
    explicit CoverLayoutModel(QObject* parent = nullptr);

    // Seed the default composition (the centred difficulty card). Idempotent:
    // only creates missing layers.
    void ensureDefaultLayers();

    QList<QObject*> layersAsObjects() const;
    const QList<CoverLayer*>& layers() const { return layers_; }
    CoverLayer* layer(const QString& key) const;
    int indexOfKey(const QString& key) const;
    static QString cardKey();
    static QString legacyChartFrameKey();

    // Legacy single-frame compatibility API. When enabled and no legacy frame
    // exists, this creates a chart-frame layer; when disabled it hides that layer.
    Q_INVOKABLE bool chartFrameEnabled() const;
    Q_INVOKABLE void setChartFrameEnabled(bool enabled);
    CoverLayer* addChartFrameLayer(double frameSeconds = 0.0);
    // Add a custom-image layer for `imagePath` (its intrinsic aspect is read into
    // the layer), or a custom-text layer seeded with `text`. Both are placed with
    // the normal new-layer geometry, made visible, and raised to the front.
    CoverLayer* addImageLayer(const QString& imagePath);
    CoverLayer* addTextLayer(const QString& text);
    // Add a chart-frame layer whose non-position properties are copied from
    // `source`, while geometry keeps the normal new-frame placement. Used by
    // the studio's "add frame" command to inherit the user's last frame setup.
    CoverLayer* addChartFrameLayerFromTemplate(const CoverLayer* source, double fallbackFrameSeconds = 0.0);
    CoverLayer* duplicateLayer(const QString& key);
    bool removeLayer(const QString& key);
    // Key to select after removing `key`, in the layer LIST's visible order
    // (z descending, row 0 = front): the next item below, else the previous,
    // else the stable card. Drives "delete then auto-select neighbour" so the
    // delete key can be held to clear layers. Computed BEFORE the removal.
    QString selectionAfterRemoval(const QString& key) const;
    QList<CoverLayer*> chartFrameLayers() const;
    QList<CoverLayer*> visibleChartFrameLayers() const;
    bool moveLayerBefore(const QString& key, const QString& beforeKey);
    bool moveLayerAfter(const QString& key, const QString& afterKey);
    // Reorder by the layer LIST's VISIBLE rows (z descending, row 0 = front). Used
    // by the list's drag-to-reorder; handles the view↔z reversal (§12.12) here so
    // callers pass plain view-row indices.
    void moveByViewRows(int fromRow, int toRow);
    void normalizeZOrder();

    // Store a freshly-rendered chart still on `key`'s layer and bump its
    // imageRevision so the QML Image reloads it from the "coverchart" provider.
    void setLayerImage(const QString& key, const QImage& image);
    // Drop a still when the renderer/difficulty changes. Reusing a layer object
    // must not make a frame from the previous chart look current.
    void clearLayerImage(const QString& key);

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
    CoverLayer* addLayerFromJson(const QJsonObject& obj);
    void applyDefaults(CoverLayer* layer) const;
    QString nextChartFrameKey() const;
    // First unused key of the form "<prefix>", "<prefix>2", "<prefix>3", …
    QString nextKeyWithPrefix(const QString& prefix) const;
    void assignZOrderFromList();
    int minZ() const;
    int maxZ() const;

    QList<CoverLayer*> layers_;
};

}  // namespace miacode::cover_export
