#include "tools/cover_export/CoverCompositionState.h"

#include "UiText.h"

#include <QJsonArray>

#include <initializer_list>
#include <cmath>
#include <QFileInfo>

namespace miacode::cover_export {
namespace {

constexpr char kCompositionKind[] = "miacode-cover-composition";

QString normalizedPresetName(QString name)
{
    return name.trimmed();
}

QJsonObject makePresetLayer(const QString& key, const QString& kind,
                            qreal nx, qreal ny, qreal sizeFraction, int z, bool visible)
{
    QJsonObject layer{{QStringLiteral("key"), key}, {QStringLiteral("kind"), kind},
                      {QStringLiteral("nx"), nx}, {QStringLiteral("ny"), ny},
                      {QStringLiteral("sizeFraction"), sizeFraction}, {QStringLiteral("z"), z},
                      {QStringLiteral("visible"), visible}, {QStringLiteral("locked"), false},
                      {QStringLiteral("opacity"), 1.0}};
    if (kind == QStringLiteral("chartFrame")) {
        layer.insert(QStringLiteral("label"), QStringLiteral("Chart frame"));
        layer.insert(QStringLiteral("frameSeconds"), 0.0);
        layer.insert(QStringLiteral("frameBgEnabled"), true);
        layer.insert(QStringLiteral("frameBgBrightness"), 0.8);
        layer.insert(QStringLiteral("frameStyle"), QString());
    } else {
        layer.insert(QStringLiteral("label"), QStringLiteral("Difficulty card"));
    }
    return layer;
}

QJsonObject makePresetComposition(std::initializer_list<QJsonObject> layers)
{
    QJsonArray array;
    for (const QJsonObject& layer : layers) array.append(layer);
    return {{QStringLiteral("kind"), QStringLiteral("miacode-cover-composition")},
            {QStringLiteral("version"), CoverCompositionState::kCurrentVersion},
            {QStringLiteral("layout"), QJsonObject{{QStringLiteral("layers"), array}}}};
}

QJsonObject migrateLayoutV1ToV2(const QJsonObject& root)
{
    QJsonObject layout = root.value(QStringLiteral("layout")).toObject();
    QJsonArray layers = layout.value(QStringLiteral("layers")).toArray();
    const QJsonObject legacyFrame = root.value(QStringLiteral("chartFrame")).toObject();
    if (layers.isEmpty() && root.contains(QStringLiteral("chartFrame"))) {
        QJsonObject card;
        card.insert(QStringLiteral("key"), QStringLiteral("card"));
        card.insert(QStringLiteral("kind"), QStringLiteral("card"));
        layers.append(card);

        QJsonObject layer;
        layer.insert(QStringLiteral("key"), QStringLiteral("chartFrame"));
        layer.insert(QStringLiteral("kind"), QStringLiteral("chartFrame"));
        layer.insert(QStringLiteral("visible"), true);
        layers.append(layer);
    }

    for (QJsonValueRef value : layers) {
        QJsonObject layer = value.toObject();
        const QString key = layer.value(QStringLiteral("key")).toString();
        if (!layer.contains(QStringLiteral("kind"))) {
            layer.insert(QStringLiteral("kind"), key == QStringLiteral("card")
                ? QStringLiteral("card")
                : QStringLiteral("chartFrame"));
        }
        if (key == QStringLiteral("chartFrame")) {
            layer.insert(QStringLiteral("frameBgEnabled"),
                         legacyFrame.value(QStringLiteral("innerBackground")).toBool(true));
            layer.insert(QStringLiteral("frameBgBrightness"),
                         legacyFrame.value(QStringLiteral("innerBrightness")).toDouble(0.8));
        }
        if (!layer.contains(QStringLiteral("opacity"))) {
            layer.insert(QStringLiteral("opacity"), 1.0);
        }
        value = layer;
    }

    layout.insert(QStringLiteral("layers"), layers);
    return layout;
}

QJsonObject migrateLayoutV2ToV3(const QJsonObject& root)
{
    QJsonObject layout = root.value(QStringLiteral("layout")).toObject();
    QJsonArray layers = layout.value(QStringLiteral("layers")).toArray();
    for (QJsonValueRef value : layers) {
        QJsonObject layer = value.toObject();
        if (layer.value(QStringLiteral("kind")).toString() == QStringLiteral("chartFrame")
            || layer.value(QStringLiteral("key")).toString().startsWith(QStringLiteral("chartFrame"))) {
            if (!layer.contains(QStringLiteral("frameBgMode"))) {
                const bool enabled = layer.value(QStringLiteral("frameBgEnabled")).toBool(true);
                layer.insert(QStringLiteral("frameBgMode"),
                             enabled ? QStringLiteral("image") : QStringLiteral("transparent"));
                if (!enabled && !layer.contains(QStringLiteral("frameBgTransparency"))) {
                    layer.insert(QStringLiteral("frameBgTransparency"), 1.0);
                }
            }
            if (!layer.contains(QStringLiteral("frameBgTransparency"))) {
                layer.insert(QStringLiteral("frameBgTransparency"), 0.5);
            }
        }
        value = layer;
    }
    layout.insert(QStringLiteral("layers"), layers);
    return layout;
}

}  // namespace

QJsonObject CoverCompositionState::toJson() const
{
    QJsonObject root;
    root.insert(QStringLiteral("kind"), QString::fromLatin1(kCompositionKind));
    root.insert(QStringLiteral("version"), kCurrentVersion);

    QJsonObject sizeObject;
    sizeObject.insert(QStringLiteral("w"), size.width());
    sizeObject.insert(QStringLiteral("h"), size.height());
    root.insert(QStringLiteral("size"), sizeObject);
    root.insert(QStringLiteral("background"), background);
    root.insert(QStringLiteral("card"), card);
    root.insert(QStringLiteral("layout"), layout);
    return root;
}

bool CoverCompositionState::fromJson(const QJsonObject& root, CoverCompositionState* out, QString* errorMessage)
{
    if (root.value(QStringLiteral("kind")).toString() != QString::fromLatin1(kCompositionKind)) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("not a MiaCode cover composition");
        }
        return false;
    }

    const QJsonObject migrated = migrateToCurrent(root);
    if (out != nullptr) {
        const QJsonObject sizeObject = migrated.value(QStringLiteral("size")).toObject();
        out->size = QSize(sizeObject.value(QStringLiteral("w")).toInt(),
                          sizeObject.value(QStringLiteral("h")).toInt());
        out->background = migrated.value(QStringLiteral("background")).toObject();
        out->card = migrated.value(QStringLiteral("card")).toObject();
        out->layout = migrated.value(QStringLiteral("layout")).toObject();
    }
    return true;
}

QJsonObject CoverCompositionState::migrateToCurrent(const QJsonObject& root)
{
    QJsonObject migrated = root;
    const int version = migrated.value(QStringLiteral("version")).toInt(1);
    if (version < 2) {
        migrated.insert(QStringLiteral("layout"), migrateLayoutV1ToV2(migrated));
        migrated.remove(QStringLiteral("chartFrame"));
    }
    if (version < 3) {
        migrated.insert(QStringLiteral("layout"), migrateLayoutV2ToV3(migrated));
    }
    migrated.insert(QStringLiteral("version"), kCurrentVersion);
    return migrated;
}

QJsonObject CoverCompositionState::loadPreferences()
{
    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject app = root.value(QStringLiteral("app")).toObject();
    return app.value(QStringLiteral("cover_export")).toObject();
}

bool CoverCompositionState::savePreferences(const QJsonObject& preferences)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QStringLiteral("app")).toObject();
    const QJsonObject existing = app.value(QStringLiteral("cover_export")).toObject();
    QJsonObject merged = preferences;
    // Preserve sibling lists that live alongside the composition but aren't part
    // of its payload — otherwise every export would wipe recent files / presets.
    for (const char* siblingKey : {"recentFiles", "presets"}) {
        const QString key = QString::fromLatin1(siblingKey);
        if (existing.contains(key) && !merged.contains(key)) {
            merged.insert(key, existing.value(key));
        }
    }
    app.insert(QStringLiteral("cover_export"), merged);
    root.insert(QStringLiteral("app"), app);
    return UiText::savePreferencesObject(root);
}

QStringList CoverCompositionState::loadRecentFiles()
{
    const QJsonObject cover = loadPreferences();
    QStringList out;
    const QJsonArray arr = cover.value(QStringLiteral("recentFiles")).toArray();
    for (const QJsonValue& value : arr) {
        const QString path = value.toString();
        if (!path.isEmpty()) {
            out.append(path);
        }
    }
    return out;
}

void CoverCompositionState::pushRecentFile(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QStringLiteral("app")).toObject();
    QJsonObject cover = app.value(QStringLiteral("cover_export")).toObject();

    QStringList list;
    list.append(trimmed);
    const QJsonArray prior = cover.value(QStringLiteral("recentFiles")).toArray();
    for (const QJsonValue& value : prior) {
        const QString p = value.toString();
        if (!p.isEmpty() && p != trimmed && list.size() < 8) {
            list.append(p);
        }
    }
    QJsonArray arr;
    for (const QString& p : list) {
        arr.append(p);
    }
    cover.insert(QStringLiteral("recentFiles"), arr);
    app.insert(QStringLiteral("cover_export"), cover);
    root.insert(QStringLiteral("app"), app);
    UiText::savePreferencesObject(root);
}

void CoverCompositionState::clearRecentFiles()
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QStringLiteral("app")).toObject();
    QJsonObject cover = app.value(QStringLiteral("cover_export")).toObject();
    cover.insert(QStringLiteral("recentFiles"), QJsonArray());
    app.insert(QStringLiteral("cover_export"), cover);
    root.insert(QStringLiteral("app"), app);
    UiText::savePreferencesObject(root);
}

QList<CoverUserPreset> CoverCompositionState::builtInPresets()
{
    const auto layer = makePresetLayer;
    return {
        {UiText::text(QStringLiteral("cover.centered_card_default")),
         makePresetComposition({layer(QStringLiteral("card"), QStringLiteral("card"), 0.5, 0.5, 0.85, 0, true)})},
        {UiText::text(QStringLiteral("cover.card_chart_frame")),
         makePresetComposition({layer(QStringLiteral("chartFrame"), QStringLiteral("chartFrame"), 0.32, 0.5, 0.82, 0, true),
                                layer(QStringLiteral("card"), QStringLiteral("card"), 0.64, 0.5, 0.78, 1, true)})},
        {UiText::text(QStringLiteral("cover.dual_chart_frame_collage")),
         makePresetComposition({layer(QStringLiteral("card"), QStringLiteral("card"), 0.5, 0.5, 0.85, 0, false),
                                layer(QStringLiteral("chartFrame"), QStringLiteral("chartFrame"), 0.30, 0.40, 0.56, 1, true),
                                layer(QStringLiteral("chartFrame2"), QStringLiteral("chartFrame"), 0.66, 0.60, 0.56, 2, true)})},
        {UiText::text(QStringLiteral("cover.pure_chart_frame")),
         makePresetComposition({layer(QStringLiteral("card"), QStringLiteral("card"), 0.5, 0.5, 0.85, 0, false),
                                layer(QStringLiteral("chartFrame"), QStringLiteral("chartFrame"), 0.5, 0.5, 0.92, 1, true)})},
    };
}

bool CoverCompositionState::prepareBatchPreset(const QJsonObject& preset,
                                                double durationSeconds,
                                                bool chartFrameAvailable,
                                                QJsonObject* prepared,
                                                QStringList* frameAdjustments,
                                                QString* errorMessage)
{
    CoverCompositionState state;
    if (!fromJson(preset, &state, errorMessage)) return false;
    const QJsonObject background = state.background;
    if (background.value(QStringLiteral("mode")).toString() == QStringLiteral("custom")
        && !QFileInfo::exists(background.value(QStringLiteral("customPath")).toString())) {
        if (errorMessage != nullptr) *errorMessage = QStringLiteral("custom cover background is missing");
        return false;
    }
    QJsonArray layers = state.layout.value(QStringLiteral("layers")).toArray();
    const double lastFrameSeconds = qMax(0.0, durationSeconds - 1.0 / 60.0);
    for (QJsonValueRef value : layers) {
        QJsonObject layer = value.toObject();
        if (layer.value(QStringLiteral("kind")).toString() != QStringLiteral("chartFrame")
            || !layer.value(QStringLiteral("visible")).toBool(true)) continue;
        if (!chartFrameAvailable || durationSeconds <= 0.0) {
            if (errorMessage != nullptr) *errorMessage = QStringLiteral("chart frame is unavailable for this difficulty");
            return false;
        }
        const double requested = layer.value(QStringLiteral("frameSeconds")).toDouble();
        if (!std::isfinite(requested)) {
            if (errorMessage != nullptr) *errorMessage = QStringLiteral("chart frame time is invalid");
            return false;
        }
        const double bounded = qBound(0.0, requested, lastFrameSeconds);
        if (frameAdjustments != nullptr && !qFuzzyCompare(requested + 1.0, bounded + 1.0)) {
            frameAdjustments->append(QStringLiteral("%1: %2s → %3s")
                .arg(layer.value(QStringLiteral("key")).toString())
                .arg(requested, 0, 'f', 2).arg(bounded, 0, 'f', 2));
        }
        layer.insert(QStringLiteral("frameSeconds"), bounded);
        value = layer;
    }
    state.layout.insert(QStringLiteral("layers"), layers);
    if (prepared != nullptr) *prepared = state.toJson();
    return true;
}

QList<CoverUserPreset> CoverCompositionState::loadUserPresets()
{
    const QJsonObject cover = loadPreferences();
    QList<CoverUserPreset> out;
    const QJsonArray arr = cover.value(QStringLiteral("presets")).toArray();
    for (const QJsonValue& value : arr) {
        const QJsonObject obj = value.toObject();
        const QString name = normalizedPresetName(obj.value(QStringLiteral("name")).toString());
        const QJsonObject composition = obj.value(QStringLiteral("composition")).toObject();
        if (!name.isEmpty() && !composition.isEmpty()) {
            out.append(CoverUserPreset{name, composition});
        }
    }
    return out;
}

void CoverCompositionState::saveUserPreset(const QString& name, const QJsonObject& composition)
{
    const QString trimmed = normalizedPresetName(name);
    if (trimmed.isEmpty() || composition.isEmpty()) {
        return;
    }

    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QStringLiteral("app")).toObject();
    QJsonObject cover = app.value(QStringLiteral("cover_export")).toObject();

    QJsonArray arr;
    QJsonObject saved;
    saved.insert(QStringLiteral("name"), trimmed);
    saved.insert(QStringLiteral("version"), 1);
    saved.insert(QStringLiteral("composition"), composition);
    arr.append(saved);

    const QJsonArray prior = cover.value(QStringLiteral("presets")).toArray();
    for (const QJsonValue& value : prior) {
        const QJsonObject obj = value.toObject();
        const QString existingName = normalizedPresetName(obj.value(QStringLiteral("name")).toString());
        if (!existingName.isEmpty() && existingName != trimmed) {
            arr.append(obj);
        }
    }

    cover.insert(QStringLiteral("presets"), arr);
    app.insert(QStringLiteral("cover_export"), cover);
    root.insert(QStringLiteral("app"), app);
    UiText::savePreferencesObject(root);
}

void CoverCompositionState::removeUserPreset(const QString& name)
{
    const QString trimmed = normalizedPresetName(name);
    if (trimmed.isEmpty()) {
        return;
    }

    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QStringLiteral("app")).toObject();
    QJsonObject cover = app.value(QStringLiteral("cover_export")).toObject();
    QJsonArray arr;
    const QJsonArray prior = cover.value(QStringLiteral("presets")).toArray();
    for (const QJsonValue& value : prior) {
        const QJsonObject obj = value.toObject();
        if (normalizedPresetName(obj.value(QStringLiteral("name")).toString()) != trimmed) {
            arr.append(obj);
        }
    }
    cover.insert(QStringLiteral("presets"), arr);
    app.insert(QStringLiteral("cover_export"), cover);
    root.insert(QStringLiteral("app"), app);
    UiText::savePreferencesObject(root);
}

void CoverCompositionState::renameUserPreset(const QString& oldName, const QString& newName)
{
    const QString oldTrimmed = normalizedPresetName(oldName);
    const QString newTrimmed = normalizedPresetName(newName);
    if (oldTrimmed.isEmpty() || newTrimmed.isEmpty()) {
        return;
    }

    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject app = root.value(QStringLiteral("app")).toObject();
    QJsonObject cover = app.value(QStringLiteral("cover_export")).toObject();
    QJsonArray arr;
    const QJsonArray prior = cover.value(QStringLiteral("presets")).toArray();
    for (const QJsonValue& value : prior) {
        QJsonObject obj = value.toObject();
        const QString existingName = normalizedPresetName(obj.value(QStringLiteral("name")).toString());
        if (existingName == oldTrimmed) {
            obj.insert(QStringLiteral("name"), newTrimmed);
            arr.append(obj);
        } else if (existingName != newTrimmed) {
            arr.append(obj);
        }
    }
    cover.insert(QStringLiteral("presets"), arr);
    app.insert(QStringLiteral("cover_export"), cover);
    root.insert(QStringLiteral("app"), app);
    UiText::savePreferencesObject(root);
}

}  // namespace miacode::cover_export
