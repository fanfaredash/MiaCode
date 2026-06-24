#include "tools/cover_export/CoverCompositionState.h"

#include "UiText.h"

#include <QJsonArray>

namespace miacode::cover_export {
namespace {

constexpr char kCompositionKind[] = "miacode-cover-composition";

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
    migrated.insert(QStringLiteral("version"), kCurrentVersion);
    return migrated;
}

QJsonObject CoverCompositionState::loadPreferences()
{
    const QJsonObject root = UiText::loadPreferencesObject();
    const QJsonObject app = root.value(QStringLiteral("app")).toObject();
    return app.value(QStringLiteral("cover_export")).toObject();
}

void CoverCompositionState::savePreferences(const QJsonObject& preferences)
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
    UiText::savePreferencesObject(root);
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

}  // namespace miacode::cover_export
