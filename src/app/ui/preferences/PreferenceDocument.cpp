#include "preferences/PreferenceDocument.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QIODevice>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace {

// v4 (2026-06-19): re-runs first-run onboarding so existing users see the
// welcome dialog's new IME-block choice. Bump this whenever onboarding gains a
// setting whose default should be re-confirmed by users with stored prefs.
constexpr auto kPreferencesSchema = "miacode_preferences_v4";
constexpr auto kUiSectionKey = "ui";
constexpr auto kAppSectionKey = "app";
constexpr auto kPreviewSectionKey = "preview";
constexpr auto kLanguageKey = "language";
constexpr auto kThemeKey = "theme";

QString preferencesPath()
{
    const QString configRoot = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configRoot.isEmpty()) {
        return QString();
    }
    const QDir configDir(configRoot);
    return configDir.filePath("preferences.json");
}

QString legacyPreferencesFilePath()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(".miacode_preferences.json");
}

QJsonObject loadJsonObjectFromFile(const QString& path)
{
    QFile file(path);
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return QJsonObject();
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        return QJsonObject();
    }
    return doc.object();
}

bool saveJsonObjectToFile(const QString& path, const QJsonObject& root)
{
    if (path.isEmpty()) {
        return false;
    }

    const QFileInfo fileInfo(path);
    const QDir parentDir = fileInfo.dir();
    if (!parentDir.exists() && !QDir().mkpath(parentDir.absolutePath())) {
        return false;
    }

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (file.write(payload) != payload.size()) {
        return false;
    }
    return file.commit();
}

QJsonObject normalizedPreferencesRoot(const QJsonObject& raw)
{
    QJsonObject normalized = raw;
    normalized.remove(kUiSectionKey);
    normalized.remove(kAppSectionKey);
    normalized.remove(QStringLiteral("extensions"));
    normalized.insert("schema", kPreferencesSchema);

    QJsonObject ui = raw.value(kUiSectionKey).toObject();
    if (!ui.contains(kLanguageKey) && raw.contains("ui_language")) {
        ui.insert(kLanguageKey, raw.value("ui_language").toString("system"));
    }
    if (!ui.contains(kLanguageKey)) {
        ui.insert(kLanguageKey, "system");
    }
    if (!ui.contains(kThemeKey) && raw.contains("ui_theme")) {
        ui.insert(kThemeKey, raw.value("ui_theme").toString("system"));
    }
    if (!ui.contains(kThemeKey) && raw.contains("theme")) {
        ui.insert(kThemeKey, raw.value("theme").toString("system"));
    }
    if (!ui.contains(kThemeKey)) {
        // Default theme for fresh installs is dark (user-voted default). Existing
        // users keep whatever they explicitly stored (incl. "system"/"light").
        ui.insert(kThemeKey, "dark");
    }
    normalized.insert(kUiSectionKey, ui);

    QJsonObject app = raw.value(kAppSectionKey).toObject();
    if (!app.contains("last_open_dir") && raw.contains("last_open_dir")) {
        app.insert("last_open_dir", raw.value("last_open_dir").toString());
    }
    if (!app.contains("last_track_path") && raw.contains("last_track_path")) {
        app.insert("last_track_path", raw.value("last_track_path").toString());
    }
    if (!app.contains("show_slide_tracks") && raw.contains("show_slide_tracks")) {
        app.insert("show_slide_tracks", raw.value("show_slide_tracks").toBool(true));
    }

    QJsonObject preview = app.value(kPreviewSectionKey).toObject();
    if (!preview.contains("show_judge_markers") && raw.contains("show_judge_markers")) {
        preview.insert("show_judge_markers", raw.value("show_judge_markers").toBool(false));
    }
    if (!preview.contains("show_touch_trail") && raw.contains("show_touch_trail")) {
        preview.insert("show_touch_trail", raw.value("show_touch_trail").toBool(false));
    }
    if (!preview.contains("background_brightness") && raw.contains("preview_background_brightness")) {
        preview.insert("background_brightness", raw.value("preview_background_brightness").toDouble(0.2));
    }
    if (!preview.contains("show_debug_info") && raw.contains("preview_show_debug_info")) {
        preview.insert("show_debug_info", raw.value("preview_show_debug_info").toBool(false));
    }
    QJsonObject audio = preview.value("audio").toObject();
    const bool canonicalTouchVolumePresent = audio.contains("touch_volume");
    if (raw.contains("preview_audio") && raw.value("preview_audio").isObject()) {
        const QJsonObject legacyAudio = raw.value("preview_audio").toObject();
        for (auto it = legacyAudio.constBegin(); it != legacyAudio.constEnd(); ++it) {
            if (!audio.contains(it.key())) {
                audio.insert(it.key(), it.value());
            }
        }
    }
    if (raw.contains("master_volume")
        || raw.contains("master_restore_volume")
        || raw.contains("bgm_volume")
        || raw.contains("sfx_volume")
        || raw.contains("answer_volume")
        || raw.contains("judge_volume")
        || raw.contains("ex_volume")
        || raw.contains("break_volume")
        || raw.contains("slide_volume")
        || raw.contains("touch_volume")
        || raw.contains("touchhold_volume")
        || raw.contains("break_slide_volume")
        || raw.contains("break_slide_restore_volume")
        || raw.contains("break_slide_tail_cheer_muted")
        || raw.contains("firework_volume")
        || raw.contains("hanabi_volume")) {
        if (!audio.contains("global_volume") && raw.contains("master_volume")) {
            audio.insert("global_volume", raw.value("master_volume").toDouble());
        }
        if (!audio.contains("global_restore_volume") && raw.contains("master_restore_volume")) {
            audio.insert("global_restore_volume", raw.value("master_restore_volume").toDouble());
        }
        if (raw.contains("bgm_volume")) {
            if (!audio.contains("track_volume")) {
                audio.insert("track_volume", raw.value("bgm_volume").toDouble());
            }
            if (!audio.contains("bgm_volume")) {
                audio.insert("bgm_volume", raw.value("bgm_volume").toDouble());
            }
        }
        if (!audio.contains("answer_volume") && raw.contains("answer_volume")) {
            audio.insert("answer_volume", raw.value("answer_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("judge_volume")) {
            if (!audio.contains("tap_volume")) {
                audio.insert("tap_volume", raw.value("judge_volume").toDouble(raw.value("sfx_volume").toDouble()));
            }
            if (!audio.contains("judge_volume")) {
                audio.insert("judge_volume", raw.value("judge_volume").toDouble(raw.value("sfx_volume").toDouble()));
            }
        }
        if (!audio.contains("slide_volume") && raw.contains("slide_volume")) {
            audio.insert("slide_volume", raw.value("slide_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (!audio.contains("break_volume") && raw.contains("break_volume")) {
            audio.insert("break_volume", raw.value("break_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (!audio.contains("break_slide_volume") && raw.contains("break_slide_volume")) {
            audio.insert(
                "break_slide_volume",
                raw.value("break_slide_volume").toDouble(
                    raw.value("slide_volume").toDouble(raw.value("sfx_volume").toDouble())));
        }
        if (!audio.contains("break_slide_restore_volume") && raw.contains("break_slide_restore_volume")) {
            audio.insert(
                "break_slide_restore_volume",
                raw.value("break_slide_restore_volume").toDouble(
                    raw.value("break_slide_volume").toDouble(
                        raw.value("slide_volume").toDouble(raw.value("sfx_volume").toDouble()))));
        }
        if (!audio.contains("break_slide_tail_cheer_muted") && raw.contains("break_slide_tail_cheer_muted")) {
            audio.insert("break_slide_tail_cheer_muted", raw.value("break_slide_tail_cheer_muted").toBool(false));
        }
        if (!audio.contains("ex_volume") && raw.contains("ex_volume")) {
            audio.insert("ex_volume", raw.value("ex_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (!audio.contains("touch_volume") && raw.contains("touch_volume")) {
            audio.insert("touch_volume", raw.value("touch_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("touchhold_volume")) {
            const double touchHoldVolume = raw.value("touchhold_volume").toDouble(raw.value("sfx_volume").toDouble());
            if (!audio.contains("touchhold_volume")) {
                audio.insert("touchhold_volume", touchHoldVolume);
            }
            if (!canonicalTouchVolumePresent) {
                audio.insert("touch_volume", qMax(audio.value("touch_volume").toDouble(), touchHoldVolume));
            }
        }
        if (!audio.contains("firework_volume") && raw.contains("firework_volume")) {
            audio.insert("firework_volume", raw.value("firework_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (!audio.contains("firework_volume") && raw.contains("hanabi_volume")) {
            audio.insert("firework_volume", raw.value("hanabi_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
    }
    if (!audio.isEmpty()) {
        preview.insert("audio", audio);
    }

    if (!preview.isEmpty()) {
        app.insert(kPreviewSectionKey, preview);
    }
    normalized.insert(kAppSectionKey, app);

    static const char* const legacyKeys[] = {
        "ui_language", "ui_theme", "theme", "last_open_dir", "last_track_path",
        "show_slide_tracks", "show_judge_markers", "show_touch_trail",
        "preview_background_brightness", "preview_show_debug_info", "preview_audio",
        "master_volume", "master_restore_volume", "bgm_volume", "sfx_volume",
        "answer_volume", "judge_volume", "ex_volume", "break_volume",
        "slide_volume", "touch_volume", "touchhold_volume", "break_slide_volume",
        "break_slide_restore_volume", "break_slide_tail_cheer_muted",
        "firework_volume", "hanabi_volume",
    };
    for (const char* key : legacyKeys) {
        normalized.remove(QString::fromLatin1(key));
    }
    return normalized;
}

QString normalizedLanguageToken(QString token)
{
    token = token.trimmed().toLower();
    token.replace('-', '_');
    return token;
}

PreferenceDocument::LanguagePreference parseLanguagePreference(const QString& raw)
{
    const QString token = normalizedLanguageToken(raw);
    if (token == "zh" || token == "zh_cn" || token == "zh_hans" || token == "zh_hans_cn" || token == "cn") {
        return PreferenceDocument::LanguagePreference::Chinese;
    }
    if (token == "ja" || token == "ja_jp" || token == "jp") {
        return PreferenceDocument::LanguagePreference::Japanese;
    }
    if (token == "en" || token == "en_us" || token == "en_gb") {
        return PreferenceDocument::LanguagePreference::English;
    }
    return PreferenceDocument::LanguagePreference::System;
}

QString languagePreferenceToken(PreferenceDocument::LanguagePreference preference)
{
    switch (preference) {
    case PreferenceDocument::LanguagePreference::English:
        return "en";
    case PreferenceDocument::LanguagePreference::Chinese:
        return "zh";
    case PreferenceDocument::LanguagePreference::Japanese:
        return "ja";
    case PreferenceDocument::LanguagePreference::System:
    default:
        return "system";
    }
}

QString themePreferenceToken(PreferenceDocument::ThemePreference preference)
{
    switch (preference) {
    case PreferenceDocument::ThemePreference::Light:
        return "light";
    case PreferenceDocument::ThemePreference::Dark:
        return "dark";
    case PreferenceDocument::ThemePreference::System:
    default:
        return "system";
    }
}

PreferenceDocument::LanguagePreference loadStoredLanguagePreference()
{
    const bool hasMergedPreferences = QFile::exists(preferencesPath());
    const QJsonObject root = PreferenceDocument::loadPreferencesObject();
    if (!hasMergedPreferences) {
        PreferenceDocument::savePreferencesObject(root);
    }

    return parseLanguagePreference(root.value(kUiSectionKey).toObject().value(kLanguageKey).toString("system"));
}

PreferenceDocument::ThemePreference loadStoredThemePreference()
{
    const bool hasMergedPreferences = QFile::exists(preferencesPath());
    const QJsonObject root = PreferenceDocument::loadPreferencesObject();
    if (!hasMergedPreferences) {
        PreferenceDocument::savePreferencesObject(root);
    }

    const QString raw = root.value(kUiSectionKey).toObject().value(kThemeKey).toString("system").trimmed().toLower();
    if (raw == "light") {
        return PreferenceDocument::ThemePreference::Light;
    }
    if (raw == "dark") {
        return PreferenceDocument::ThemePreference::Dark;
    }
    return PreferenceDocument::ThemePreference::System;
}

void saveStoredLanguagePreference(PreferenceDocument::LanguagePreference preference)
{
    QJsonObject root = PreferenceDocument::loadPreferencesObject();
    QJsonObject ui = root.value(kUiSectionKey).toObject();
    ui.insert(kLanguageKey, languagePreferenceToken(preference));
    root.insert(kUiSectionKey, ui);
    root.insert("schema", kPreferencesSchema);
    PreferenceDocument::savePreferencesObject(root);
}

void saveStoredThemePreference(PreferenceDocument::ThemePreference preference)
{
    QJsonObject root = PreferenceDocument::loadPreferencesObject();
    QJsonObject ui = root.value(kUiSectionKey).toObject();
    ui.insert(kThemeKey, themePreferenceToken(preference));
    root.insert(kUiSectionKey, ui);
    root.insert("schema", kPreferencesSchema);
    PreferenceDocument::savePreferencesObject(root);
}

PreferenceDocument::LanguagePreference languageListPreference(const QStringList& languages)
{
    for (const QString& language : languages) {
        const QString token = normalizedLanguageToken(language);
        if (token.startsWith("zh")) {
            return PreferenceDocument::LanguagePreference::Chinese;
        }
        if (token.startsWith("ja") || token == "jp") {
            return PreferenceDocument::LanguagePreference::Japanese;
        }
        if (token.startsWith("en")) {
            return PreferenceDocument::LanguagePreference::English;
        }
    }
    return PreferenceDocument::LanguagePreference::System;
}

PreferenceDocument::LanguagePreference resolvedLanguagePreference()
{
    const QByteArray env = qgetenv("MIACODE_LANG").trimmed();
    if (!env.isEmpty()) {
        const PreferenceDocument::LanguagePreference envPreference = parseLanguagePreference(QString::fromUtf8(env));
        if (envPreference != PreferenceDocument::LanguagePreference::System) {
            return envPreference;
        }
    }

    const PreferenceDocument::LanguagePreference storedPreference = PreferenceDocument::preferredLanguage();
    if (storedPreference != PreferenceDocument::LanguagePreference::System) {
        return storedPreference;
    }

    const QStringList uiLanguages = QLocale::system().uiLanguages();
    const PreferenceDocument::LanguagePreference uiLanguagePreference = languageListPreference(uiLanguages);
    if (uiLanguagePreference != PreferenceDocument::LanguagePreference::System) {
        return uiLanguagePreference;
    }

    const QString localeName = normalizedLanguageToken(QLocale::system().name());
    if (localeName.startsWith("zh")) {
        return PreferenceDocument::LanguagePreference::Chinese;
    }
    if (localeName.startsWith("ja") || localeName == "jp") {
        return PreferenceDocument::LanguagePreference::Japanese;
    }
    return PreferenceDocument::LanguagePreference::English;
}

bool isBuiltInLanguageToken(const QString& raw)
{
    const QString token = normalizedLanguageToken(raw);
    return token == "system" || token == "en" || token == "en_us" || token == "en_gb"
        || token == "zh" || token == "zh_cn" || token == "zh_hans" || token == "zh_hans_cn" || token == "cn"
        || token == "ja" || token == "ja_jp" || token == "jp";
}

QString builtInLanguageToken(const QString& raw)
{
    const QString token = normalizedLanguageToken(raw);
    if (token == "en_us" || token == "en_gb") {
        return QStringLiteral("en");
    }
    if (token == "zh_cn" || token == "zh_hans" || token == "zh_hans_cn" || token == "cn") {
        return QStringLiteral("zh");
    }
    if (token == "ja_jp" || token == "jp") {
        return QStringLiteral("ja");
    }
    if (token == "en" || token == "zh" || token == "ja") {
        return token;
    }
    return QStringLiteral("system");
}

QString resolvedLanguageTokenFromStorage()
{
    const QByteArray env = qgetenv("MIACODE_LANG").trimmed();
    if (!env.isEmpty()) {
        const QString envToken = normalizedLanguageToken(QString::fromUtf8(env));
        if (isBuiltInLanguageToken(envToken)) {
            return builtInLanguageToken(envToken);
        }
    }

    const QString storedToken = normalizedLanguageToken(
        PreferenceDocument::loadPreferencesObject().value(QString::fromLatin1(kUiSectionKey)).toObject()
            .value(QString::fromLatin1(kLanguageKey)).toString(QStringLiteral("system")));
    if (isBuiltInLanguageToken(storedToken)) {
        const QString normalized = builtInLanguageToken(storedToken);
        if (normalized != QStringLiteral("system")) {
            return normalized;
        }
    }

    const PreferenceDocument::LanguagePreference resolved = resolvedLanguagePreference();
    switch (resolved) {
    case PreferenceDocument::LanguagePreference::Chinese:
        return QStringLiteral("zh");
    case PreferenceDocument::LanguagePreference::Japanese:
        return QStringLiteral("ja");
    case PreferenceDocument::LanguagePreference::English:
    case PreferenceDocument::LanguagePreference::System:
    default:
        return QStringLiteral("en");
    }
}

PreferenceDocument::LanguagePreference& preferredLanguageStorage()
{
    static PreferenceDocument::LanguagePreference preference = loadStoredLanguagePreference();
    return preference;
}

PreferenceDocument::ThemePreference& preferredThemeStorage()
{
    static PreferenceDocument::ThemePreference preference = loadStoredThemePreference();
    return preference;
}

}  // namespace

PreferenceDocument::LanguagePreference PreferenceDocument::preferredLanguage()
{
    return preferredLanguageStorage();
}

void PreferenceDocument::setPreferredLanguage(LanguagePreference preference)
{
    preferredLanguageStorage() = preference;
    saveStoredLanguagePreference(preference);
}
QString PreferenceDocument::preferredLanguageToken()
{
    const QJsonObject root = loadPreferencesObject();
    return normalizedLanguageToken(
        root.value(QString::fromLatin1(kUiSectionKey)).toObject()
            .value(QString::fromLatin1(kLanguageKey)).toString(QStringLiteral("system")));
}

void PreferenceDocument::setPreferredLanguageToken(const QString& token)
{
    const QString normalized = normalizedLanguageToken(token);
    QJsonObject root = loadPreferencesObject();
    QJsonObject ui = root.value(QString::fromLatin1(kUiSectionKey)).toObject();
    ui.insert(QString::fromLatin1(kLanguageKey), normalized.isEmpty() ? QStringLiteral("system") : normalized);
    root.insert(QString::fromLatin1(kUiSectionKey), ui);
    root.insert(QStringLiteral("schema"), QString::fromLatin1(kPreferencesSchema));
    savePreferencesObject(root);
    preferredLanguageStorage() = parseLanguagePreference(preferredLanguageToken());
}

QString PreferenceDocument::resolvedLanguageToken()
{
    return resolvedLanguageTokenFromStorage();
}

QVector<PreferenceDocument::LanguageOption> PreferenceDocument::availableLanguageOptions()
{
    return {
        {QStringLiteral("system"), qtTrId("dialog.preferences.language.system"), true},
        {QStringLiteral("en"), qtTrId("dialog.preferences.language.english"), true},
        {QStringLiteral("zh"), qtTrId("dialog.preferences.language.chinese"), true},
        {QStringLiteral("ja"), qtTrId("dialog.preferences.language.japanese"), true},
    };
}

bool PreferenceDocument::isLanguageAvailable(const QString& token)
{
    const QString normalized = normalizedLanguageToken(token);
    if (isBuiltInLanguageToken(normalized)) {
        return true;
    }
    return false;
}

bool PreferenceDocument::ensurePreferredLanguageAvailable()
{
    const QString token = preferredLanguageToken();
    if (token.isEmpty() || isLanguageAvailable(token)) {
        return false;
    }
    setPreferredLanguageToken(QStringLiteral("system"));
    return true;
}

PreferenceDocument::ThemePreference PreferenceDocument::preferredTheme()
{
    return preferredThemeStorage();
}

void PreferenceDocument::setPreferredTheme(ThemePreference preference)
{
    preferredThemeStorage() = preference;
    saveStoredThemePreference(preference);
}

PreferenceDocument::LanguagePreference PreferenceDocument::resolvedLanguage()
{
    return resolvedLanguagePreference();
}


QString PreferenceDocument::preferencesFilePath()
{
    return preferencesPath();
}

QString PreferenceDocument::currentPreferencesSchema()
{
    return QString::fromLatin1(kPreferencesSchema);
}

QString PreferenceDocument::storedPreferencesSchema()
{
    // Raw read mirroring loadPreferencesObject()'s file precedence (primary then
    // legacy), but WITHOUT normalizedPreferencesRoot() — that helper always
    // injects the current token, which would hide an outdated on-disk schema and
    // defeat the upgrade-detection gate in main(). An empty string (no readable
    // preferences file) compares unequal to the current token, so callers treat
    // it as "needs onboarding".
    QJsonObject root = loadJsonObjectFromFile(preferencesPath());
    if (root.isEmpty()) {
        root = loadJsonObjectFromFile(legacyPreferencesFilePath());
    }
    return root.value(QStringLiteral("schema")).toString();
}

QJsonObject PreferenceDocument::loadPreferencesObject()
{
    const QJsonObject primary = loadJsonObjectFromFile(preferencesPath());
    if (!primary.isEmpty()) {
        return normalizedPreferencesRoot(primary);
    }
    const QJsonObject legacy = loadJsonObjectFromFile(legacyPreferencesFilePath());
    if (!legacy.isEmpty()) {
        return normalizedPreferencesRoot(legacy);
    }
    return normalizedPreferencesRoot(QJsonObject());
}

bool PreferenceDocument::savePreferencesObject(const QJsonObject& root)
{
    return saveJsonObjectToFile(preferencesPath(), normalizedPreferencesRoot(root));
}

QJsonObject PreferenceDocument::normalizePreferencesObject(const QJsonObject& root)
{
    return normalizedPreferencesRoot(root);
}

QString PreferenceDocument::themeTokenFromPreferencesObject(const QJsonObject& root)
{
    return root.value(QStringLiteral("ui")).toObject()
        .value(QStringLiteral("theme")).toString(QStringLiteral("system"));
}

void PreferenceDocument::setThemeTokenInPreferencesObject(QJsonObject* root, const QString& token)
{
    if (root == nullptr) {
        return;
    }
    QJsonObject ui = root->value(QStringLiteral("ui")).toObject();
    ui.insert(QStringLiteral("theme"), token);
    root->insert(QStringLiteral("ui"), ui);
    root->remove(QStringLiteral("theme"));
}
