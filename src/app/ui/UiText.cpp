#include "UiText.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QPair>
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

UiText::LanguagePreference parseLanguagePreference(const QString& raw)
{
    const QString token = normalizedLanguageToken(raw);
    if (token == "zh" || token == "zh_cn" || token == "zh_hans" || token == "zh_hans_cn" || token == "cn") {
        return UiText::LanguagePreference::Chinese;
    }
    if (token == "ja" || token == "ja_jp" || token == "jp") {
        return UiText::LanguagePreference::Japanese;
    }
    if (token == "en" || token == "en_us" || token == "en_gb") {
        return UiText::LanguagePreference::English;
    }
    return UiText::LanguagePreference::System;
}

QString languagePreferenceToken(UiText::LanguagePreference preference)
{
    switch (preference) {
    case UiText::LanguagePreference::English:
        return "en";
    case UiText::LanguagePreference::Chinese:
        return "zh";
    case UiText::LanguagePreference::Japanese:
        return "ja";
    case UiText::LanguagePreference::System:
    default:
        return "system";
    }
}

QString themePreferenceToken(UiText::ThemePreference preference)
{
    switch (preference) {
    case UiText::ThemePreference::Light:
        return "light";
    case UiText::ThemePreference::Dark:
        return "dark";
    case UiText::ThemePreference::System:
    default:
        return "system";
    }
}

UiText::LanguagePreference loadStoredLanguagePreference()
{
    const bool hasMergedPreferences = QFile::exists(preferencesPath());
    const QJsonObject root = UiText::loadPreferencesObject();
    if (!hasMergedPreferences) {
        UiText::savePreferencesObject(root);
    }

    return parseLanguagePreference(root.value(kUiSectionKey).toObject().value(kLanguageKey).toString("system"));
}

UiText::ThemePreference loadStoredThemePreference()
{
    const bool hasMergedPreferences = QFile::exists(preferencesPath());
    const QJsonObject root = UiText::loadPreferencesObject();
    if (!hasMergedPreferences) {
        UiText::savePreferencesObject(root);
    }

    const QString raw = root.value(kUiSectionKey).toObject().value(kThemeKey).toString("system").trimmed().toLower();
    if (raw == "light") {
        return UiText::ThemePreference::Light;
    }
    if (raw == "dark") {
        return UiText::ThemePreference::Dark;
    }
    return UiText::ThemePreference::System;
}

void saveStoredLanguagePreference(UiText::LanguagePreference preference)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value(kUiSectionKey).toObject();
    ui.insert(kLanguageKey, languagePreferenceToken(preference));
    root.insert(kUiSectionKey, ui);
    root.insert("schema", kPreferencesSchema);
    UiText::savePreferencesObject(root);
}

void saveStoredThemePreference(UiText::ThemePreference preference)
{
    QJsonObject root = UiText::loadPreferencesObject();
    QJsonObject ui = root.value(kUiSectionKey).toObject();
    ui.insert(kThemeKey, themePreferenceToken(preference));
    root.insert(kUiSectionKey, ui);
    root.insert("schema", kPreferencesSchema);
    UiText::savePreferencesObject(root);
}

UiText::LanguagePreference languageListPreference(const QStringList& languages)
{
    for (const QString& language : languages) {
        const QString token = normalizedLanguageToken(language);
        if (token.startsWith("zh")) {
            return UiText::LanguagePreference::Chinese;
        }
        if (token.startsWith("ja") || token == "jp") {
            return UiText::LanguagePreference::Japanese;
        }
        if (token.startsWith("en")) {
            return UiText::LanguagePreference::English;
        }
    }
    return UiText::LanguagePreference::System;
}

UiText::LanguagePreference resolvedLanguagePreference()
{
    const QByteArray env = qgetenv("MIACODE_LANG").trimmed();
    if (!env.isEmpty()) {
        const UiText::LanguagePreference envPreference = parseLanguagePreference(QString::fromUtf8(env));
        if (envPreference != UiText::LanguagePreference::System) {
            return envPreference;
        }
    }

    const UiText::LanguagePreference storedPreference = UiText::preferredLanguage();
    if (storedPreference != UiText::LanguagePreference::System) {
        return storedPreference;
    }

    const QStringList uiLanguages = QLocale::system().uiLanguages();
    const UiText::LanguagePreference uiLanguagePreference = languageListPreference(uiLanguages);
    if (uiLanguagePreference != UiText::LanguagePreference::System) {
        return uiLanguagePreference;
    }

    const QString localeName = normalizedLanguageToken(QLocale::system().name());
    if (localeName.startsWith("zh")) {
        return UiText::LanguagePreference::Chinese;
    }
    if (localeName.startsWith("ja") || localeName == "jp") {
        return UiText::LanguagePreference::Japanese;
    }
    return UiText::LanguagePreference::English;
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
        UiText::loadPreferencesObject().value(QString::fromLatin1(kUiSectionKey)).toObject()
            .value(QString::fromLatin1(kLanguageKey)).toString(QStringLiteral("system")));
    if (isBuiltInLanguageToken(storedToken)) {
        const QString normalized = builtInLanguageToken(storedToken);
        if (normalized != QStringLiteral("system")) {
            return normalized;
        }
    }

    const UiText::LanguagePreference resolved = resolvedLanguagePreference();
    switch (resolved) {
    case UiText::LanguagePreference::Chinese:
        return QStringLiteral("zh");
    case UiText::LanguagePreference::Japanese:
        return QStringLiteral("ja");
    case UiText::LanguagePreference::English:
    case UiText::LanguagePreference::System:
    default:
        return QStringLiteral("en");
    }
}

QString mapValue(const QHash<QString, QString>& map, const QString& key)
{
    const auto it = map.constFind(key);
    if (it == map.constEnd()) {
        return QString();
    }
    return it.value();
}

const QHash<QString, QString>& enMap()
{
    static const QHash<QString, QString> map{
        {"menu.file", "File(&F)"},
        {"menu.tools", "Tools(&T)"},
        {"menu.transform", "Modify(&M)"},
        {"menu.help", "Help(&H)"},
        {"menu.bpm_latency", "BPM && Latency"},
        {"menu.clear_elements", "Clear Elements"},
        {"menu.export_as_zip", "Export as ZIP..."},
        {"menu.find_replace", "Find/Replace"},
        {"menu.format_chart", "Format Chart"},
        {"menu.official_chart_mirror", "Official Chart Mirror"},
        {"menu.preview_mode_chart_review", "Preview Mode: Chart Review"},
        {"menu.preview_mode_muri_check", "Preview Mode: Muri Check"},
        {"menu.preview_mode_erase_by_area", "Preview Mode: Erase by Area"},
        {"menu.simaiwiki", "simaiwiki"},
        {"menu.swap_side_panels", "Swap Side Panels"},
        {"menu.tap_on_slide_threshold", "Tap-On-Slide Threshold..."},
        {"action.new", "New"},
        {"action.open", "Open"},
        {"action.open_folder", "Open Folder"},
        {"action.open_current_folder", "Open Current Folder"},
        {"action.open_recent", "Open Recent"},
        {"action.open_recent.empty", "No Recent Files"},
        {"dialog.open_startup_folder.missing_maidata.title", "maidata.txt Not Found"},
        {"dialog.open_startup_folder.missing_maidata.message", "No maidata.txt was found in the dropped folder:\n%1"},
        {"dialog.open_startup_target.missing.title", "Open Failed"},
        {"dialog.open_startup_target.missing.message", "The dropped file or folder does not exist:\n%1"},
        {"action.save", "Save"},
        {"action.ok", "OK"},
        {"action.discard", "Discard"},
        {"action.cancel", "Cancel"},
        {"drop_chart.create_chart_file", "Create Chart File"},
        {"drop_chart.drop_hint", "Release to create a new chart project"},
        {"drop_chart.preview.title", "Prepare Charts"},
        {"drop_chart.preview.message", "Create %1 chart(s) at:\n\n"},
        {"drop_chart.preview.create", "Create %1 Chart(s)"},
        {"drop_chart.error.title", "Chart Creation Failed"},
        {"drop_chart.create_failed", "The chart could not be created. No incomplete project was kept."},
        {"drop_chart.created_title", "Charts Created"},
        {"drop_chart.confirm_switch", "Open the first newly created chart? (%1 chart(s))"},
        {"drop_chart.created_chart", "Created %1 new chart(s)."},
        {"drop_chart.created_with_failures", "Created %1 chart(s), but %2 could not be created. No incomplete project was kept."},
        {"action.close", "Close"},
        {"action.yes", "Yes"},
        {"action.no", "No"},
        {"action.browse", "Browse..."},
        {"action.save_as", "Save As..."},
        {"action.preferences", "Preferences..."},
        {"action.restore_backup", "Restore Backup"},
        {"action.restore_backup.empty", "No Backups Available"},
        {"dialog.restore_backup.title", "Restore Backup"},
        {"dialog.restore_backup.missing", "Backup file does not exist:\n%1"},
        {"dialog.restore_backup.read_failed", "Cannot read backup file:\n%1"},
        {"dialog.restore_backup.confirm", "Restore the backup from %1?"},
        {"dialog.restore_backup.abnormal_exit_confirm", "MiaCode did not exit normally last time.\n\nRestore the backup from %1?"},
        {"status.restore_backup.loaded", "Restored from backup. Save to keep the changes."},
        {"action.about", "About"},
        {"dialog.invalid_star_preview.title", "Hidden Options"},
        {"dialog.invalid_star_preview.enable", "Invalid star preview mode has been enabled for this run only."},
        {"dialog.invalid_star_preview.already_enabled", "Invalid star preview mode is already enabled, and only for this run."},
        {"dialog.invalid_star_preview.enabled_status", "Invalid star preview mode is enabled (this run only)."},
        {"action.cut", "Cut"},
        {"action.copy", "Copy"},
        {"action.paste", "Paste"},
        {"action.undo", "Undo"},
        {"action.redo", "Redo"},
        {"action.stop_preview", "Stop Preview"},
        {"action.pause_preview", "Play/Pause Preview"},
        {"action.preview_speed_down", "Playback Speed -"},
        {"action.preview_speed_up", "Playback Speed +"},
        {"action.audio_settings", "Audio Settings"},
        {"action.video_settings", "Preview Settings"},
        {"action.skin_settings", "Skins"},
        {"toolbar.skin", "Skin"},
        {"toolbar.export", "Export"},
        {"action.export_chart", "Export Chart"},
        {"action.export_cover", "Export Cover"},
        {"action.batch_export", "Batch Export"},
        {"action.transform.mirror_lr", "Mirror Left/Right"},
        {"action.transform.mirror_ud", "Mirror Up/Down"},
        {"action.transform.rotate_180", "Rotate 180"},
        {"action.transform.rotate_ccw_45", "Rotate -45"},
        {"action.transform.rotate_cw_45", "Rotate +45"},
        {"action.transform.more", "More..."},
        {"action.transform.toggle_break", "Toggle Break"},
        {"action.transform.toggle_ex", "Toggle EX"},
        {"action.transform.toggle_firework", "Toggle Firework"},
        {"action.transform.random_rotate", "Random Rotate"},
        {"context.batch_transform", "Batch Operations"},
        {"context.more_transform", "More..."},
        {"toolbar.settings_placeholder", "Settings"},
        {"preview.play", "Play"},
        {"preview.pause", "Pause"},
        {"preview.fullscreen.window_title", "Fullscreen Preview"},
        {"preview.fullscreen.exit_hint", "Press Esc to exit fullscreen"},
        {"preview.fullscreen.exit_tooltip", "Exit fullscreen preview (Esc)"},
        {"preview.fullscreen.enter_tooltip", "Open fullscreen preview"},
        {"preview.canvas.free_aspect", "Free Aspect"},
        {"preview.canvas.menu_tooltip", "Preview canvas"},
        {"preview.canvas.menu_description", "Open preview canvas menu"},
        {"editor.metadata", "Metadata"},
        {"editor.welcome", "Welcome to MiaCode!"},
        {"editor.des", "Des"},
        {"editor.export", "Export"},
        {"editor.delete_bookmark", "Delete Bookmark"},
        {"editor.delete_bookmark_1_this_will", "Delete bookmark \"%1\"? This will delete the chart comment on that line."},
        {"editor.l_1", "L%1"},
        {"editor.new_bookmark", "New Bookmark"},
        {"editor.target_line_already_has_a", "Target line already has a comment; bookmark move canceled."},
        {"editor.untitled_bookmark", "Untitled Bookmark"},
        {"metadata.information", "Information"},
        {"metadata.other_fields", "Other &xx Fields"},
        {"metadata.field.title", "title"},
        {"metadata.field.artist", "artist"},
        {"metadata.field.first", "Offset"},
        {"metadata.field.des", "des"},
        {"metadata.field.cover", "cover"},
        {"metadata.choose_an_mp3_and_pull", "Choose an MP3 and pull the title from its ID3 tag."},
        {"metadata.choose_an_mp3_and_pull_2", "Choose an MP3 and pull the artist from its ID3 tag."},
        {"metadata.choose_an_mp3_and_write", "Choose an MP3 and write its embedded cover artwork as bg.jpg next to the chart."},
        {"metadata.click_to_open_the_muri", "Click to open the Muri tab"},
        {"metadata.click_to_open_the_syntax", "Click to open the Syntax tab"},
        {"metadata.close", "Close"},
        {"metadata.copy_area", "Copy area"},
        {"metadata.copy_area_2", "Copy Area"},
        {"metadata.delete", "Delete"},
        {"metadata.delete_1", "Delete %1"},
        {"metadata.edit_e", "Edit(&E)"},
        {"metadata.empty_hint", "← Click to add a chart difficulty"},
        {"metadata.find", "Find"},
        {"metadata.find_next", "Find Next"},
        {"metadata.find_previous", "Find Previous"},
        {"metadata.full_copy_area", "Full Copy Area"},
        {"metadata.insert_bookmark", "Insert Bookmark"},
        {"metadata.jump_to_timeline_position", "Jump to Timeline Position"},
        {"metadata.latency_card.title", "Latency && Offset Calibration"},
        {"metadata.latency_card.open", "Open Latency Settings →"},
        {"metadata.ln_1_col_1", "Ln 1, Col 1"},
        {"metadata.manage_per_difficulty_designers", "Manage per-difficulty designers"},
        {"metadata.open_the_latency_settings_page", "Open the Latency Settings page: adjust BPM/Offset and audition for calibration."},
        {"metadata.preview_p", "Preview(&P)"},
        {"metadata.read_from_mp3", "Read from MP3"},
        {"metadata.rename", "Rename"},
        {"metadata.rename_bookmark", "Rename Bookmark"},
        {"metadata.replace", "Replace"},
        {"metadata.replace_all", "Replace All"},
        {"metadata.set_each_difficulty_designer", "Set each difficulty's designer (&des_1 … &des_7); includes the \"all difficulties share one designer\" toggle."},
        {"metadata.show_in_sidebar", "Show in Sidebar"},
        {"sidebar.metadata", "Metadata"},
        {"sidebar.add_difficulty", "+ Add Diff."},
        {"sidebar.export", "Export"},
        {"tab.timeline", "Timeline"},
        {"tab.validation_errors", "Validation Errors"},
        {"dialog.preferences.title", "Preferences"},
        {"dialog.preferences.interface_group", "Appearance"},
        {"dialog.preferences.background_group", "Background"},
        {"dialog.preferences.background.enabled", "Enable background"},
        {"dialog.preferences.background.image", "Image"},
        {"dialog.preferences.background.choose", "Choose..."},
        {"dialog.preferences.background.clear", "Clear"},
        {"dialog.preferences.background.image_filter", "Images (*.png *.jpg *.jpeg *.bmp *.webp);;All Files (*)"},
        {"dialog.preferences.background.image_error", "The selected image cannot be read."},
        {"dialog.preferences.background.save_error", "Failed to save background settings."},
        {"dialog.preferences.background.file_picker_unavailable", "The image picker is unavailable."},
        {"dialog.preferences.background.opacity", "Opacity"},
        {"dialog.preferences.background.overlay", "Cover opacity"},
        {"dialog.preferences.background.overlay_button", "Cover opacity..."},
        {"dialog.preferences.background.overlay_dialog_title", "Background Cover Opacity"},
        {"dialog.preferences.background.overlay.toolbar", "Top menu and toolbar"},
        {"dialog.preferences.background.overlay.status", "Bottom status bar"},
        {"dialog.preferences.background.overlay.panel", "Preview frame"},
        {"dialog.preferences.background.overlay.editor_header", "Chart editor title bar"},
        {"dialog.preferences.background.overlay.input", "Input fields"},
        {"dialog.preferences.background.overlay.code_editor", "Code editor"},
        {"dialog.preferences.background.overlay.dark", "%1 (Dark)"},
        {"dialog.preferences.background.overlay.light", "%1 (Light)"},
        {"dialog.preferences.background.overlay.reset_defaults", "Restore Defaults"},
        {"dialog.preferences.background.blur", "Blur"},
        {"dialog.preferences.background.scale", "Scale"},
        {"dialog.preferences.background.scale.cover", "Cover"},
        {"dialog.preferences.background.scale.contain", "Contain"},
        {"dialog.preferences.background.scale.stretch", "Stretch"},
        {"dialog.preferences.background.scale.center", "Center"},
        {"dialog.preferences.background.scale.repeat", "Repeat"},
        {"dialog.preferences.background.position", "Position"},
        {"dialog.preferences.background.position.center", "Center"},
        {"dialog.preferences.background.position.left", "Left"},
        {"dialog.preferences.background.position.right", "Right"},
        {"dialog.preferences.background.position.top", "Top"},
        {"dialog.preferences.background.position.bottom", "Bottom"},
        {"dialog.preferences.background.position.left_top", "Left Top"},
        {"dialog.preferences.background.position.right_top", "Right Top"},
        {"dialog.preferences.background.position.left_bottom", "Left Bottom"},
        {"dialog.preferences.background.position.right_bottom", "Right Bottom"},
        {"dialog.preferences.language", "Language"},
        {"dialog.preferences.language.system", "Follow System"},
        {"dialog.preferences.language.english", "English"},
        {"dialog.preferences.language.chinese", "Simplified Chinese"},
        {"dialog.preferences.language.japanese", "Japanese"},
        {"dialog.preferences.theme", "Theme"},
        {"dialog.preferences.theme.system", "Follow System"},
        {"dialog.preferences.theme.light", "Light"},
        {"dialog.preferences.theme.dark", "Dark"},
        {"dialog.preferences.preview_side", "Preview Position"},
        {"dialog.preferences.preview_side.right", "Right"},
        {"dialog.preferences.preview_side.left", "Left"},
        {"dialog.welcome.title", "Welcome to MiaCode"},
        {"dialog.welcome.heading", "Welcome to MiaCode!"},
        {"dialog.welcome.subtitle", "Choose how your workspace looks. You can change these anytime later in Preferences (the gear icon in the top menu)."},
        {"dialog.welcome.preview.editor", "Editor"},
        {"dialog.welcome.preview.preview", "Preview"},
        {"dialog.welcome.preview_side", "Preview pane"},
        {"dialog.welcome.preview_side.right", "On the right"},
        {"dialog.welcome.preview_side.left", "On the left"},
        {"dialog.welcome.theme", "Color theme"},
        {"dialog.welcome.theme.light", "Light"},
        {"dialog.welcome.theme.dark", "Dark"},
        {"dialog.welcome.chinese_input", "IME block"},
        {"dialog.welcome.chinese_input.disable", "Block IME"},
        {"dialog.welcome.chinese_input.enable", "Allow IME"},
        {"dialog.welcome.chinese_input.fullwidth", "Normalize full-width symbols"},
        {"dialog.welcome.chinese_input.hint",
         "If you don't use comments, choose \"Block IME\".\n"
         "If you do use comments and want to avoid mistyped full-width symbols such as 1h【8:1】, "
         "choose \"Normalize full-width symbols\"."},
        {"dialog.welcome.get_started", "Get Started"},
        {"dialog.preferences.editor_group", "Editor"},
        {"dialog.preferences.editor_font_size", "Text Font Size"},
        {"dialog.preferences.editor_line_spacing", "Line Spacing"},
        {"dialog.preferences.editor_half_width_input", "Lock half-width symbol input"},
        {"dialog.preferences.editor_auto_completion", "Auto-complete brackets"},
        {"dialog.preferences.editor_ime_input_disabled", "Block IME input"},
        {"dialog.preferences.performance_group", "Performance"},
        {"dialog.preferences.shortcuts_group", "Shortcuts"},
        {"dialog.preferences.shortcuts.edit", "Edit Shortcuts"},
        {"dialog.preferences.shortcuts.reset", "Restore Shortcuts"},
        {"dialog.preferences.shortcuts.title", "Keyboard Shortcuts"},
        {"dialog.preferences.shortcuts.command", "Command"},
        {"dialog.preferences.shortcuts.keybinding", "Keybinding"},
        {"dialog.preferences.shortcuts.change", "Change Keybinding"},
        {"dialog.preferences.shortcuts.capture_title", "Change Keybinding"},
        {"dialog.preferences.shortcuts.capture_prompt", "Press the desired key combination or mouse wheel gesture, then press Enter."},
        {"dialog.preferences.shortcuts.capture_prompt_hold", "Press the key to hold (a bare modifier like Alt works), then press Enter."},
        {"dialog.preferences.shortcuts.reset_confirm_title", "Restore Shortcuts"},
        {"dialog.preferences.shortcuts.reset_confirm_message", "Restore all editable shortcuts to their defaults?"},
        {"action.reset", "Reset"},
        {"dialog.preferences.restart_title", "Restart Required"},
        {"dialog.preferences.restart_message", "Language settings have been saved. Restart MiaCode to apply menus, fonts, and interface text."},
        {"dialog.unsaved_changes.title", "Unsaved Changes"},
        {"dialog.unsaved_changes.message", "Current document has unsaved changes. Save before continue?"},
        {"dialog.unsaved_field_changes.title", "Unsaved Field Changes"},
        {"dialog.unsaved_field_changes.message", "%1 has unsaved changes. Save before switch?"},
        {"dialog.unsaved_field_changes.field.metadata", "Metadata"},
        {"dialog.unsaved_tab_changes.title", "Unsaved Tab Changes"},
        {"dialog.unsaved_tab_changes.message", "%1 has unsaved changes. Save before closing this tab?"},
        {"dialog.unsaved_tab_changes.details.difficulty", "Save writes this difficulty only; discard restores it to its last saved content."},
        {"dialog.unsaved_tab_changes.details.document", "Save writes the whole document; discard restores it to its last saved content."},
        {"dialog.audio_settings.title", "Audio Settings"},
        {"dialog.video_settings.title", "Preview Settings"},
        {"dialog.render_settings.audio_group", "Audio"},
        {"dialog.render_settings.video_group", "Video"},
        {"dialog.render_settings.gameplay_group", "Gameplay"},
        {"dialog.render_settings.music_group", "Music"},
        {"dialog.render_settings.performance_group", "Performance"},
        {"dialog.render_settings.preview_group", "Preview"},
        {"dialog.skin_settings.title", "Skin"},
        {"dialog.skin_settings.dialog_title", "Skin Settings"},
        {"dialog.skin_settings.section.chart_skin", "Chart Skin"},
        {"dialog.skin_settings.open_skin_folder", "Open Skin Folder"},
        {"dialog.skin_settings.open_judge_line_folder", "Open Judge Line Folder"},
        {"dialog.skin_settings.open_directory", "Open Directory"},
        {"dialog.skin_settings.chart_effect", "Judge Effect"},
        {"dialog.skin_settings.chart_effect.standard", "SD"},
        {"dialog.skin_settings.chart_effect.starry", "DX"},
        {"dialog.render_settings.button.close", "Close"},
        {"dialog.render_settings.button.set_software_default_audio", "Save Local Preset"},
        {"dialog.render_settings.button.restore_project_default", "Apply Local Preset"},
        {"dialog.render_settings.button.mute_non_bgm", "Mute non-BGM"},
        {"dialog.render_settings.button.restore_non_bgm", "Restore non-BGM volume"},
        {"dialog.render_settings.option.enabled", "Enabled"},
        {"dialog.render_settings.option.disabled", "Disabled"},
        {"dialog.render_settings.audio.master", "Master Volume"},
        {"dialog.render_settings.audio.bgm", "BGM Volume"},
        {"dialog.render_settings.audio.answer", "Answer Volume"},
        {"dialog.render_settings.audio.tap", "Tap Volume"},
        {"dialog.render_settings.audio.break", "Break Volume"},
        {"dialog.render_settings.audio.break_slide", "Break Slide Volume"},
        {"dialog.render_settings.audio.slide", "Slide Volume"},
        {"dialog.render_settings.audio.break_slide_tail_cheer_mute", "Disable breakslide tail cheer"},
        {"dialog.render_settings.audio.ex", "EX Volume"},
        {"dialog.render_settings.audio.touch", "Touch Volume"},
        {"dialog.render_settings.audio.track", "Track Volume"},
        {"dialog.render_settings.audio.global", "Global Volume"},
        {"dialog.render_settings.audio.firework", "Firework Volume"},
        {"dialog.render_settings.audio.button.mute", "Mute %1"},
        {"dialog.render_settings.audio.button.unmute", "Unmute %1"},
        {"dialog.render_settings.music.intro_sound", "Intro sound"},
        {"dialog.render_settings.music.intro_sound_volume", "Intro sound volume"},
        {"dialog.render_settings.music.default_intro_sound", "Default intro sound"},
        {"dialog.render_settings.music.audition", "Audition"},
        {"dialog.render_settings.music.open_folder", "Open Folder"},
        {"dialog.render_settings.music.open_folder.tooltip", "Put intro sound files here; when missing, MiaCode checks assets/music/track_start.wav, then assets/SFX/track_start.wav."},
        {"dialog.render_settings.video.brightness", "Background/PV Brightness"},
        {"dialog.render_settings.video.scale.fill", "Fill (crop if needed)"},
        {"dialog.render_settings.video.scale.fit", "Fit (keep full image, may letterbox)"},
        {"dialog.render_settings.video.scale.square_fit", "1:1 Fit (center square)"},
        {"dialog.render_settings.video.scale.inner_circle_fit_outer_fill", "Inner 1:1 Fit + Outer Fill"},
        {"dialog.render_settings.video.canvas_aspect.square", "1:1 (Square)"},
        {"dialog.render_settings.video.canvas_aspect.4_3", "4:3"},
        {"dialog.render_settings.video.canvas_aspect.16_9", "16:9"},
        {"dialog.render_settings.video.auto_restore_square", "Auto-restore 1:1 after export"},
        {"dialog.render_settings.video.smooth_brightness", "Smooth brightness"},
        {"dialog.render_settings.video.touch_pad_authoring_shortcut", "Enable touch click input"},
        {"dialog.render_settings.video.brightness_outer", "Outer Brightness"},
        {"dialog.render_settings.video.brightness_inner", "Inner Brightness"},
        {"dialog.render_settings.video.layout_square_scale", "Stage Display Scale"},
        {"dialog.render_settings.video.flow_speed", "Flow Speed"},
        {"dialog.render_settings.video.tap_flow_speed", "Tap Flow Speed"},
        {"dialog.render_settings.video.touch_flow_speed", "Touch Flow Speed"},
        {"dialog.render_settings.video.skin", "Skin"},
        {"dialog.render_settings.video.skin.standard", "SD"},
        {"dialog.render_settings.video.skin.dx", "DX"},
        {"dialog.render_settings.video.skin.import", "Import..."},
        {"dialog.render_settings.video.scale_mode", "Background / PV Scale Mode"},
        {"dialog.render_settings.video.canvas_aspect", "Preview Canvas Aspect"},
        {"dialog.render_settings.gameplay.judge_effect", "Judge Effect Display"},
        {"dialog.render_settings.gameplay.judge_effect.slide", "slide"},
        {"dialog.render_settings.gameplay.judge_effect.tap", "tap"},
        {"dialog.render_settings.gameplay.judge_effect.break", "break"},
        {"dialog.render_settings.gameplay.judge_effect.touch", "touch"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance", "Tap Judge Text Distance"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance.inner", "Inner"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance.middle", "Middle"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance.outer", "Outer"},
        {"dialog.render_settings.gameplay.judge_line", "Judge Line"},
        {"dialog.render_settings.gameplay.judge_line.point", "Point"},
        {"dialog.render_settings.gameplay.judge_line.line", "Line"},
        {"dialog.render_settings.gameplay.judge_line.area", "Judge Area"},
        {"dialog.render_settings.gameplay.judge_line.area_labeled", "Judge Area (Labeled)"},
        {"dialog.render_settings.gameplay.judge_line.import", "Import..."},
        {"dialog.render_settings.gameplay.force_labeled_judge_line_when_paused", "Show judge area while preview is paused"},
        {"dialog.render_settings.gameplay.center_display", "Center Display"},
        {"dialog.render_settings.gameplay.slide_stack_order", "Slide Stack Order"},
        {"dialog.render_settings.gameplay.slide_stack_order.dx_style", "DX Style"},
        {"dialog.render_settings.gameplay.slide_stack_order.finale_style", "FiNALE Style"},
        {"dialog.render_settings.preview.debug", "Show preview debug info"},
        {"dialog.render_settings.preview.canvas_frame_rate", "Preview Refresh Rate"},
        {"dialog.render_settings.preview.canvas_frame_rate.30", "30 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.display", "Display Refresh Rate"},
        {"dialog.preferences.performance.pv_frame_rate", "PV Refresh Rate"},
        {"dialog.preferences.performance.timeline_frame_rate", "Timeline Refresh Rate"},
        {"dialog.preferences.performance.video_decode", "PV Render"},
        {"dialog.preferences.performance.video_decode.hardware", "Hardware"},
        {"dialog.preferences.performance.video_decode.software", "Software"},
        {"dialog.render_settings.preview.show_object_stats", "Show object stats in preview/export"},
        {"dialog.render_settings.preview.show_validation_summary", "Show top error/warning summary"},
        {"editor.validation_summary.tooltip_with_muri", "%1 error(s), %2 warning(s), %3 muri issue(s)"},
        {"dialog.render_settings.preview.show_object_stats_preview", "Show object stats in preview"},
        {"dialog.render_settings.preview.show_object_stats_export", "Show object stats in export"},
        {"dialog.video_export.title", "Export Video"},
        {"dialog.video_export.output", "Output"},
        {"dialog.video_export.browse", "Browse..."},
        {"dialog.video_export.resolution", "Resolution"},
        {"dialog.video_export.fps", "FPS"},
        {"dialog.video_export.audio_bitrate", "Audio quality"},
        {"dialog.video_export.preset", "Export Quality"},
        {"dialog.video_export.preset.fast", "Fast"},
        {"dialog.video_export.preset.high_quality", "High Quality"},
        {"dialog.video_export.size_preset", "File size"},
        {"dialog.video_export.size_preset.standard", "Standard"},
        {"dialog.video_export.size_preset.compact", "Smaller"},
        {"dialog.video_export.size_preset.ultra_compact_with_pv", "Smallest (With PV)"},
        {"dialog.video_export.size_preset.ultra_compact", "Smallest (No PV)"},
        {"dialog.video_export.section.options", "Options"},
        {"dialog.video_export.section.font", "Font"},
        {"dialog.video_export.section.range", "Export Range"},
        {"dialog.video_export.range.start", "Start"},
        {"dialog.video_export.range.end", "End"},
        {"dialog.video_export.range.set_left", "Set ?"},
        {"dialog.video_export.range.set_end", "Set End"},
        {"dialog.video_export.range.set_current", "Set to current value"},
        {"dialog.video_export.range.set_current.tip", "Set to the current preview position"},
        {"dialog.video_export.range.intro_tag", "intro"},
        {"dialog.video_export.preview.stop", "Stop"},
        {"dialog.video_export.preview.play", "Play"},
        {"dialog.video_export.preview.pause", "Pause"},
        {"dialog.video_export.button.export", "Export"},
        {"dialog.video_export.button.cancel", "Cancel"},
        {"dialog.video_export.button.close", "Close"},
        {"dialog.video_export.option.show_object_stats", "Show object stats"},
        {"dialog.video_export.option.show_chart_info", "Show chart info"},
        {"dialog.video_export.option.fix_hud_text_layout", "Fix HUD font line spacing"},
        {"dialog.video_export.option.hud_font", "HUD Font"},
        {"dialog.video_export.option.hud_font_settings", "Font Settings"},
        {"dialog.video_export.option.import_hud_font", "Import Font"},
        {"dialog.video_export.option.reset_hud_font", "Reset"},
        {"dialog.video_export.option.hud_font_default", "Default font"},
        {"dialog.video_export.option.hud_font_area", "Area"},
        {"dialog.video_export.option.hud_font_area.chart_info", "Top-left export info"},
        {"dialog.video_export.option.hud_font_area.timestamp", "Bottom-left timestamp"},
        {"dialog.video_export.option.hud_font_area.object_stats", "Object stats"},
        {"dialog.video_export.option.hud_font_area.debug", "Debug info"},
        {"dialog.video_export.error.invalid_hud_font", "Please select a .ttf or .otf font file."},
        {"dialog.video_export.error.copy_hud_font_failed", "Failed to copy the font into the font library."},
        {"dialog.video_export.option.show_timestamp", "Show bottom-left timestamp"},
        {"dialog.video_export.option.smooth_brightness", "Smooth brightness"},
        {"dialog.video_export.option.brightness_outer", "Brightness (Outer)"},
        {"dialog.video_export.option.brightness_inner", "Brightness (Inner)"},
        {"dialog.video_export.option.layout_size", "Stage Display Scale"},
        {"dialog.video_export.option.flow_speed", "Flow Speed"},
        {"dialog.video_export.option.tap_flow_speed", "Tap Flow Speed"},
        {"dialog.video_export.option.touch_flow_speed", "Touch Flow Speed"},
        {"dialog.video_export.option.scale_mode", "Background / PV Scale Mode"},
        {"dialog.video_export.option.scale.fill", "Fill (crop if needed)"},
        {"dialog.video_export.option.scale.fit", "Fit (keep full image, may letterbox)"},
        {"dialog.video_export.option.scale.square_fit", "1:1 Fit (center square)"},
        {"dialog.video_export.option.scale.inner_circle_fit_outer_fill", "Inner 1:1 Fit + Outer Fill"},
        {"dialog.batch_export.title", "Batch Export"},
        {"dialog.batch_export.task", "Task"},
        {"dialog.batch_export.difficulty", "Difficulty"},
        {"dialog.batch_export.output_dir", "Output Folder"},
        {"dialog.batch_export.chart_folders", "Chart Folders"},
        {"dialog.batch_export.add_folders", "Add Folders"},
        {"dialog.batch_export.remove_selected", "Remove Selected"},
        {"dialog.batch_export.clear", "Clear"},
        {"dialog.batch_export.select_charts", "Select Chart Folders"},
        {"dialog.batch_export.select_folder", "Select Folder"},
        {"dialog.batch_export.error.no_difficulty", "No active difficulty is selected."},
        {"dialog.batch_export.error.no_preview", "Preview canvas is not initialized."},
        {"dialog.batch_export.error.no_chart_dirs", "Please add at least one chart folder."},
        {"dialog.batch_export.error.no_difficulties", "Please select at least one difficulty."},
        {"dialog.batch_export.error.no_output_dir", "Please choose an output folder."},
        {"dialog.batch_export.error.output_dir_create_failed", "Failed to create output folder."},
        {"dialog.batch_export.error.invalid_selection", "Some folders were skipped because required files are missing."},
        {"dialog.batch_export.error.invalid_folder", "The selected path is not a valid folder."},
        {"dialog.batch_export.error.missing_chart_file", "Missing maidata.txt."},
        {"dialog.batch_export.error.missing_track_file", "Missing track.mp3."},
        {"dialog.batch_export.error.read_chart_failed", "Failed to read %1."},
        {"dialog.batch_export.error.missing_requested_difficulty", "Requested difficulty is missing."},
        {"dialog.batch_export.error.no_selected_difficulties_in_folder", "None of the selected difficulties exist in this folder: %1"},
        {"dialog.batch_export.error.no_markers", "No parsed note markers are available for this difficulty."},
        {"dialog.batch_export.error.invalid_duration", "Failed to determine export duration for this chart."},
        {"dialog.batch_export.error.validation_failed_count", "Syntax check failed with %1 error(s)."},
        {"dialog.batch_export.error.validation_failed_detail", "Syntax check failed: %1"},
        {"dialog.batch_export.progress.preparing", "Preparing batch export..."},
        {"dialog.batch_export.progress.exporting", "Exporting %1/%2...\n%3"},
        {"dialog.batch_export.progress.exporting_named", "Exporting %1/%2\n%3"},
        {"dialog.batch_export.progress.current_item", "%1\n%2"},
        {"dialog.batch_export.message.canceled", "Batch export canceled."},
        {"dialog.batch_export.message.success", "Batch export completed: %1 file(s)."},
        {"dialog.batch_export.message.partial_failed", "Batch export finished with failures.\nSucceeded: %1\nFailed: %2"},
        {"dialog.batch_export.message.output_files", "Output files:"},
        {"dialog.video_export.error.preview_unavailable", "Preview canvas is not initialized."},
        {"dialog.video_export.progress.preparing", "Preparing export..."},
        {"dialog.video_export.progress.worker_ready", "Worker ready..."},
        {"dialog.video_export.progress.starting_export", "Starting export..."},
        {"dialog.video_export.progress.preparing_audio", "Preparing audio..."},
        {"dialog.video_export.progress.starting_ffmpeg", "Starting ffmpeg..."},
        {"dialog.video_export.progress.rendering", "Rendering frames..."},
        {"dialog.video_export.progress.rendering_count", "Rendering frames... %1/%2"},
        {"dialog.video_export.progress.finalizing_encode", "Finalizing video..."},
        {"dialog.video_export.progress.repacking", "Finalizing video..."},
        {"dialog.video_export.progress.finishing", "Finishing up..."},
        {"dialog.video_export.progress.remaining", "About %1 remaining"},
        {"dialog.video_export.progress.generic", "Exporting..."},
        {"dialog.video_export.progress.canceling", "Canceling export..."},
        {"dialog.video_export.progress.retrying_safe_mode", "Export worker crashed. Retrying in safe mode..."},
        {"dialog.video_export.progress.done", "Done."},
        {"dialog.video_export.status.canceled", "Export canceled."},
        {"dialog.video_export.status.completed", "Export completed."},
        {"dialog.video_export.message.canceled", "Export canceled."},
        {"dialog.video_export.message.completed", "Export completed."},
        {"dialog.video_export.error.failed", "Export failed."},
        {"dialog.video_export.error.worker_crash", "Export worker crashed."},
        {"dialog.video_export.error.worker_exit", "Export worker exited unexpectedly."},
        {"dialog.video_export.error.worker_retry_note", "The export worker crashed once and was retried automatically with PBO disabled."},
        {"dialog.video_export.error.worker_retry_first_attempt", "First attempt diagnostics"},
        {"dialog.video_export.error.worker_retry_final_attempt", "Safe-mode retry diagnostics"},
        {"dialog.video_export.error.failed_title", "Export Failed"},
        {"status.audio_restored_default", "Restored default audio settings"},
        {"status.audio_saved_software_default", "Saved current audio settings as the software default"},
        {"status.touch_trail_enabled", "Touch trail enabled."},
        {"status.touch_trail_disabled", "Touch trail hidden."},
        {"status.judge_marker_enabled", "Judge markers enabled."},
        {"status.judge_marker_disabled", "Judge markers hidden."},
        {"status.editor_text_display_updated", "Editor text display updated."},
        {"status.preferences_updated", "Preferences updated."},
        {"status.preferences_saved", "Preferences saved. Restart to apply."},
        {"status.syntax.select_difficulty", "Select a difficulty text first."},
        {"status.syntax.failed_counts", "Syntax check failed: %1 error(s), %2 warning(s)."},
        {"about.platform", "Release Platform"},
        {"about.build_type", "Build Type"},
        {"dialog.batch_export.error.export_failed", "Export failed."},
        {"dialog.batch_export.error.invalid_first", "Invalid &first value in chart metadata."},
        {"dialog.batch_export.error.skin_missing", "Preview skin assets were not found."},
        {"dialog.normalize.title", "Format Chart"},
        {"dialog.normalize.failed", "Failed to normalize the current chart."},
        {"dialog.normalize.options", "Fine Tuning"},
        {"dialog.preferences.extensions_group", "Extensions"},
        {"dialog.preferences.extensions.open_folder", "Open Extensions Folder"},
        {"dialog.preferences.extensions.refresh", "Refresh Extensions"},
        {"dialog.preferences.extensions.open_logs", "Open Logs Folder"},
        {"dialog.preferences.extensions.devtools", "DevTools Panel"},
        {"dialog.preferences.extensions.devtools.summary", "Snapshot Summary"},
        {"dialog.preferences.extensions.devtools.api_count", "APIs"},
        {"dialog.preferences.extensions.devtools.extension_count", "Extensions"},
        {"dialog.preferences.extensions.devtools.raw_count", "Experimental raw"},
        {"dialog.preferences.extensions.devtools.callback_count", "Event callbacks"},
        {"dialog.preferences.extensions.devtools.call_count", "Recent calls"},
        {"dialog.preferences.extensions.devtools.diagnostic_count", "Diagnostics"},
        {"dialog.preferences.extensions.devtools.api", "API Registry"},
        {"dialog.preferences.extensions.devtools.open_bridge", "Open Bridge"},
        {"dialog.preferences.extensions.devtools.recent_calls", "Recent Calls"},
        {"dialog.preferences.extensions.devtools.ui", "UI Contributions"},
        {"dialog.preferences.extensions.devtools.diagnostics", "Diagnostics"},
        {"dialog.preferences.extensions.devtools.raw_json", "Raw JSON"},
        {"dialog.preferences.extensions.devtools.refresh", "Refresh Snapshot"},
        {"dialog.preferences.extensions.enabled", "Enabled"},
        {"dialog.preferences.extensions.name", "Extension"},
        {"dialog.preferences.extensions.version", "Version"},
        {"dialog.preferences.extensions.contributions", "Contributions"},
        {"dialog.preferences.extensions.permissions", "Permissions"},
        {"dialog.preferences.extensions.status", "Status"},
        {"extension.menu.extensions", "Extensions"},
        {"extension.menu.empty", "No extension commands"},
        {"extension.dialog.permission_title", "Extension Permission"},
        {"extension.dialog.permission_message", "Extension %1 requests high-risk permission:\n%2\n\nTarget: %3\n\nAllow and remember this grant?"},
        {"extension.dialog.input_title", "Extension Input"},
        {"extension.dialog.quick_pick_title", "Extension Selection"},
        {"extension.dialog.canceled", "Canceled."},
        {"extension.command.unavailable",
         "Extension command is unavailable: %1\n\nPossible causes: the extension is disabled, its manifest is invalid, or the command no longer exists after refresh."},
        {"extension.command.runtime_not_running",
         "Extension runtime is not running. MiaCode cannot run: %1\n\nPossible causes: the extension has no loadable JS entry, activation failed, or extensions need to be refreshed."},
        {"extension.command.failed", "Extension command did not respond normally: %1\n\nReason: %2"},
        {"extension.command.ran", "Extension command ran: %1"},
        {"editor.bookmark.insert", "Insert Bookmark"},
        {"editor.bookmark.rename", "Rename Bookmark"},
        {"editor.bookmark.delete", "Delete Bookmark"},
        {"editor.bookmark.show_in_sidebar", "Show in Sidebar"},
        {"editor.bookmark.untitled", "Untitled Bookmark"},
        {"editor.bookmark.delete_confirm", "Delete bookmark \"%1\"? This will remove the chart comment on that line."},
        {"dialog.render_settings.gameplay.center_display.achievement_dx_minus_100", "ACHIEVEMENT DX (100-)"},
        {"dialog.render_settings.gameplay.center_display.achievement_dx_minus_101", "ACHIEVEMENT DX (101-)"},
        {"dialog.render_settings.gameplay.center_display.achievement_dx_plus", "ACHIEVEMENT DX (+)"},
        {"dialog.render_settings.gameplay.center_display.achievement_finale_plus", "ACHIEVEMENT FINALE (+)"},
        {"dialog.render_settings.gameplay.center_display.combo", "Combo"},
        {"dialog.render_settings.gameplay.center_display.dx_score_minus", "DX SCORE (-)"},
        {"dialog.render_settings.gameplay.center_display.dx_score_plus", "DX SCORE (+)"},
        {"dialog.render_settings.gameplay.center_display.off", "Off"},
        {"dialog.video_export.button.cancel_export", "Cancel Export"},
        {"dialog.video_export.button.start_export", "Start Export"},
        {"dialog.video_export.error.executable_missing", "Failed to locate MiaCode executable."},
        {"dialog.video_export.error.invalid_flow_speed", "Flow speed is invalid."},
        {"dialog.video_export.error.launch_failed", "Failed to start background export."},
        {"dialog.video_export.error.no_difficulty", "No active difficulty is selected."},
        {"dialog.video_export.error.no_markers", "No parsed note markers are available for export."},
        {"dialog.video_export.error.skin_missing", "Preview skin assets were not found."},
        {"dialog.video_export.error.sync_failed", "Failed to sync current editor state."},
        {"dialog.video_export.error.worker_busy", "Another export is already running."},
        {"dialog.video_export.error.worker_write_failed", "Failed to send export snapshot to worker."},
        {"dialog.video_export.section.intro", "Intro"},
        {"dialog.video_export.section.output", "Output"},
        {"editor.validation_summary.tooltip", "%1 error(s), %2 warning(s)"},
        {"status.muri_render_mode_dx", "Preview mode: muri check."},
        {"status.muri_render_mode_native", "Preview mode: chart review."},
        {"status.muri_render_mode_erase_by_area", "Preview mode: erase by area."},
        {"status.normalize.already_normalized", "Format Chart: already normalized."},
        {"status.normalize.applied", "Format Chart applied: %1 measure line(s)."},
        {"status.transform.mirror_lr", "Mirror Left/Right applied."},
        {"status.transform.mirror_ud", "Mirror Up/Down applied."},
        {"status.transform.rotate_180", "Rotate 180 applied."},
        {"status.transform.rotate_ccw_45", "Rotate -45 applied."},
        {"status.transform.rotate_cw_45", "Rotate +45 applied."},

        {"card_font.body", "Body font"},
        {"card_font.copy_failed", "Could not copy the font into the library."},
        {"card_font.default", "Default font"},
        {"card_font.import", "Import font…"},
        {"card_font.invalid_font", "That file is not a valid font."},
        {"card_font.reset", "Reset"},
        {"card_font.title", "Title font"},
        {"cover.add_image", "Add image"},
        {"cover.add_layer", "Add ▾"},
        {"cover.add_text", "Add text"},
        {"cover.bold", "Bold"},
        {"cover.choose_image", "Choose image"},
        {"cover.font", "Font"},
        {"cover.image_file", "Image file"},
        {"cover.image_layer", "Image"},
        {"cover.image_options", "Image options"},
        {"cover.text_color", "Color"},
        {"cover.text_content", "Text"},
        {"cover.text_layer", "Text"},
        {"cover.text_layer_default", "Text"},
        {"cover.text_options", "Text options"},
        {"cover.add_a_chart_frame_a", "Add a chart frame (A)"},
        {"cover.add_chart_frame", "Add chart frame"},
        {"cover.add_difficulty_card", "Add difficulty card"},
        {"cover.add_frame", "＋ Add frame"},
        {"cover.apply_preset", "Apply preset"},
        {"cover.backdrop_brightness", "Backdrop brightness"},
        {"cover.background", "Background"},
        {"cover.background_brightness", "Background brightness"},
        {"cover.background_transparency", "Background transparency"},
        {"cover.blur_background", "Blur background"},
        {"cover.brightness", "Brightness"},
        {"cover.brightness_2", "Brightness"},
        {"cover.bring_to_front", "Bring to front"},
        {"cover.browse", "Browse…"},
        {"cover.canvas", "Canvas"},
        {"cover.card_chart_frame", "Card + chart frame"},
        {"cover.card_drop_shadow", "Card drop shadow"},
        {"cover.centered_card_default", "Centered card (default)"},
        {"cover.chart_frame", "Chart frame"},
        {"cover.chart_frame_background_brightness", "Chart-frame background brightness"},
        {"cover.chart_frame_background_transparency", "Chart-frame background transparency"},
        {"cover.chart_frame_inner_background", "Chart-frame inner background"},
        {"cover.chart_frame_options", "Chart frame options"},
        {"cover.chart_jacket", "Chart jacket (曲绘)"},
        {"cover.chart_type", "Chart type"},
        {"cover.chart_type_auto_result", "Auto (%1)"},
        {"cover.choose_background_image", "Choose background image"},
        {"cover.clear_recent", "Clear recent"},
        {"cover.close", "Close"},
        {"cover.close_without_exporting_esc", "Close without exporting (Esc)"},
        {"cover.could_not_read_the_layout", "Could not read the layout file."},
        {"cover.could_not_render_the_chart", "Could not render the chart frame."},
        {"cover.could_not_write_the_layout", "Could not write the layout file."},
        {"cover.cover_export_completed", "Cover export completed."},
        {"cover.cover_export_failed_1", "Cover export failed:\n%1"},
        {"cover.cover_layout_miacover", "Cover layout (*.miacover)"},
        {"cover.cover_layout_miacover_legacy_json", "Cover layout (*.miacover);;Legacy JSON (*.json)"},
        {"cover.custom_background_image_path", "Custom background image path"},
        {"cover.custom_image", "Custom image"},
        {"cover.delete_preset", "Delete preset"},
        {"cover.duplicate_layer", "Duplicate layer"},
        {"cover.delete_the_selected_layer_delete", "Delete the selected layer (Delete)"},
        {"cover.delete_this_preset", "Delete this preset?"},
        {"cover.difficulty_card", "Difficulty card"},
        {"cover.difficulty_card_2", "Difficulty card"},
        {"cover.difficulty_card_options", "Difficulty card options"},
        {"cover.dual_chart_frame_collage", "Dual chart-frame collage"},
        {"cover.export", "Export"},
        {"cover.export_cover", "Export Cover"},
        {"cover.failed_to_start_the_composer", "Failed to start the composer:\n%1"},
        {"cover.file_not_found", "File not found"},
        {"cover.frame", "Frame"},
        {"cover.frame_2", "Frame "},
        {"cover.frame_time", "Frame time"},
        {"cover.frame_time_2", "Frame time"},
        {"cover.frame_time_for_the_selected", "Frame time for the selected chart frame"},
        {"cover.frame_time_for_the_selected_2", "Frame time for the selected chart frame"},
        {"cover.hidden", " · Hidden"},
        {"cover.hide", "Hide"},
        {"cover.hide_layer_v", "Hide layer (V)"},
        {"cover.horizontal_position", "Horizontal position"},
        {"cover.images_png_jpg_jpeg_bmp", "Images (*.png *.jpg *.jpeg *.bmp *.webp)"},
        {"cover.import_cover_layout", "Import cover layout"},
        {"cover.import_layout", "Import layout…"},
        {"cover.import_layout_2", "Import layout"},
        {"cover.import_layout_file", "Import layout file…"},
        {"cover.inner_bg", "Inner bg"},
        {"cover.jacket", "Jacket"},
        {"cover.keep_size_ellipsis", "Keep size, ellipsis (…)"},
        {"cover.layer", "Layer"},
        {"cover.layer_2", "Layer · "},
        {"cover.layer_opacity", "Layer opacity"},
        {"cover.layer_size", "Layer size"},
        {"cover.layers", "Layers"},
        {"cover.layout", "Layout ▾"},
        {"cover.lock", "Lock"},
        {"cover.lock_geometry_l", "Lock geometry (L)"},
        {"cover.lock_position_and_size_l", "Lock position and size (L)"},
        {"cover.long_text", "Long text"},
        {"cover.manage_presets", "Manage presets..."},
        {"cover.manage_presets_2", "Manage presets"},
        {"cover.move_down", "Move down"},
        {"cover.move_up", "Move up"},
        {"cover.no_recent_files", "(No recent files)"},
        {"cover.no_presets", "No saved presets"},
        {"cover.no_difficulty_selected", "No difficulty is selected, so the cover cannot be exported."},
        {"cover.no_output_directory", "Choose an output folder before exporting the cover."},
        {"cover.opacity", "Opacity"},
        {"cover.open", "Open"},
        {"cover.open_recent", "Open recent"},
        {"cover.play_pause_space", "Play / pause (Space)"},
        {"cover.play_pause_visual_only", "Play / pause (visual only)"},
        {"cover.preset_name", "Preset name:"},
        {"cover.pure_chart_frame", "Pure chart frame"},
        {"cover.rename_preset", "Rename preset"},
        {"cover.render_and_save_the_cover", "Render and save the cover image"},
        {"cover.render_level_as_text", "Render level as text"},
        {"cover.reset_canvas_zoom", "Reset canvas zoom"},
        {"cover.reset_canvas_zoom_ctrl_0", "Reset canvas zoom (Ctrl+0)"},
        {"cover.reset_discards_all_current_layers", "Reset discards all current layers and positions. Continue?"},
        {"cover.reset_layout", "Reset layout"},
        {"cover.reset_save_import_recent_layouts", "Reset / save / import / recent layouts"},
        {"cover.reset_to_default", "Reset to default…"},
        {"cover.save_cover_layout", "Save cover layout"},
        {"cover.save_current_as_preset", "Save current as preset..."},
        {"cover.save_layout", "Save layout…"},
        {"cover.save_layout_2", "Save layout"},
        {"cover.save_layout_to_file", "Save layout to file…"},
        {"cover.save_preset", "Save preset"},
        {"cover.select_a_chart_frame_layer", "Select a chart-frame layer to edit its time"},
        {"cover.send_to_back", "Send to back"},
        {"cover.show", "Show"},
        {"cover.show_layer_v", "Show layer (V)"},
        {"cover.show_or_hide_this_layer", "Show or hide this layer (V)"},
        {"cover.shrink_to_fit", "Shrink to fit"},
        {"cover.size", "Size"},
        {"cover.size_2", "Size"},
        {"cover.step_back", "Step back (←)"},
        {"cover.step_forward", "Step forward (→)"},
        {"cover.the_chart_frame_could_not", "The chart frame could not be rendered; the cover will not include it."},
        {"cover.the_custom_background_image_was", "The custom background image was not found; using the chart jacket instead."},
        {"cover.the_imported_layout_included_a_chart_frame", "The imported layout included a chart frame, but this difficulty has no renderable notes; the chart frame was skipped."},
        {"cover.the_layout_file_is_not", "The layout file is not valid JSON."},
        {"cover.this_difficulty_has_no_chart", "This difficulty has no chart notes to render."},
        {"cover.this_file_is_not_a", "This file is not a MiaCode cover layout."},
        {"cover.this_preset_needs_a_renderable", "This preset needs a renderable chart frame"},
        {"cover.transparency", "Transparency"},
        {"cover.transparent", "Transparent"},
        {"cover.unlock", "Unlock"},
        {"cover.unlock_geometry_l", "Unlock geometry (L)"},
        {"cover.vertical_position", "Vertical position"},
        {"cover.visible", "Visible"},
        {"cover.x", "X"},
        {"cover.y", "Y"},
        {"cover.zoom_canvas_in", "Zoom canvas in"},
        {"cover.zoom_canvas_in_ctrl", "Zoom canvas in (Ctrl++)"},
        {"cover.zoom_canvas_out", "Zoom canvas out"},
        {"cover.zoom_canvas_out_ctrl", "Zoom canvas out (Ctrl+-)"},

        {"dialogs.open_folder", "Open Folder"},

        {"document.1_comment_bookmarks", "%1 · comment bookmarks"},
        {"document.1_line_2_double_click", "%1\nLine %2 · double-click to rename"},
        {"document.all_difficulties_share_this_designer", "All difficulties share this designer"},
        {"document.bpm_1_offset_2_s", "BPM %1  ·  Offset %2 s"},
        {"document.clear_all", "Clear all"},
        {"document.delete_1", "Delete %1?"},
        {"document.delete_difficulty", "Delete Difficulty"},
        {"document.deleted_1", "Deleted %1."},
        {"document.deleted_1_changes_are_still", "Deleted %1. Changes are still unsaved."},
        {"document.designer_management", "Designer management"},
        {"document.designers", "Designers"},
        {"document.file_already_exists", "File Already Exists"},
        {"document.latency_settings", "Latency Settings"},
        {"document.ln_1_col_2", "Ln %1, Col %2"},
        {"document.ln_9999_col_9999", "Ln 9999, Col 9999"},
        {"document.maidata_txt_already_exists_in", "maidata.txt already exists in the selected folder. Overwrite it?"},
        {"document.multiple_distinct_designer_names_were_detected", "Multiple distinct designer names were detected. Pick one to use everywhere, or choose \"Clear all\" to empty every field."},
        {"document.no_chart_yet_records_des", "No chart yet — records &des_%1 only."},
        {"document.open_the_export_page_video", "Open the Export page: video / cover / batch / ZIP"},
        {"document.open_toolbox_muri_check_format", "Open toolbox: Muri Check / Format Chart / Official Chart Mirror"},
        {"document.pick_the_canonical_designer", "Pick the canonical designer"},
        {"document.select_chart_folder", "Select Chart Folder"},
        {"document.selection_full_chart", "Selection: full chart"},
        {"document.selection_l_1c_2_l", "Selection: L%1C%2 ~ L%3C%4"},
        {"document.snap_approximately_to_384_grid", "Reduce to 384 subdivisions"},
        {"document.chart_sectioning", "Chart sectioning"},
        {"document.chart_section_every_4_measures", "Every 4 measures"},
        {"document.chart_section_every_2_measures", "Every 2 measures"},
        {"document.chart_section_none", "No sectioning"},
        {"document.subdivision_minus_1", "Subdivision -1"},
        {"document.subdivision_minus_half", "Subdivision -1/2"},
        {"document.subdivision_plus_1", "Subdivision +1"},
        {"document.subdivision_plus_half", "Subdivision +1/2"},
        {"document.toolbox", "Toolbox"},
        {"document.treat_selection_start_as_measure", "Treat selection start as measure boundary"},
        {"document.untitled_bookmark", "Untitled Bookmark"},
        {"document.when_checked_des_and_every", "When checked, &des and every &des_N stay identical."},

        {"export.export_as_zip", "Export as ZIP"},
        {"export.exported_to_1_2_file", "Exported to:\n%1\n\n%2 file(s) included:\n%3"},
        {"export.packaging_1_2_3", "Packaging %1/%2\n%3"},
        {"export.packaging_canceled", "Packaging canceled."},
        {"export.packaging_failed_1", "Packaging failed.\n\n%1"},
        {"export.preparing_package", "Preparing package..."},
        {"export.the_chart_is_empty_there", "The chart is empty; there is nothing to package."},

        {"export_page.batch_export", "Batch Export"},
        {"export_page.export_cover", "Export Cover"},
        {"export_page.export_video", "Export Video"},
        {"export_page.no_difficulty_has_chart_content", "No difficulty has chart content yet, so there is nothing to export."},
        {"export_page.no_difficulty_is_available_to", "No difficulty is available to export."},
        {"export_page.open_composer", "Open Composer… ↗"},
        {"export_page.open_queue", "Open Queue… ↗"},
        {"export_page.pack_as_zip", "Pack as ZIP"},
        {"export_page.pack_now", "Pack Now"},
        {"export_page.the_selected_difficulty_has_no", "The selected difficulty has no chart content to export."},
        {"export_page.the_video_export_panel_is", "The video export panel is unavailable right now."},

        {"latency.auto_detect", "Auto-detect"},
        {"latency.audio_decode_failed", "Audio decoding failed"},
        {"latency.audio_decoder", "Audio decoder"},
        {"latency.back_to_chart_info", "← Back to Chart Info"},
        {"latency.bpm_not_detected", "BPM not detected"},
        {"latency.chart_parameters", "Chart Parameters"},
        {"latency.detected_1", "Detected: %1"},
        {"latency.detected_1_s", "Detected: %1 s"},
        {"latency.offset", "Offset"},
        {"latency.open_audio_video_tools_sample", "Open audio/video tools: sample-rate convert / compress video / prepend silence / prepend black."},
        {"latency.pause", "⏸ Pause"},
        {"latency.requires_a_loaded_track_audio", "Requires a loaded track audio file"},
        {"latency.reset_volume", "Reset volume"},
        {"latency.rhythm_calibration_audition", "Rhythm Calibration Audition"},
        {"latency.s", " s"},
        {"latency.set_or_detect_bpm_first", "Set or detect BPM first"},
        {"latency.sfx_volume", "SFX Volume"},
        {"latency.start_audition", "▶ Start Audition"},
        {"latency.subdivision", "Subdivision:"},
        {"latency.track_audio_missing", "Track audio missing"},

        {"net.also_create_zip_after_success", "Also create ZIP after success"},
        {"net.artist", "Artist"},
        {"net.background_download_thread_started", "Background download thread started."},
        {"net.browse", "Browse..."},
        {"net.cancel_download", "Cancel Download"},
        {"net.canceling", "Canceling..."},
        {"net.chart_complete_1_2", "Chart complete: %1 -> %2"},
        {"net.chart_speed_summary_1_total", "Chart speed summary: %1 total=%2 bytes network=%3ms avg=%4 slowest=%5/%6ms"},
        {"net.choose_output_directory", "Choose Output Directory"},
        {"net.clear_selection", "Clear Selection"},
        {"net.could_not_create_chart_folder", "Could not create chart folder."},
        {"net.designer", "Designer"},
        {"net.done_folder", "Done (folder)"},
        {"net.done_folder_zip", "Done (folder + ZIP)"},
        {"net.download_canceled", "Download canceled."},
        {"net.download_complete_1_succeeded_2", "Download complete: %1 succeeded, %2 failed."},
        {"net.download_complete_with_errors_1", "%1 chart(s) could not be downloaded completely. Incomplete files were not saved. Please check the download log for details."},
        {"net.download_resource_chart_1_resource", "Download resource: chart=%1 resource=%2 attempt=%3"},
        {"net.download_selected", "Download Selected"},
        {"net.downloading", "Downloading..."},
        {"net.downloading_1", "Downloading: %1"},
        {"net.downloading_1_2", "Downloading: %1"},
        {"net.end", "End"},
        {"net.enter_a_user_id_or", "Enter a user ID or tag, choose a date range, then query."},
        {"net.failed_1", "Failed: %1"},
        {"net.found_1_chart_s_from", "Found %1 chart(s) from %2 returned chart(s)."},
        {"net.fuzzy_case_insensitive_match", "Fuzzy case-insensitive match"},
        {"net.hide_log", "Hide Log"},
        {"net.levels", "Levels"},
        {"net.net_batch_download", "Net Batch Download"},
        {"net.net_batch_download_action", "Net Batch Download..."},
        {"net.no_charts_are_selected", "No charts are selected."},
        {"net.not_selected", "Not selected"},
        {"net.output_directory", "Output Directory"},
        {"net.package_failed_1", "Package failed: %1"},
        {"net.packaging_zip", "Packaging ZIP..."},
        {"net.paused", "Paused"},
        {"net.pending", "Pending"},
        {"net.please_choose_a_valid_output", "Please choose a valid output directory."},
        {"net.please_enter_a_user_id", "Please enter a user ID, tag, or song title."},
        {"net.query", "Query"},
        {"net.query_and_download_diagnostics_will", "Query and download diagnostics will appear here."},
        {"net.query_complete_1_ms_api", "Query complete (%1 ms): API returned %2, date filter kept %3, local ID/tag/title filter kept %4."},
        {"net.query_failed", "Query failed."},
        {"net.query_failed_1_ms_2", "Query failed (%1 ms): %2"},
        {"net.querying_net", "Querying Net..."},
        {"net.queue_canceled_1_succeeded_2", "Queue canceled: %1 succeeded, %2 failed."},
        {"net.queue_complete_1_succeeded_2", "Queue complete: %1 succeeded, %2 failed, network total %3 bytes, average %4."},
        {"net.queue_paused_net_cloudflare_blocked", "Queue paused: Net/Cloudflare blocked a request."},
        {"net.queue_paused_net_cloudflare_blocked_2", "Queue paused: Net/Cloudflare blocked a request."},
        {"net.resource_download_failed", "Resource download failed."},
        {"net.resource_result_1_http_2", "Resource result: %1 HTTP=%2 bytes=%3 elapsed=%4ms speed=%5"},
        {"net.retrying_1", "Retrying: %1"},
        {"net.select", "Select"},
        {"net.select_all", "Select All"},
        {"net.show_log", "Show Log"},
        {"net.show_log_2", "Show Log *"},
        {"net.skip_existing_file_1_2", "Skip existing file: %1 (%2 bytes)"},
        {"net.song_title", "Song Title"},
        {"net.start", "Start"},
        {"net.start_chart_1_2", "Start chart: %1 [%2]"},
        {"net.start_download_queue_selected_1", "Start download queue: selected=%1, output=%2, extra ZIP=%3"},
        {"net.start_query_user_1_tag", "Start query: user=%1, tag=%2, title=%3, dates=%4..%5, fuzzy case=%6"},
        {"net.status", "Status"},
        {"net.title", "Title"},
        {"net.uploaded", "Uploaded"},
        {"net.user_id", "User ID"},
        {"net.zip_package_1_2_3", "ZIP package: %1 -> %2 (%3 ms)"},

        {"preferences.auto_closes_brackets_suggests_durations", "Auto-closes brackets, suggests durations/BPMs inside them, and offers [8:1]-style hold tokens after typing 'h'."},
        {"preferences.auto_completion", "Auto-completion"},
        {"preferences.chinese_input", "IME block"},
        {"preferences.conflicts_with_1", "Conflicts with \"%1\""},
        {"preferences.disable_ime", "Block IME"},
        {"preferences.filter_full_width_chars", "Normalize full-width symbols"},
        {"preferences.hides_muri_from_the_editor", "Hides muri from the editor header and timeline dots. Saved in the current chart folder's .miacode data."},
        {"preferences.ignore_muri_issue_prompts", "Ignore muri issue prompts"},
        {"preferences.off", "Off"},
        {"preferences.on", "On"},

        {"shell.follow_code", "Follow Code"},
        {"shell.timeline_measure_line_brightness", "Grid Line Brightness"},
        {"shell.timeline_waveform_brightness", "Waveform Brightness"},
        {"shell.timeline_sync", "Timeline Sync"},
        {"shell.view_lock", "View Lock"},

        {"shortcut.editor.font_decrease", "Decrease Editor Font"},
        {"shortcut.editor.font_increase", "Increase Editor Font"},
        {"shortcut.editor.overwrite_mode", "Overwrite Mode"},
        {"shortcut.file.quit", "Quit"},
        {"shortcut.preview.pause_display_hold", "Flip Judge Area / PV While Paused (Hold)"},
        {"shortcut.preview.play_pause_global", "Play/Pause Preview"},
        {"shortcut.preview.speed_down", "Playback Speed -"},
        {"shortcut.preview.speed_up", "Playback Speed +"},
        {"shortcut.preview.stop_or_play", "Stop or Play Preview"},
        {"shortcut.timeline.zoom_in", "Timeline Zoom In"},
        {"shortcut.timeline.zoom_out", "Timeline Zoom Out"},
        {"shortcut.transform.clear_complete_elements", "Clear Complete Elements"},

        {"timeline.follow_code", "Follow Code"},
        {"timeline.follow_code_tooltip", "During playback, bind the editor cursor to the latest comma at or before preview time"},
        {"timeline.playback_speed", "Playback Speed"},
        {"timeline.progress_follow", "Progress Follow"},
        {"timeline.progress_follow_tooltip", "During playback, keep the timeline view centered on the preview progress line"},
        {"timeline.view_lock", "View Lock"},
        {"timeline.view_lock_tooltip", "Keep the editor cursor near the middle of the code area when possible"},

        {"ui.click_to_type_a_value", "Click to type a value"},

        {"validation.adjust_the_static_tap_on", "Adjust the static Tap-On-Slide reference threshold."},
        {"validation.click_an_icon_to_jump", "Click an icon to jump to its tab"},
        {"validation.copy_info", "Copy Info"},
        {"validation.ignore_this_issue_type", "Ignore This Issue Type"},
        {"validation.issue_info_copied", "Issue info copied."},
        {"validation.jump_to_source", "Jump to Source"},
        {"validation.muri.alert.muri", "Muri"},
        {"validation.muri.alert.warning", "Warning"},
        {"validation.muri.kind.multi_touch", "Multi-touch"},
        {"validation.muri.kind.overlap", "Overlap"},
        {"validation.muri.kind.slide_head_tap", "Outer"},
        {"validation.muri.kind.slide_too_fast", "Inner"},
        {"validation.muri.kind.tap_on_slide", "Tail"},
        {"validation.no_muri_issues_detected", "No muri issues detected."},
        {"validation.no_syntax_errors_detected", "No syntax errors detected."},
        {"validation.stop_ignoring_this_issue_type", "Stop Ignoring This Issue Type"},
        {"validation.tap_on_slide_threshold", "Tap-On-Slide Threshold"},
        {"validation.tap_on_slide_threshold_set", "Tap-On-Slide threshold set to %1 ms."},

        {"video_export.add_intro", "Add intro"},
        {"video_export.cancel_export", "Cancel Export"},
        {"video_export.current_export_range_1_2", "Current export range: [%1, %2], %3 s total."},
        {"video_export.enable_clock_count_1", "Enable clock_count (%1)"},
        {"video_export.export_range", "Export Range"},
        {"video_export.export_range_is_empty", "Export range is empty."},
        {"video_export.export_start_is_out_of", "Export start is out of range."},
        {"video_export.export_video", "Export Video"},
        {"video_export.gameplay", "Gameplay"},
        {"video_export.intro", "Intro"},
        {"video_export.layout_size", "Layout Size"},
        {"video_export.level_text_tooltip", "The baked LV sprites only cover digits 0-9 and \"+\". Tick this to render the level as text when it needs any other character."},
        {"video_export.output", "Output"},
        {"video_export.output_directory_does_not_exist", "Output directory does not exist."},
        {"video_export.please_choose_an_output_path", "Please choose an output path."},
        {"video_export.prepend_the_maimai_track_start", "Prepend the maimai track-start intro to each export."},
        {"video_export.resolution_is_invalid", "Resolution is invalid."},
        {"video_export.show_bottom_left_timestamp", "Show bottom-left timestamp"},
        {"video_export.skin", "Skin"},
        {"video_export.smooth_brightness", "Smooth brightness"},
        {"video_export.start_export", "Start Export"},
        {"video_export.video", "Video"},

        {"window.collapse_left_sidebar", "Collapse left sidebar"},
        {"window.expand_left_sidebar", "Expand left sidebar"},
        {"window.muri", "Muri"},
        {"window.replaced_1_occurrence_s", "Replaced %1 occurrence(s)."},
        {"window.syntax", "Syntax"},
        {"window.timeline", "Timeline"},
        {"window.restore", "Restore"},

        {"track_metadata.artist", "artist"},
        {"track_metadata.bg_jpg_already_exists_overwrite", "bg.jpg already exists. Overwrite?"},
        {"track_metadata.extract_cover_to_bg_jpg", "Extract Cover to bg.jpg"},
        {"track_metadata.failed_to_decode_embedded_cover", "Failed to decode embedded cover (MIME=%1)."},
        {"track_metadata.failed_to_write_bg_jpg", "Failed to write bg.jpg."},
        {"track_metadata.loaded_artist_from_mp3", "Loaded artist from MP3."},
        {"track_metadata.loaded_title_from_mp3", "Loaded title from MP3."},
        {"track_metadata.mp3_audio_mp3_all_files", "MP3 audio (*.mp3);;All files (*.*)"},
        {"track_metadata.no_id3v2_tag_was_found", "No ID3v2 tag was found in the selected MP3."},
        {"track_metadata.overwrote_bg_jpg_with_embedded", "Overwrote bg.jpg with embedded cover from the selected MP3."},
        {"track_metadata.read_artist_from_mp3", "Read Artist from MP3"},
        {"track_metadata.read_title_from_mp3", "Read Title from MP3"},
        {"track_metadata.the_selected_mp3_has_no", "The selected MP3 has no embedded cover artwork."},
        {"track_metadata.the_selected_mp3_s_id3", "The selected MP3's ID3 tag carries no %1."},
        {"track_metadata.title", "title"},
        {"track_metadata.wrote_bg_jpg_from_the", "Wrote bg.jpg from the selected MP3's embedded cover."},

        {"media_tools.1_was_not_found_next", "%1 was not found next to the current chart."},
        {"media_tools.a_black_screen", "a black screen"},
        {"media_tools.audio_video_processing", "Audio/Video Processing"},
        {"media_tools.background_mp4_video", "background .mp4 video"},
        {"media_tools.backup_restored", "Backup restored."},
        {"media_tools.beats", "Beats"},
        {"media_tools.black_screen", "black screen"},
        {"media_tools.cancel", "Cancel"},
        {"media_tools.canceled", "Canceled."},
        {"media_tools.compress_1_under_20_mib", "Compress %1 under 20 MiB and create/replace backup %2?"},
        {"media_tools.compress_the_background_video_under", "Compress the background video under 20 MiB (the original is backed up)."},
        {"media_tools.compress_video", "Compress Video"},
        {"media_tools.compressed_1_under_20_mib", "Compressed %1 under 20 MiB."},
        {"media_tools.compressed_1_under_20_mib_2", "Compressed %1 under 20 MiB (original backed up as %2)."},
        {"media_tools.compressing_video", "Compressing video..."},
        {"media_tools.convert_track_mp3_to_44100", "Convert track.mp3 to 44100 Hz and create/replace backup track_bak.mp3?"},
        {"media_tools.convert_track_mp3_to_44100_2", "Convert track.mp3 to 44100 Hz (the original is backed up)."},
        {"media_tools.converted_track_mp3_to_44100", "Converted track.mp3 to 44100 Hz."},
        {"media_tools.converted_track_mp3_to_44100_2", "Converted track.mp3 to 44100 Hz (original backed up as track_bak.mp3)."},
        {"media_tools.detect", "Detect"},
        {"media_tools.failed_to_restore_backup_to", "Failed to restore backup to: %1\n\nThe file may be open in preview, a media player, File Explorer preview pane, or another program."},
        {"media_tools.failed_to_stage_original_file", "Failed to stage original file for replacement: %1\n\nThe file may still be open in preview, a media player, File Explorer preview pane, or another program. Stop preview and close programs using it, then try again."},
        {"media_tools.compressed_but_replace_failed", "Compression finished, but the original file could not be replaced because it is still in use. The compressed video was saved as:\n%1\nThe original file was left unchanged. Close whatever is using it and replace it by hand, or run this again."},
        {"media_tools.failed_to_write_file_1", "Failed to write file: %1\n\nThe file may be open in preview, a media player, File Explorer preview pane, or another program."},
        {"media_tools.ffmpeg_was_not_found_place", "ffmpeg was not found. Place ffmpeg next to the app or set MIACODE_FFMPEG_PATH."},
        {"media_tools.insert_a_black_screen_at", "Insert a black screen at the start of the background video (the original is backed up)."},
        {"media_tools.insert_silence_at_the_start", "Insert silence at the start of track.mp3 (the original is backed up)."},
        {"media_tools.no_background_mp4_video_was", "No background .mp4 video was found next to the current chart."},
        {"media_tools.open_or_save_a_chart", "Open or save a chart file first."},
        {"media_tools.prepend_pv_black_screen", "Prepend PV Black Screen"},
        {"media_tools.prepend_track_silence", "Prepend Track Silence"},
        {"media_tools.prepended_2_s_of_3", "Prepended %2 s of %3 to %1 (original backed up as %4)."},
        {"media_tools.prepended_2_seconds_of_blank", "Prepended %2 seconds of blank media to %1."},
        {"media_tools.prepends_1_to_2_3", "Prepends %1 to %2: %3 quarter-notes at %4 BPM (~%5 s)."},
        {"media_tools.processing_audio", "Processing audio..."},
        {"media_tools.processing_pv_mp4", "Processing pv.mp4..."},
        {"media_tools.processing_track_mp3", "Processing track.mp3..."},
        {"media_tools.restore_backup", "Restore Backup"},
        {"media_tools.sample_rate", "Sample Rate"},
        {"media_tools.sample_rate_conversion_canceled", "Sample-rate conversion canceled."},
        {"media_tools.silence", "silence"},
        {"media_tools.the_background_video", "the background video"},
        {"media_tools.the_current_video_is_already", "The current video is already under 20 MiB; compression is not needed."},
        {"media_tools.the_current_video_is_already_2", "The current video is already under 20 MiB (%1); compression is not needed."},
        {"media_tools.track_mp3_failed", "track.mp3 Failed"},
        {"media_tools.track_mp3_processing_canceled", "track.mp3 processing canceled."},
        {"media_tools.track_mp3_was_not_found", "track.mp3 was not found next to the current chart."},
        {"media_tools.video_compression_canceled", "Video compression canceled."},
        {"media_tools.video_failed", "Video Failed"},
        {"media_tools.video_processing_canceled", "Video processing canceled."},
        {"media_tools.prepend_track_silence_action", "Prepend Track Silence..."},
        {"media_tools.prepend_pv_black_screen_action", "Prepend PV Black Screen..."},
        {"media_tools.compress_video_action", "Compress Video..."},
        {"media_tools.sample_rate_action", "Sample Rate..."},

        {"net.upload_action", "Net Batch Upload..."},
        {"net.upload_add", "Add"},
        {"net.upload_added_1_total_2", "Added %1 folder(s); %2 in the queue."},
        {"net.upload_assets", "Files"},
        {"net.upload_cancel", "Cancel Upload"},
        {"net.upload_canceled", "Upload canceled."},
        {"net.upload_chart_folder", "Chart Folder"},
        {"net.upload_choose_root_directory", "Choose Chart Root Directory"},
        {"net.upload_complete_1_succeeded_2", "Upload complete: %1 succeeded, %2 failed."},
        {"net.upload_could_not_open_file_1", "Could not open file: %1"},
        {"net.upload_credentials_required", "Enter your Majdata Net username and password."},
        {"net.upload_done", "Uploaded"},
        {"net.upload_found_1_folders", "Found %1 uploadable chart folder(s)."},
        {"net.upload_invalid_root_directory", "Choose a valid chart root directory."},
        {"net.upload_logging_in", "Signing in to Majdata Net..."},
        {"net.upload_login_failed_1", "Majdata Net sign-in failed: %1"},
        {"net.upload_network_error", "Network request failed."},
        {"net.upload_no_folders_scanned", "The upload queue is empty."},
        {"net.upload_password", "Password"},
        {"net.upload_path", "Path"},
        {"net.upload_pending", "Pending upload"},
        {"net.upload_clear_queue", "Clear Queue"},
        {"net.upload_queue", "Upload Queue"},
        {"net.upload_queue_total_1", "%1 folder(s) in the upload queue."},
        {"net.upload_remove_selected", "Remove Selected"},
        {"net.upload_remember_credentials", "Remember account and password"},
        {"net.upload_request_timed_out", "Request timed out."},
        {"net.upload_root_directory", "Chart Root"},
        {"net.upload_scan", "Scan"},
        {"net.upload_selected", "Upload Selected"},
        {"net.upload_title", "Net Batch Upload"},
        {"net.upload_uploading", "Uploading..."},
        {"net.upload_uploading_1", "Uploading: %1"},
        {"net.upload_username", "Username"},
        {"net.upload_batch_stopped_1", "Upload batch stopped: %1"},
        {"net.upload_cloudflare_challenge", "Cloudflare challenge or access block detected"},
        {"net.upload_could_not_open_file_1_2", "Could not open file: %1 (%2)"},
        {"net.upload_detail_attempt_1", "Attempt %1"},
        {"net.upload_detail_chart_1", "Chart: %1"},
        {"net.upload_detail_cloudflare_challenge", "Diagnosis: Cloudflare challenge/access block; the batch was stopped to avoid repeated blocked requests."},
        {"net.upload_detail_directory_1", "Directory: %1"},
        {"net.upload_detail_http_1_2", "HTTP: %1 %2"},
        {"net.upload_detail_local_error_1", "Local file error: %1"},
        {"net.upload_detail_network_1", "Network error: %1"},
        {"net.upload_detail_payload_too_large", "Diagnosis: the server rejected an oversized upload request (HTTP 413), usually because the PV video is too large. Compress or remove the video and retry. Other charts will continue uploading."},
        {"net.upload_detail_rate_limited", "Diagnosis: rate limited by the server or Cloudflare."},
        {"net.upload_detail_response_body", "Server response:"},
        {"net.upload_detail_stage_1", "Stage: %1"},
        {"net.upload_detail_url_1", "URL: %1"},
        {"net.upload_failure_details_available", "Expand Details to view and copy the complete failure diagnostics."},
        {"net.upload_failed_count_1", "%1 chart(s) failed to upload."},
        {"net.upload_not_uploaded_batch_stopped", "Not uploaded (batch stopped)"},
        {"net.upload_payload_too_large", "Upload content is too large (HTTP 413); compress or remove the video"},
        {"net.upload_rate_limit_retry_exhausted", "Rate limit remained after one delayed retry"},
        {"net.upload_rate_limited", "Request rate limited"},
        {"net.upload_rate_limited_retrying_1", "Rate limited; retrying in %1 s"},
        {"net.upload_retry_failed", "Retry Failed Charts"},
        {"net.upload_stage_local_file", "Open local upload file"},
        {"net.upload_stage_login", "Majdata Net sign-in"},
        {"net.upload_stage_upload", "Chart upload"},
        {"net.upload_waiting_before_next_1", "Next upload starts in %1 s"},
        {"media_tools.batch_pv_add", "Add"},
        {"media_tools.batch_pv_added_1_total_2", "Added %1 folder(s); %2 folder(s) in the list."},
        {"media_tools.batch_pv_already_small", "Already under 20 MiB; not compressed"},
        {"media_tools.batch_pv_backup_failed", "Could not create or replace the video backup."},
        {"media_tools.batch_pv_cancel", "Cancel Compression"},
        {"media_tools.batch_pv_canceled", "Batch compression canceled."},
        {"media_tools.batch_pv_chart_folder", "Folder"},
        {"media_tools.batch_pv_choose_folder", "Choose a Folder to Scan for Videos"},
        {"media_tools.batch_pv_clear_queue", "Clear Queue"},
        {"media_tools.batch_pv_complete_1_2", "Batch compression complete: %1 succeeded, %2 failed."},
        {"media_tools.batch_pv_compressing", "Compressing..."},
        {"media_tools.batch_pv_compressing_1", "Compressing: %1"},
        {"media_tools.batch_pv_confirm_1", "Compress all %1 videos in the list? Originals will be backed up as bg_bak.mp4 or pv_bak.mp4."},
        {"media_tools.batch_pv_description", "Scan the selected folder and its immediate subfolders for bg.mp4 or pv.mp4, then compress videos under 20 MiB."},
        {"media_tools.batch_pv_done_1", "Compression succeeded (%1)"},
        {"media_tools.batch_pv_duration_failed", "Could not read the video duration."},
        {"media_tools.batch_pv_empty", "The video compression list is empty."},
        {"media_tools.batch_pv_ffmpeg_failed_1", "FFmpeg failed: %1"},
        {"media_tools.batch_pv_ffmpeg_missing", "FFmpeg was not found."},
        {"media_tools.batch_pv_file", "File"},
        {"media_tools.batch_pv_folder", "Folder"},
        {"media_tools.batch_pv_invalid_file", "Invalid or empty video file"},
        {"media_tools.batch_pv_invalid_folder", "Choose a valid folder."},
        {"media_tools.batch_pv_original_size", "Original Size"},
        {"media_tools.batch_pv_output_invalid", "Output was not smaller than the original and under 20 MiB."},
        {"media_tools.batch_pv_path", "Path"},
        {"media_tools.batch_pv_pending", "Pending"},
        {"media_tools.batch_pv_no_video", "No video"},
        {"media_tools.batch_pv_failed_1", "Compression failed: %1"},
        {"media_tools.batch_pv_queue_total_1", "%1 folder(s) in the compression list."},
        {"media_tools.batch_pv_remove_selected", "Remove Selected"},
        {"media_tools.batch_pv_replace_failed_1", "Compressed, but replacement failed; output kept at %1"},
        {"media_tools.batch_pv_start", "Compress Videos"},
        {"media_tools.batch_pv_title", "Batch Compress Videos"},
    };
    return map;
}

const QHash<QString, QString>& zhMap()
{
    static const QHash<QString, QString> map{
        {"menu.file", "文件(&F)"},
        {"menu.tools", "工具(&T)"},
        // Beta20-fix — was 变换(&T) which collided with 工具(&T) on the
        // Alt+T accelerator. Renamed to 调整 (synonym for Transform in
        // chart-editing context: rotate / mirror / normalise / break-
        // toggle etc. are all "adjustments" to the chart) with mnemonic
        // M. Mirrors English "Modify(&M)".
        {"menu.transform", "调整(&M)"},
        {"menu.help", "帮助(&H)"},
        {"menu.bpm_latency", "BPM && 延迟检测"},
        {"menu.clear_elements", "一键清空"},
        {"menu.export_as_zip", "导出为ZIP"},
        {"menu.find_replace", "查找/替换"},
        {"menu.format_chart", "谱面整理"},
        {"menu.official_chart_mirror", "官谱镜像站"},
        {"menu.preview_mode_chart_review", "预览模式：谱面确认"},
        {"menu.preview_mode_muri_check", "预览模式：无理检测"},
        {"menu.preview_mode_erase_by_area", "预览模式：按区消去"},
        {"menu.simaiwiki", "simaiwiki"},
        {"menu.swap_side_panels", "左右面板互换"},
        {"menu.tap_on_slide_threshold", "撞尾阈值..."},

        {"action.new", "新建"},
        {"action.open", "打开"},
        {"action.open_folder", "打开文件夹"},
        {"action.open_current_folder", "打开当前文件夹"},
        {"action.open_recent", "打开最近的文件"},
        {"action.open_recent.empty", "没有最近的文件"},
        {"dialog.open_startup_folder.missing_maidata.title", "未找到 maidata.txt"},
        {"dialog.open_startup_folder.missing_maidata.message", "拖入的文件夹中未找到 maidata.txt：\n%1"},
        {"dialog.open_startup_target.missing.title", "打开失败"},
        {"dialog.open_startup_target.missing.message", "拖入的文件或文件夹不存在：\n%1"},
        {"action.save", "保存"},
        {"action.ok", "确定"},
        {"action.discard", "放弃"},
        {"action.cancel", "取消"},
        {"drop_chart.create_chart_file", "创建谱面文件"},
        {"drop_chart.drop_hint", "松开鼠标以创建新的谱面工程"},
        {"drop_chart.preview.title", "准备创建谱面"},
        {"drop_chart.preview.message", "将在以下位置创建 %1 个谱面：\n\n"},
        {"drop_chart.preview.create", "创建 %1 个谱面"},
        {"drop_chart.error.title", "谱面创建失败"},
        {"drop_chart.create_failed", "谱面创建失败，未保留不完整的谱面工程。"},
        {"drop_chart.created_title", "谱面创建完成"},
        {"drop_chart.confirm_switch", "是否打开刚创建的第一个谱面？（共 %1 个）"},
        {"drop_chart.created_chart", "已创建 %1 个新谱面。"},
        {"drop_chart.created_with_failures", "已创建 %1 个谱面，但有 %2 个创建失败。未保留不完整的谱面工程。"},
        {"action.close", "关闭"},
        {"action.yes", "是"},
        {"action.no", "否"},
        {"action.browse", "浏览..."},
        {"action.save_as", "另存为"},
        {"action.preferences", "首选项..."},
        {"action.restore_backup", "从备份恢复"},
        {"action.restore_backup.empty", "没有可用备份"},
        {"dialog.restore_backup.title", "从备份恢复"},
        {"dialog.restore_backup.missing", "备份文件不存在：\n%1"},
        {"dialog.restore_backup.read_failed", "无法读取备份文件：\n%1"},
        {"dialog.restore_backup.confirm", "是否恢复 %1 的备份？"},
        {"dialog.restore_backup.abnormal_exit_confirm", "MiaCode 上次未正常退出。\n\n是否恢复 %1 的备份？"},
        {"status.restore_backup.loaded", "已从备份恢复，请保存以保留更改。"},
        {"action.about", "关于"},
        {"dialog.invalid_star_preview.title", "隐藏选项"},
        {"dialog.invalid_star_preview.enable", "非法星星预览模式已开启，仅本次程序运行有效。"},
        {"dialog.invalid_star_preview.already_enabled", "非法星星预览模式已经开启，且仅本次程序运行有效。"},
        {"dialog.invalid_star_preview.enabled_status", "非法星星预览模式已开启（仅本次运行有效）。"},
        {"action.cut", "剪切"},
        {"action.copy", "复制"},
        {"action.paste", "粘贴"},
        {"action.undo", "撤回"},
        {"action.redo", "重做"},
        {"action.stop_preview", "停止"},
        {"action.pause_preview", "播放/暂停预览"},
        {"action.preview_speed_down", "播放速度 ↓"},
        {"action.preview_speed_up", "播放速度 ↑"},
        {"action.audio_settings", "音频设置"},
        {"action.video_settings", "预览设置"},
        {"action.skin_settings", "皮肤"},
        {"toolbar.skin", "皮肤"},
        {"toolbar.export", "导出"},
        {"action.export_chart", "导出"},
        {"action.export_cover", "导出封面"},
        {"action.batch_export", "批量导出"},
        {"action.transform.mirror_lr", "左右镜像"},
        {"action.transform.mirror_ud", "上下镜像"},
        {"action.transform.rotate_180", "旋转 180°"},
        {"action.transform.rotate_ccw_45", "逆时针旋转 45°"},
        {"action.transform.rotate_cw_45", "顺时针旋转 45°"},
        {"action.transform.more", "更多..."},
        {"action.transform.toggle_break", "一键全 Break"},
        {"action.transform.toggle_ex", "一键全 Ex"},
        {"action.transform.toggle_firework", "一键全 Firework"},
        {"action.transform.random_rotate", "一键全随机"},
        {"context.batch_transform", "批量操作"},
        {"context.more_transform", "更多..."},

        {"toolbar.settings_placeholder", "设置"},

        {"preview.play", "播放"},
        {"preview.pause", "暂停"},
        {"preview.fullscreen.window_title", "全屏预览"},
        {"preview.fullscreen.exit_hint", "按 Esc 退出全屏"},
        {"preview.fullscreen.exit_tooltip", "退出全屏预览（Esc）"},
        {"preview.fullscreen.enter_tooltip", "打开全屏预览"},
        {"preview.canvas.free_aspect", "自由比例"},
        {"preview.canvas.menu_tooltip", "预览画布"},
        {"preview.canvas.menu_description", "打开预览画布菜单"},

        {"editor.metadata", "谱面信息设置"},
        {"editor.welcome", "欢迎使用MiaCode！"},
        {"editor.des", "谱师"},
        {"editor.export", "导出"},
        {"editor.delete_bookmark", "删除书签"},
        {"editor.delete_bookmark_1_this_will", "确定删除书签“%1”吗？这会删除该行的谱面注释。"},
        {"editor.l_1", "第 %1 行"},
        {"editor.new_bookmark", "新书签"},
        {"editor.target_line_already_has_a", "目标行已经有注释，已取消移动书签。"},
        {"editor.untitled_bookmark", "未命名书签"},

        {"metadata.information", "基础信息"},
        {"metadata.other_fields", "其他 &xx 字段"},
        {"metadata.field.title", "标题"},
        {"metadata.field.artist", "曲师"},
        {"metadata.field.first", "偏移"},
        {"metadata.field.des", "谱师"},
        {"metadata.field.cover", "曲绘"},
        {"metadata.choose_an_mp3_and_pull", "选择一个 MP3，从它的 ID3 标签里读取标题。"},
        {"metadata.choose_an_mp3_and_pull_2", "选择一个 MP3，从它的 ID3 标签里读取曲师。"},
        {"metadata.choose_an_mp3_and_write", "选择一个 MP3，把它内嵌的封面图写到当前谱面目录的 bg.jpg。"},
        {"metadata.click_to_open_the_muri", "点击跳转到「无理」选项卡"},
        {"metadata.click_to_open_the_syntax", "点击跳转到「语法」选项卡"},
        {"metadata.close", "关闭查找栏"},
        {"metadata.copy_area", "复制区"},
        {"metadata.copy_area_2", "复制区"},
        {"metadata.delete", "删除"},
        {"metadata.delete_1", "删除 %1"},
        {"metadata.edit_e", "编辑(&E)"},
        {"metadata.empty_hint", "← 点击添加谱面难度"},
        {"metadata.find", "查找"},
        {"metadata.find_next", "查找下一个"},
        {"metadata.find_previous", "查找上一个"},
        {"metadata.full_copy_area", "完整复制区"},
        {"metadata.insert_bookmark", "插入书签"},
        {"metadata.jump_to_timeline_position", "跳到时间轴位置"},
        {"metadata.latency_card.title", "延迟与偏移校准"},
        {"metadata.latency_card.open", "打开延迟设置 →"},
        {"metadata.ln_1_col_1", "1行 1列"},
        {"metadata.manage_per_difficulty_designers", "管理多个难度名义"},
        {"metadata.open_the_latency_settings_page", "打开延迟设置页：调整 BPM/Offset，并通过试听校准。"},
        {"metadata.preview_p", "预览(&P)"},
        {"metadata.read_from_mp3", "从 MP3 读取"},
        {"metadata.rename", "重命名"},
        {"metadata.rename_bookmark", "重命名书签"},
        {"metadata.replace", "替换"},
        {"metadata.replace_all", "全部替换"},
        {"metadata.set_each_difficulty_designer", "为每个难度（&des_1 … &des_7）分别填写谱师名义，并可勾选「所有难度采用相同名义」。"},
        {"metadata.show_in_sidebar", "在侧边栏显示"},

        {"sidebar.metadata", "谱面信息设置"},
        {"sidebar.add_difficulty", "添加难度"},
        {"sidebar.export", "导出"},

        {"tab.timeline", "时间轴"},
        {"tab.validation_errors", "校验错误"},

        {"dialog.preferences.title", "首选项"},
        {"dialog.preferences.interface_group", "外观"},
        {"dialog.preferences.background_group", "背景"},
        {"dialog.preferences.background.enabled", "启用背景"},
        {"dialog.preferences.background.image", "图片"},
        {"dialog.preferences.background.choose", "选择..."},
        {"dialog.preferences.background.clear", "清除"},
        {"dialog.preferences.background.image_filter", "图片 (*.png *.jpg *.jpeg *.bmp *.webp);;所有文件 (*)"},
        {"dialog.preferences.background.image_error", "所选图片无法读取。"},
        {"dialog.preferences.background.save_error", "无法保存背景设置。"},
        {"dialog.preferences.background.file_picker_unavailable", "图片选择器不可用。"},
        {"dialog.preferences.background.opacity", "不透明度"},
        {"dialog.preferences.background.overlay", "遮盖度"},
        {"dialog.preferences.background.overlay_button", "遮盖度..."},
        {"dialog.preferences.background.overlay_dialog_title", "背景遮盖度"},
        {"dialog.preferences.background.overlay.toolbar", "顶部菜单与工具栏"},
        {"dialog.preferences.background.overlay.status", "窗口底部状态栏"},
        {"dialog.preferences.background.overlay.panel", "预览外框"},
        {"dialog.preferences.background.overlay.editor_header", "谱面编辑页标题栏"},
        {"dialog.preferences.background.overlay.input", "输入框和文本框"},
        {"dialog.preferences.background.overlay.code_editor", "代码编辑区"},
        {"dialog.preferences.background.overlay.dark", "%1（深色）"},
        {"dialog.preferences.background.overlay.light", "%1（浅色）"},
        {"dialog.preferences.background.overlay.reset_defaults", "还原到默认"},
        {"dialog.preferences.background.blur", "模糊"},
        {"dialog.preferences.background.scale", "缩放"},
        {"dialog.preferences.background.scale.cover", "覆盖"},
        {"dialog.preferences.background.scale.contain", "完整显示"},
        {"dialog.preferences.background.scale.stretch", "拉伸"},
        {"dialog.preferences.background.scale.center", "居中"},
        {"dialog.preferences.background.scale.repeat", "平铺"},
        {"dialog.preferences.background.position", "位置"},
        {"dialog.preferences.background.position.center", "居中"},
        {"dialog.preferences.background.position.left", "左"},
        {"dialog.preferences.background.position.right", "右"},
        {"dialog.preferences.background.position.top", "上"},
        {"dialog.preferences.background.position.bottom", "下"},
        {"dialog.preferences.background.position.left_top", "左上"},
        {"dialog.preferences.background.position.right_top", "右上"},
        {"dialog.preferences.background.position.left_bottom", "左下"},
        {"dialog.preferences.background.position.right_bottom", "右下"},
        {"dialog.preferences.language", "语言"},
        {"dialog.preferences.language.system", "跟随系统"},
        {"dialog.preferences.language.english", "English"},
        {"dialog.preferences.language.chinese", "简体中文"},
        {"dialog.preferences.language.japanese", "日本語"},
        {"dialog.preferences.theme", "主题"},
        {"dialog.preferences.theme.system", "跟随系统"},
        {"dialog.preferences.theme.light", "浅色"},
        {"dialog.preferences.theme.dark", "深色"},
        {"dialog.preferences.preview_side", "预览位置"},
        {"dialog.preferences.preview_side.right", "右"},
        {"dialog.preferences.preview_side.left", "左"},

        {"dialog.welcome.title", "欢迎使用 MiaCode"},
        {"dialog.welcome.heading", "欢迎使用 MiaCode！"},
        {"dialog.welcome.subtitle", "选择工作区的外观，之后可随时在“首选项”（顶部菜单的齿轮图标）中重新调整。"},
        {"dialog.welcome.preview.editor", "编辑器"},
        {"dialog.welcome.preview.preview", "预览区"},
        {"dialog.welcome.preview_side", "预览区位置"},
        {"dialog.welcome.preview_side.right", "在右侧"},
        {"dialog.welcome.preview_side.left", "在左侧"},
        {"dialog.welcome.theme", "颜色主题"},
        {"dialog.welcome.theme.light", "浅色"},
        {"dialog.welcome.theme.dark", "深色"},
        {"dialog.welcome.chinese_input", "输入法禁止"},
        {"dialog.welcome.chinese_input.disable", "禁止输入法"},
        {"dialog.welcome.chinese_input.enable", "允许输入法"},
        {"dialog.welcome.chinese_input.fullwidth", "转换全角符号"},
        {"dialog.welcome.chinese_input.hint",
         "如果您不使用注释功能，推荐选择“禁止输入法”；\n"
         "如果您使用注释功能，且希望减少类似 1h【8:1】 的全角符号误输入，推荐选择“转换全角符号”。"},
        {"dialog.welcome.get_started", "开始使用"},
        {"dialog.preferences.editor_group", "编辑器"},
        {"dialog.preferences.editor_font_size", "字号"},
        {"dialog.preferences.editor_line_spacing", "行距"},
        {"dialog.preferences.editor_half_width_input", "锁定半角符号输入"},
        {"dialog.preferences.editor_auto_completion", "自动补全括号"},
        {"dialog.preferences.editor_ime_input_disabled", "禁止输入法输入"},
        {"dialog.preferences.performance_group", "性能"},
        {"dialog.preferences.shortcuts_group", "快捷键"},
        {"dialog.preferences.extensions_group", "扩展"},
        {"dialog.preferences.extensions.open_folder", "打开扩展文件夹"},
        {"dialog.preferences.extensions.refresh", "刷新扩展"},
        {"dialog.preferences.extensions.open_logs", "打开日志位置"},
        {"dialog.preferences.extensions.devtools", "DevTools 面板"},
        {"dialog.preferences.extensions.devtools.summary", "快照摘要"},
        {"dialog.preferences.extensions.devtools.api_count", "API"},
        {"dialog.preferences.extensions.devtools.extension_count", "扩展"},
        {"dialog.preferences.extensions.devtools.raw_count", "实验 raw"},
        {"dialog.preferences.extensions.devtools.callback_count", "事件回调"},
        {"dialog.preferences.extensions.devtools.call_count", "最近调用"},
        {"dialog.preferences.extensions.devtools.diagnostic_count", "诊断"},
        {"dialog.preferences.extensions.devtools.api", "API 注册表"},
        {"dialog.preferences.extensions.devtools.open_bridge", "Open Bridge"},
        {"dialog.preferences.extensions.devtools.recent_calls", "最近调用"},
        {"dialog.preferences.extensions.devtools.ui", "UI 贡献"},
        {"dialog.preferences.extensions.devtools.diagnostics", "诊断"},
        {"dialog.preferences.extensions.devtools.raw_json", "原始 JSON"},
        {"dialog.preferences.extensions.devtools.refresh", "刷新快照"},
        {"dialog.preferences.extensions.enabled", "启用"},
        {"dialog.preferences.extensions.name", "扩展"},
        {"dialog.preferences.extensions.version", "版本"},
        {"dialog.preferences.extensions.contributions", "贡献"},
        {"dialog.preferences.extensions.permissions", "权限"},
        {"dialog.preferences.extensions.status", "状态"},
        {"extension.menu.extensions", "扩展"},
        {"extension.menu.empty", "没有扩展命令"},
        {"extension.dialog.permission_title", "扩展权限"},
        {"extension.dialog.permission_message", "扩展 %1 请求高风险权限：\n%2\n\n目标：%3\n\n是否允许并记住此授权？"},
        {"extension.dialog.input_title", "扩展输入"},
        {"extension.dialog.quick_pick_title", "扩展选择"},
        {"extension.dialog.canceled", "已取消。"},
        {"extension.command.unavailable",
         "扩展命令不可用：%1\n\n可能原因：该扩展已禁用、manifest 无效，或刷新后该命令已经不存在。"},
        {"extension.command.runtime_not_running",
         "扩展运行时未启动，MiaCode 无法运行：%1\n\n可能原因：该扩展没有可加载的 JS 入口、激活失败，或需要刷新扩展。"},
        {"extension.command.failed", "扩展命令未正常响应：%1\n\n原因：%2"},
        {"extension.command.ran", "扩展命令已运行：%1"},
        {"editor.bookmark.insert", "插入书签"},
        {"editor.bookmark.rename", "重命名书签"},
        {"editor.bookmark.delete", "删除书签"},
        {"editor.bookmark.show_in_sidebar", "在侧边栏显示"},
        {"editor.bookmark.untitled", "未命名书签"},
        {"editor.bookmark.delete_confirm", "确定删除书签“%1”吗？这会删除该行的谱面注释。"},
        {"dialog.preferences.shortcuts.edit", "修改快捷键"},
        {"dialog.preferences.shortcuts.reset", "还原快捷键"},
        {"dialog.preferences.shortcuts.title", "键盘快捷方式"},
        {"dialog.preferences.shortcuts.command", "命令"},
        {"dialog.preferences.shortcuts.keybinding", "键绑定"},
        {"dialog.preferences.shortcuts.change", "更改键绑定"},
        {"dialog.preferences.shortcuts.capture_title", "更改键绑定"},
        {"dialog.preferences.shortcuts.capture_prompt", "先按所需的组合键或鼠标滚轮手势，再按 Enter 键。"},
        {"dialog.preferences.shortcuts.capture_prompt_hold", "按下要用于按住的按键（可以是单个修饰键，如 Alt），再按 Enter 键。"},
        {"dialog.preferences.shortcuts.reset_confirm_title", "还原快捷键"},
        {"dialog.preferences.shortcuts.reset_confirm_message", "是否将所有可修改快捷键还原为默认值？"},
        {"action.reset", "还原"},
        {"dialog.preferences.restart_title", "需要重启"},
        {"dialog.preferences.restart_message", "语言设置已保存。请重启 MiaCode 以应用菜单、字体和界面文本。"},
        {"dialog.unsaved_changes.title", "未保存的更改"},
        {"dialog.unsaved_changes.message", "当前文档有未保存的更改。是否先保存？"},
        {"dialog.unsaved_field_changes.title", "未保存的字段更改"},
        {"dialog.unsaved_field_changes.message", "%1 有未保存的更改。切换前是否保存？"},
        {"dialog.unsaved_field_changes.field.metadata", "谱面信息"},
        {"dialog.unsaved_tab_changes.title", "标签有未保存的更改"},
        {"dialog.unsaved_tab_changes.message", "%1 有未保存的更改。关闭此标签前是否保存？"},
        {"dialog.unsaved_tab_changes.details.difficulty", "保存只写入这个难度；放弃将其还原到上次保存时的内容。"},
        {"dialog.unsaved_tab_changes.details.document", "保存会写入整个文档；放弃将文档还原到上次保存时的内容。"},

        {"dialog.audio_settings.title", "音频设置"},
        {"dialog.video_settings.title", "预览设置"},
        {"dialog.render_settings.audio_group", "音频"},
        {"dialog.render_settings.video_group", "视频"},
        {"dialog.render_settings.gameplay_group", "游戏"},
        {"dialog.render_settings.music_group", "音乐"},
        {"dialog.render_settings.performance_group", "性能"},
        {"dialog.render_settings.preview_group", "预览"},

        {"dialog.skin_settings.title", "皮肤"},
        {"dialog.skin_settings.dialog_title", "皮肤设置"},
        {"dialog.skin_settings.section.chart_skin", "谱面皮肤"},
        {"dialog.skin_settings.open_skin_folder", "打开皮肤文件夹"},
        {"dialog.skin_settings.open_judge_line_folder", "打开判定线文件夹"},
        {"dialog.skin_settings.open_directory", "打开目录"},
        {"dialog.skin_settings.chart_effect", "判定效果"},
        {"dialog.skin_settings.chart_effect.standard", "SD"},
        {"dialog.skin_settings.chart_effect.starry", "DX"},
        {"dialog.render_settings.button.close", "关闭"},
        {"dialog.render_settings.button.set_software_default_audio", "保存为本地预设"},
        {"dialog.render_settings.button.restore_project_default", "应用本地预设"},
        {"dialog.render_settings.button.mute_non_bgm", "除 BGM 外静音"},
        {"dialog.render_settings.button.restore_non_bgm", "恢复非 BGM 音量"},
        {"dialog.render_settings.option.enabled", "开启"},
        {"dialog.render_settings.option.disabled", "关闭"},
        {"dialog.render_settings.audio.master", "全局音量"},
        {"dialog.render_settings.audio.bgm", "BGM 音量"},
        {"dialog.render_settings.audio.answer", "Answer 音量"},
        {"dialog.render_settings.audio.tap", "Tap 音量"},
        {"dialog.render_settings.audio.break", "Break 音量"},
        {"dialog.render_settings.audio.break_slide", "Break Slide 音量"},
        {"dialog.render_settings.audio.slide", "Slide 音量"},
        {"dialog.render_settings.audio.break_slide_tail_cheer_mute", "关闭breakslide结尾“欢呼”声"},
        {"dialog.render_settings.audio.ex", "EX 音量"},
        {"dialog.render_settings.audio.touch", "Touch 音量"},
        {"dialog.render_settings.audio.track", "Track 音量"},
        {"dialog.render_settings.audio.global", "Global 音量"},
        {"dialog.render_settings.audio.firework", "Firework 音量"},
        {"dialog.render_settings.audio.button.mute", "静音 %1"},
        {"dialog.render_settings.audio.button.unmute", "恢复 %1"},
        {"dialog.render_settings.music.intro_sound", "片头音效"},
        {"dialog.render_settings.music.intro_sound_volume", "片头音量"},
        {"dialog.render_settings.music.default_intro_sound", "默认片头音效"},
        {"dialog.render_settings.music.audition", "试听"},
        {"dialog.render_settings.music.open_folder", "打开文件夹"},
        {"dialog.render_settings.music.open_folder.tooltip", "在此放入片头音效文件；缺失时依次读取 assets/music/track_start.wav 与 assets/SFX/track_start.wav。"},
        {"dialog.render_settings.video.brightness", "背景/PV 亮度"},
        {"dialog.render_settings.video.scale.fill", "填充（必要时裁切）"},
        {"dialog.render_settings.video.scale.fit", "适应（完整显示）"},
        {"dialog.render_settings.video.scale.square_fit", "1:1适应（居中方框）"},
        {"dialog.render_settings.video.scale.inner_circle_fit_outer_fill", "内圈1:1适应 + 外圈填充"},
        {"dialog.render_settings.video.canvas_aspect.square", "1:1（正方形）"},
        {"dialog.render_settings.video.canvas_aspect.4_3", "4:3"},
        {"dialog.render_settings.video.canvas_aspect.16_9", "16:9"},
        {"dialog.render_settings.video.auto_restore_square", "导出后自动恢复 1:1"},
        {"dialog.render_settings.video.smooth_brightness", "平滑亮度"},
        {"dialog.render_settings.video.touch_pad_authoring_shortcut", "启用touch点击输入"},
        {"dialog.render_settings.video.brightness_outer", "亮度（外侧）"},
        {"dialog.render_settings.video.brightness_inner", "亮度（内侧）"},
        {"dialog.render_settings.video.layout_square_scale", "谱面显示范围"},
        {"dialog.render_settings.video.flow_speed", "流速"},
        {"dialog.render_settings.video.tap_flow_speed", "Tap流速"},
        {"dialog.render_settings.video.touch_flow_speed", "Touch流速"},
        {"dialog.render_settings.video.skin", "皮肤"},
        {"dialog.render_settings.video.skin.standard", "SD"},
        {"dialog.render_settings.video.skin.dx", "DX"},
        {"dialog.render_settings.video.skin.import", "导入..."},
        {"dialog.render_settings.video.scale_mode", "背景 / PV 缩放模式"},
        {"dialog.render_settings.video.canvas_aspect", "预览画布比例"},
        {"dialog.render_settings.gameplay.judge_effect", "判定效果显示"},
        {"dialog.render_settings.gameplay.judge_effect.slide", "slide"},
        {"dialog.render_settings.gameplay.judge_effect.tap", "tap"},
        {"dialog.render_settings.gameplay.judge_effect.break", "break"},
        {"dialog.render_settings.gameplay.judge_effect.touch", "touch"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance", "Tap判定文字距离"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance.inner", "里"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance.middle", "中"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance.outer", "外"},
        {"dialog.render_settings.gameplay.judge_line", "判定线"},
        {"dialog.render_settings.gameplay.judge_line.point", "点"},
        {"dialog.render_settings.gameplay.judge_line.line", "线"},
        {"dialog.render_settings.gameplay.judge_line.area", "判定区"},
        {"dialog.render_settings.gameplay.judge_line.area_labeled", "判定区（带编号）"},
        {"dialog.render_settings.gameplay.judge_line.import", "导入..."},
        {"dialog.render_settings.gameplay.center_display", "中心显示"},
        {"dialog.render_settings.gameplay.slide_stack_order", "slide层叠顺序"},
        {"dialog.render_settings.gameplay.slide_stack_order.dx_style", "DX风格"},
        {"dialog.render_settings.gameplay.slide_stack_order.finale_style", "FiNALE风格"},
        {"dialog.render_settings.preview.debug", "显示预览调试信息"},
        {"dialog.render_settings.preview.canvas_frame_rate", "预览刷新率"},
        {"dialog.render_settings.preview.canvas_frame_rate.30", "30 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.display", "屏幕最大刷新率"},
        {"dialog.preferences.performance.pv_frame_rate", "PV刷新率"},
        {"dialog.preferences.performance.timeline_frame_rate", "时间轴刷新率"},
        {"dialog.preferences.performance.video_decode", "PV渲染"},
        {"dialog.preferences.performance.video_decode.hardware", "硬件渲染"},
        {"dialog.preferences.performance.video_decode.software", "软件渲染"},
        {"dialog.render_settings.preview.show_object_stats", "预览/导出显示物件统计"},
        {"dialog.render_settings.preview.show_validation_summary", "显示头部错误/警告摘要"},
        {"editor.validation_summary.tooltip_with_muri", "%1 个错误，%2 个警告，%3 条无理"},
        {"dialog.render_settings.preview.show_object_stats_preview", "预览显示物件统计"},
        {"dialog.render_settings.preview.show_object_stats_export", "导出显示物件统计"},

        {"dialog.video_export.title", "导出视频"},
        {"dialog.video_export.output", "输出"},
        {"dialog.video_export.browse", "浏览..."},
        {"dialog.video_export.resolution", "分辨率"},
        {"dialog.video_export.fps", "帧率"},
        {"dialog.video_export.audio_bitrate", "音频码率"},
        {"dialog.video_export.preset", "导出质量"},
        {"dialog.video_export.preset.fast", "快速"},
        {"dialog.video_export.preset.high_quality", "高质量"},
        {"dialog.video_export.size_preset", "文件体积"},
        {"dialog.video_export.size_preset.standard", "标准"},
        {"dialog.video_export.size_preset.compact", "较小"},
        {"dialog.video_export.size_preset.ultra_compact_with_pv", "极小（带 PV）"},
        {"dialog.video_export.size_preset.ultra_compact", "极小（无 PV）"},
        {"dialog.video_export.section.options", "选项"},
        {"dialog.video_export.section.font", "字体"},
        {"dialog.video_export.section.range", "导出区间"},
        {"dialog.video_export.range.start", "开始"},
        {"dialog.video_export.range.end", "结束"},
        {"dialog.video_export.range.set_left", "← 设定"},
        {"dialog.video_export.range.set_end", "设定结束"},
        {"dialog.video_export.range.set_current", "设为当前值"},
        {"dialog.video_export.range.set_current.tip", "设为当前预览位置"},
        {"dialog.video_export.range.intro_tag", "含片头"},
        {"dialog.video_export.preview.stop", "停止"},
        {"dialog.video_export.preview.play", "播放"},
        {"dialog.video_export.preview.pause", "暂停"},
        {"dialog.video_export.button.export", "导出"},
        {"dialog.video_export.button.cancel", "取消"},
        {"dialog.video_export.button.close", "关闭"},
        {"dialog.video_export.option.show_object_stats", "显示物量统计"},
        {"dialog.video_export.option.show_chart_info", "显示左上角谱面信息"},
        {"dialog.video_export.option.fix_hud_text_layout", "修正 HUD 字体行距"},
        {"dialog.video_export.option.hud_font", "HUD 字体"},
        {"dialog.video_export.option.hud_font_settings", "字体设置"},
        {"dialog.video_export.option.import_hud_font", "导入字体"},
        {"dialog.video_export.option.reset_hud_font", "还原"},
        {"dialog.video_export.option.hud_font_default", "默认字体"},
        {"dialog.video_export.option.hud_font_area", "区域"},
        {"dialog.video_export.option.hud_font_area.chart_info", "左上角导出信息"},
        {"dialog.video_export.option.hud_font_area.timestamp", "左下角时间戳"},
        {"dialog.video_export.option.hud_font_area.object_stats", "物量统计"},
        {"dialog.video_export.option.hud_font_area.debug", "调试信息"},
        {"dialog.video_export.error.invalid_hud_font", "请选择有效的 .ttf 或 .otf 字体文件。"},
        {"dialog.video_export.error.copy_hud_font_failed", "无法将字体复制到字体库。"},
        {"dialog.video_export.option.show_timestamp", "显示左下角时间戳"},
        {"dialog.video_export.option.smooth_brightness", "平滑亮度"},
        {"dialog.video_export.option.brightness_outer", "亮度（外侧）"},
        {"dialog.video_export.option.brightness_inner", "亮度（内侧）"},
        {"dialog.video_export.option.layout_size", "谱面显示范围"},
        {"dialog.video_export.option.flow_speed", "流速"},
        {"dialog.video_export.option.tap_flow_speed", "Tap流速"},
        {"dialog.video_export.option.touch_flow_speed", "Touch流速"},
        {"dialog.video_export.option.scale_mode", "背景 / PV 缩放模式"},
        {"dialog.video_export.option.scale.fill", "填充（必要时裁切）"},
        {"dialog.video_export.option.scale.fit", "适应（完整显示）"},
        {"dialog.video_export.option.scale.square_fit", "1:1适应（居中方框）"},
        {"dialog.video_export.option.scale.inner_circle_fit_outer_fill", "内圈1:1适应 + 外圈填充"},
        {"dialog.batch_export.title", "批量导出"},
        {"dialog.batch_export.task", "任务"},
        {"dialog.batch_export.difficulty", "难度"},
        {"dialog.batch_export.output_dir", "导出文件夹"},
        {"dialog.batch_export.chart_folders", "谱面文件夹"},
        {"dialog.batch_export.add_folders", "添加文件夹"},
        {"dialog.batch_export.remove_selected", "移除选中"},
        {"dialog.batch_export.clear", "清空"},
        {"dialog.batch_export.select_charts", "选择谱面文件夹"},
        {"dialog.batch_export.select_folder", "选择文件夹"},
        {"dialog.batch_export.error.no_difficulty", "当前没有激活难度。"},
        {"dialog.batch_export.error.no_preview", "预览画布尚未初始化。"},
        {"dialog.batch_export.error.no_chart_dirs", "请至少添加一个谱面文件夹。"},
        {"dialog.batch_export.error.no_difficulties", "请至少选择一个难度。"},
        {"dialog.batch_export.error.no_output_dir", "请选择导出文件夹。"},
        {"dialog.batch_export.error.output_dir_create_failed", "无法创建导出文件夹。"},
        {"dialog.batch_export.error.invalid_selection", "部分文件夹缺少必要文件，已跳过。"},
        {"dialog.batch_export.error.invalid_folder", "所选路径不是有效文件夹。"},
        {"dialog.batch_export.error.missing_chart_file", "缺少 maidata.txt。"},
        {"dialog.batch_export.error.missing_track_file", "缺少 track.mp3。"},
        {"dialog.batch_export.error.read_chart_failed", "无法读取 %1。"},
        {"dialog.batch_export.error.missing_requested_difficulty", "所选文件夹中缺少当前难度。"},
        {"dialog.batch_export.error.no_selected_difficulties_in_folder", "该文件夹中不存在任何已选难度：%1"},
        {"dialog.batch_export.error.no_markers", "该难度没有可导出的解析物件。"},
        {"dialog.batch_export.error.invalid_duration", "无法确定该谱面的导出时长。"},
        {"dialog.batch_export.error.validation_failed_count", "语法检查失败，共 %1 个错误。"},
        {"dialog.batch_export.error.validation_failed_detail", "语法检查失败：%1"},
        {"dialog.batch_export.progress.preparing", "正在准备批量导出..."},
        {"dialog.batch_export.progress.exporting", "正在导出 %1/%2...\n%3"},
        {"dialog.batch_export.progress.exporting_named", "正在导出 %1/%2\n%3"},
        {"dialog.batch_export.progress.current_item", "%1\n%2"},
        {"dialog.batch_export.message.canceled", "批量导出已取消。"},
        {"dialog.batch_export.message.success", "批量导出完成：成功 %1 个。"},
        {"dialog.batch_export.message.partial_failed", "批量导出完成，但有部分失败。\n成功：%1\n失败：%2"},
        {"dialog.batch_export.message.output_files", "输出文件："},
        {"dialog.video_export.error.preview_unavailable", "预览画布未初始化。"},
        {"dialog.video_export.progress.preparing", "正在准备导出..."},
        {"dialog.video_export.progress.worker_ready", "后台已就绪..."},
        {"dialog.video_export.progress.starting_export", "开始导出..."},
        {"dialog.video_export.progress.preparing_audio", "正在准备音频..."},
        {"dialog.video_export.progress.starting_ffmpeg", "正在启动 ffmpeg..."},
        {"dialog.video_export.progress.rendering", "正在渲染帧..."},
        {"dialog.video_export.progress.rendering_count", "正在渲染帧... %1/%2"},
        {"dialog.video_export.progress.finalizing_encode", "正在整理视频..."},
        {"dialog.video_export.progress.repacking", "正在整理视频..."},
        {"dialog.video_export.progress.finishing", "正在收尾..."},
        {"dialog.video_export.progress.remaining", "预计剩余 %1"},
        {"dialog.video_export.progress.generic", "正在导出..."},
        {"dialog.video_export.progress.canceling", "正在取消导出..."},
        {"dialog.video_export.progress.retrying_safe_mode", "导出子进程崩溃，正在以安全模式重试..."},
        {"dialog.video_export.progress.done", "导出完成。"},
        {"dialog.video_export.status.canceled", "导出已取消。"},
        {"dialog.video_export.status.completed", "导出完成。"},
        {"dialog.video_export.message.canceled", "导出已取消。"},
        {"dialog.video_export.message.completed", "导出完成。"},
        {"dialog.video_export.error.failed", "导出失败。"},
        {"dialog.video_export.error.worker_crash", "导出子进程已崩溃。"},
        {"dialog.video_export.error.worker_exit", "导出子进程异常退出。"},
        {"dialog.video_export.error.worker_retry_note", "导出子进程曾崩溃一次，系统已自动关闭 PBO 并重试。"},
        {"dialog.video_export.error.worker_retry_first_attempt", "第一次尝试诊断"},
        {"dialog.video_export.error.worker_retry_final_attempt", "安全模式重试诊断"},
        {"dialog.video_export.error.failed_title", "导出失败"},

        {"status.audio_restored_default", "已恢复默认音量设置"},
        {"status.audio_saved_software_default", "当前音频设置已保存为软件默认值"},
        {"status.touch_trail_enabled", "Touch 轨迹已开启"},
        {"status.touch_trail_disabled", "Touch 轨迹已关闭"},
        {"status.judge_marker_enabled", "判定标记已开启"},
        {"status.judge_marker_disabled", "判定标记已隐藏"},
        {"status.editor_text_display_updated", "文本框显示已更新。"},
        {"status.preferences_updated", "首选项已更新。"},
        {"status.preferences_saved", "首选项已保存，重启后生效。"},
        {"status.syntax.select_difficulty", "请先选择一个难度文本。"},
        {"status.syntax.failed_counts", "语法检查未通过：%1 个错误，%2 个警告。"},

        // 2026-07-07 audit backfill: these keys previously missed zh entries, so the
        // Chinese UI silently fell back to the call-site English string. Keep
        // built-in key sets aligned — ui_text_locale_spec enforces it.
        {"about.platform", "发行环境"},
        {"about.build_type", "构建类型"},
        {"dialog.batch_export.error.export_failed", "导出失败。"},
        {"dialog.batch_export.error.invalid_first", "谱面信息的 &first 数值无效。"},
        {"dialog.batch_export.error.skin_missing", "未找到预览皮肤素材。"},
        {"dialog.normalize.title", "整理谱面"},
        {"dialog.normalize.failed", "无法整理当前谱面。"},
        {"dialog.normalize.options", "微调选项"},
        {"dialog.preferences.extensions_group", "扩展"},
        {"dialog.preferences.extensions.open_folder", "打开扩展文件夹"},
        {"dialog.preferences.extensions.refresh", "刷新扩展"},
        {"dialog.preferences.extensions.open_logs", "打开日志位置"},
        {"dialog.preferences.extensions.devtools", "DevTools 面板"},
        {"dialog.preferences.extensions.devtools.summary", "快照摘要"},
        {"dialog.preferences.extensions.devtools.api_count", "API"},
        {"dialog.preferences.extensions.devtools.extension_count", "扩展"},
        {"dialog.preferences.extensions.devtools.raw_count", "实验 raw"},
        {"dialog.preferences.extensions.devtools.callback_count", "事件回调"},
        {"dialog.preferences.extensions.devtools.call_count", "最近调用"},
        {"dialog.preferences.extensions.devtools.diagnostic_count", "诊断"},
        {"dialog.preferences.extensions.devtools.api", "API 注册表"},
        {"dialog.preferences.extensions.devtools.open_bridge", "Open Bridge"},
        {"dialog.preferences.extensions.devtools.recent_calls", "最近调用"},
        {"dialog.preferences.extensions.devtools.ui", "UI 贡献"},
        {"dialog.preferences.extensions.devtools.diagnostics", "诊断"},
        {"dialog.preferences.extensions.devtools.raw_json", "原始 JSON"},
        {"dialog.preferences.extensions.devtools.refresh", "刷新快照"},
        {"dialog.preferences.extensions.enabled", "已启用"},
        {"dialog.preferences.extensions.name", "扩展"},
        {"dialog.preferences.extensions.version", "版本"},
        {"dialog.preferences.extensions.contributions", "提供内容"},
        {"dialog.preferences.extensions.status", "状态"},
        {"dialog.render_settings.gameplay.center_display.achievement_dx_minus_100", "ACHIEVEMENT DX (100-)"},
        {"dialog.render_settings.gameplay.center_display.achievement_dx_minus_101", "ACHIEVEMENT DX (101-)"},
        {"dialog.render_settings.gameplay.center_display.achievement_dx_plus", "ACHIEVEMENT DX (+)"},
        {"dialog.render_settings.gameplay.center_display.achievement_finale_plus", "ACHIEVEMENT FINALE (+)"},
        {"dialog.render_settings.gameplay.center_display.combo", "COMBO"},
        {"dialog.render_settings.gameplay.center_display.dx_score_minus", "DX SCORE (-)"},
        {"dialog.render_settings.gameplay.center_display.dx_score_plus", "DX SCORE (+)"},
        {"dialog.render_settings.gameplay.center_display.off", "无"},
        {"dialog.render_settings.gameplay.force_labeled_judge_line_when_paused", "暂停时显示判定范围"},
        {"dialog.video_export.button.cancel_export", "取消导出"},
        {"dialog.video_export.button.start_export", "开始导出"},
        {"dialog.video_export.error.executable_missing", "未找到 MiaCode 可执行文件。"},
        {"dialog.video_export.error.invalid_flow_speed", "流速数值无效。"},
        {"dialog.video_export.error.launch_failed", "无法启动后台导出。"},
        {"dialog.video_export.error.no_difficulty", "未选择有效难度。"},
        {"dialog.video_export.error.no_markers", "没有可导出的解析物件。"},
        {"dialog.video_export.error.skin_missing", "未找到预览皮肤素材。"},
        {"dialog.video_export.error.sync_failed", "无法同步当前编辑状态。"},
        {"dialog.video_export.error.worker_busy", "已有另一个导出正在进行。"},
        {"dialog.video_export.error.worker_write_failed", "无法将导出快照发送给导出子进程。"},
        {"dialog.video_export.section.intro", "片头"},
        {"dialog.video_export.section.output", "输出"},
        {"editor.validation_summary.tooltip", "错误 %1，警告 %2"},
        {"status.muri_render_mode_dx", "预览模式：无理检查。"},
        {"status.muri_render_mode_native", "预览模式：谱面确认。"},
        {"status.muri_render_mode_erase_by_area", "预览模式：按区消去。"},
        {"status.normalize.already_normalized", "谱面整理：已经是整理后的格式。"},
        {"status.normalize.applied", "已应用谱面整理：%1 小节行。"},
        {"status.transform.mirror_lr", "已应用左右镜像。"},
        {"status.transform.mirror_ud", "已应用上下镜像。"},
        {"status.transform.rotate_180", "已应用旋转 180°。"},
        {"status.transform.rotate_ccw_45", "已应用逆时针旋转 45°。"},
        {"status.transform.rotate_cw_45", "已应用顺时针旋转 45°。"},

        {"card_font.body", "正文字体"},
        {"card_font.copy_failed", "无法将字体复制到字体库。"},
        {"card_font.default", "默认字体"},
        {"card_font.import", "导入字体…"},
        {"card_font.invalid_font", "该文件不是有效的字体。"},
        {"card_font.reset", "重置"},
        {"card_font.title", "标题字体"},
        {"cover.add_image", "添加图片"},
        {"cover.add_layer", "添加 ▾"},
        {"cover.add_text", "添加文字"},
        {"cover.bold", "加粗"},
        {"cover.choose_image", "选择图片"},
        {"cover.font", "字体"},
        {"cover.image_file", "图片文件"},
        {"cover.image_layer", "图片"},
        {"cover.image_options", "图片选项"},
        {"cover.text_color", "颜色"},
        {"cover.text_content", "文字"},
        {"cover.text_layer", "文字"},
        {"cover.text_layer_default", "文字"},
        {"cover.text_options", "文字选项"},
        {"cover.add_a_chart_frame_a", "添加谱面帧（快捷键 A）"},
        {"cover.add_chart_frame", "添加谱面帧"},
        {"cover.add_difficulty_card", "添加难度卡"},
        {"cover.add_frame", "＋ 添加谱面帧"},
        {"cover.apply_preset", "应用预设"},
        {"cover.backdrop_brightness", "背景亮度（底图明暗）"},
        {"cover.background", "背景"},
        {"cover.background_brightness", "背景亮度"},
        {"cover.background_transparency", "背景透明度"},
        {"cover.blur_background", "背景虚化"},
        {"cover.brightness", "亮度"},
        {"cover.brightness_2", "背景亮度"},
        {"cover.bring_to_front", "置顶"},
        {"cover.browse", "浏览…"},
        {"cover.canvas", "画板"},
        {"cover.card_chart_frame", "卡片 + 谱面帧"},
        {"cover.card_drop_shadow", "难度卡阴影"},
        {"cover.centered_card_default", "卡片居中（默认）"},
        {"cover.chart_frame", "谱面帧"},
        {"cover.chart_frame_background_brightness", "谱面帧背景亮度"},
        {"cover.chart_frame_background_transparency", "谱面帧背景透明度"},
        {"cover.chart_frame_inner_background", "谱面帧内圈背景"},
        {"cover.chart_frame_options", "谱面帧选项"},
        {"cover.chart_jacket", "曲绘"},
        {"cover.chart_type", "谱面类型"},
        {"cover.chart_type_auto_result", "自动检测（%1）"},
        {"cover.choose_background_image", "选择背景图片"},
        {"cover.clear_recent", "清除最近"},
        {"cover.close", "关闭"},
        {"cover.close_without_exporting_esc", "关闭而不导出（Esc）"},
        {"cover.could_not_read_the_layout", "无法读取布局文件。"},
        {"cover.could_not_render_the_chart", "无法渲染谱面帧。"},
        {"cover.could_not_write_the_layout", "无法写入布局文件。"},
        {"cover.cover_export_completed", "封面导出完成。"},
        {"cover.cover_export_failed_1", "封面导出失败：\n%1"},
        {"cover.cover_layout_miacover", "封面布局 (*.miacover)"},
        {"cover.cover_layout_miacover_legacy_json", "封面布局 (*.miacover);;旧版 JSON (*.json)"},
        {"cover.custom_background_image_path", "自定义背景图片路径"},
        {"cover.custom_image", "自定义图片"},
        {"cover.delete_preset", "删除预设"},
        {"cover.duplicate_layer", "复制图层"},
        {"cover.delete_the_selected_layer_delete", "删除当前图层（Delete）"},
        {"cover.delete_this_preset", "删除这个预设？"},
        {"cover.difficulty_card", "难度卡"},
        {"cover.difficulty_card_2", "难度卡片"},
        {"cover.difficulty_card_options", "难度卡选项"},
        {"cover.dual_chart_frame_collage", "双谱面帧拼贴"},
        {"cover.export", "导出"},
        {"cover.export_cover", "导出封面"},
        {"cover.failed_to_start_the_composer", "合成器启动失败：\n%1"},
        {"cover.file_not_found", "文件不存在"},
        {"cover.frame", "帧"},
        {"cover.frame_2", "帧时间 "},
        {"cover.frame_time", "帧时间"},
        {"cover.frame_time_2", "谱面时间"},
        {"cover.frame_time_for_the_selected", "当前谱面帧的时间"},
        {"cover.frame_time_for_the_selected_2", "当前谱面帧的帧时间"},
        {"cover.hidden", " · 已隐藏"},
        {"cover.hide", "隐藏"},
        {"cover.hide_layer_v", "隐藏图层（快捷键 V）"},
        {"cover.horizontal_position", "水平位置"},
        {"cover.images_png_jpg_jpeg_bmp", "图片 (*.png *.jpg *.jpeg *.bmp *.webp)"},
        {"cover.import_cover_layout", "导入封面布局"},
        {"cover.import_layout", "导入布局…"},
        {"cover.import_layout_2", "导入布局"},
        {"cover.import_layout_file", "导入布局文件…"},
        {"cover.inner_bg", "内圈背景"},
        {"cover.jacket", "曲绘"},
        {"cover.keep_size_ellipsis", "保持字号，省略号(…)截断"},
        {"cover.layer", "图层"},
        {"cover.layer_2", "图层 · "},
        {"cover.layer_opacity", "图层不透明度"},
        {"cover.layer_size", "图层大小"},
        {"cover.layers", "图层"},
        {"cover.layout", "布局 ▾"},
        {"cover.lock", "锁定"},
        {"cover.lock_geometry_l", "锁定位置和大小（快捷键 L）"},
        {"cover.lock_position_and_size_l", "锁定位置与大小，防止拖动（快捷键 L）"},
        {"cover.long_text", "文字超长"},
        {"cover.manage_presets", "管理预设..."},
        {"cover.manage_presets_2", "管理预设"},
        {"cover.move_down", "下移"},
        {"cover.move_up", "上移"},
        {"cover.no_recent_files", "（无最近文件）"},
        {"cover.no_presets", "没有已保存的预设"},
        {"cover.no_difficulty_selected", "当前未选中难度，无法导出封面。"},
        {"cover.no_output_directory", "请先选择输出文件夹，再导出封面。"},
        {"cover.opacity", "不透明度"},
        {"cover.open", "打开"},
        {"cover.open_recent", "打开最近"},
        {"cover.play_pause_space", "播放 / 暂停（空格）"},
        {"cover.play_pause_visual_only", "播放 / 暂停（仅画面）"},
        {"cover.preset_name", "预设名称："},
        {"cover.pure_chart_frame", "纯谱面帧（无卡片）"},
        {"cover.rename_preset", "重命名预设"},
        {"cover.render_and_save_the_cover", "渲染并保存封面图片"},
        {"cover.render_level_as_text", "等级包含文字/字母"},
        {"cover.reset_canvas_zoom", "还原画布缩放"},
        {"cover.reset_canvas_zoom_ctrl_0", "还原画布缩放（Ctrl+0）"},
        {"cover.reset_discards_all_current_layers", "重置将丢弃当前所有图层与位置，继续？"},
        {"cover.reset_layout", "重置布局"},
        {"cover.reset_save_import_recent_layouts", "重置 / 保存 / 导入 / 最近布局"},
        {"cover.reset_to_default", "重置为默认布局…"},
        {"cover.save_cover_layout", "保存封面布局"},
        {"cover.save_current_as_preset", "保存当前为预设..."},
        {"cover.save_layout", "保存布局…"},
        {"cover.save_layout_2", "保存布局"},
        {"cover.save_layout_to_file", "保存布局到文件…"},
        {"cover.save_preset", "保存预设"},
        {"cover.select_a_chart_frame_layer", "选择谱面帧图层以编辑帧时间"},
        {"cover.send_to_back", "置底"},
        {"cover.show", "显示"},
        {"cover.show_layer_v", "显示图层（快捷键 V）"},
        {"cover.show_or_hide_this_layer", "显示或隐藏当前图层（快捷键 V）"},
        {"cover.shrink_to_fit", "缩小字体以放入全部"},
        {"cover.size", "大小"},
        {"cover.size_2", "尺寸"},
        {"cover.step_back", "后退一步（←）"},
        {"cover.step_forward", "前进一步（→）"},
        {"cover.the_chart_frame_could_not", "谱面帧无法渲染，封面将不包含它。"},
        {"cover.the_custom_background_image_was", "自定义背景图片未找到，已回退为曲绘背景。"},
        {"cover.the_imported_layout_included_a_chart_frame", "导入的布局包含谱面帧，但当前难度无可渲染音符，已跳过谱面帧。"},
        {"cover.the_layout_file_is_not", "布局文件不是有效的 JSON。"},
        {"cover.this_difficulty_has_no_chart", "当前难度没有可渲染的谱面音符。"},
        {"cover.this_file_is_not_a", "该文件不是 MiaCode 封面布局文件。"},
        {"cover.this_preset_needs_a_renderable", "该预设需要可渲染的谱面帧"},
        {"cover.transparency", "透明度"},
        {"cover.transparent", "透明"},
        {"cover.unlock", "解锁"},
        {"cover.unlock_geometry_l", "解锁位置和大小（快捷键 L）"},
        {"cover.vertical_position", "垂直位置"},
        {"cover.visible", "显示"},
        {"cover.x", "水平位置"},
        {"cover.y", "垂直位置"},
        {"cover.zoom_canvas_in", "放大画布视图"},
        {"cover.zoom_canvas_in_ctrl", "放大画布视图（Ctrl++）"},
        {"cover.zoom_canvas_out", "缩小画布视图"},
        {"cover.zoom_canvas_out_ctrl", "缩小画布视图（Ctrl+-）"},

        {"dialogs.open_folder", "打开文件夹"},

        {"document.1_comment_bookmarks", "%1 · 注释书签"},
        {"document.1_line_2_double_click", "%1\n第 %2 行 · 双击重命名"},
        {"document.all_difficulties_share_this_designer", "所有难度采用相同名义"},
        {"document.bpm_1_offset_2_s", "BPM %1　·　偏移 %2 s"},
        {"document.clear_all", "直接清除"},
        {"document.delete_1", "确定删除 %1 吗？该难度的等级、谱师与谱面内容将一并移除。"},
        {"document.delete_difficulty", "删除难度"},
        {"document.deleted_1", "已删除 %1。"},
        {"document.deleted_1_changes_are_still", "已删除 %1，更改尚未保存。"},
        {"document.designer_management", "谱师名义管理"},
        {"document.designers", "谱师"},
        {"document.file_already_exists", "文件已存在"},
        {"document.latency_settings", "延迟设置"},
        {"document.ln_1_col_2", "%1行 %2列"},
        {"document.ln_9999_col_9999", "9999行 9999列"},
        {"document.maidata_txt_already_exists_in", "所选文件夹下已存在 maidata.txt，是否覆盖？"},
        {"document.multiple_distinct_designer_names_were_detected", "检测到多个不同的谱师名义，请选择一个作为统一名义（写入所有难度），或选择「直接清除」让所有字段置空。"},
        {"document.no_chart_yet_records_des", "该难度暂无谱面，仅记录 &des_%1。"},
        {"document.open_the_export_page_video", "打开导出页：导出视频 / 导出封面 / 批量导出 / 打包ZIP"},
        {"document.open_toolbox_muri_check_format", "打开工具箱：无理检测 / 谱面整理 / 官谱镜像站"},
        {"document.pick_the_canonical_designer", "选择统一的谱师名"},
        {"document.select_chart_folder", "选择谱面文件夹"},
        {"document.selection_full_chart", "选中范围：全文"},
        {"document.selection_l_1c_2_l", "选中范围：%1行%2列 ~ %3行%4列"},
        {"document.snap_approximately_to_384_grid", "约分至384分音"},
        {"document.chart_sectioning", "谱面分段"},
        {"document.chart_section_every_4_measures", "每 4 小节"},
        {"document.chart_section_every_2_measures", "每 2 小节"},
        {"document.chart_section_none", "不分段"},
        {"document.subdivision_minus_1", "分音降低一档"},
        {"document.subdivision_minus_half", "分音降低半档"},
        {"document.subdivision_plus_1", "分音提升一档"},
        {"document.subdivision_plus_half", "分音提升半档"},
        {"document.toolbox", "工具箱"},
        {"document.treat_selection_start_as_measure", "选区起点视作小节线开始"},
        {"document.untitled_bookmark", "未命名书签"},
        {"document.when_checked_des_and_every", "勾选后，&des 与每个难度的 &des_N 会保持一致。"},

        {"export.export_as_zip", "导出为ZIP"},
        {"export.exported_to_1_2_file", "已导出到：\n%1\n\n包含 %2 个文件：\n%3"},
        {"export.packaging_1_2_3", "正在打包 %1/%2\n%3"},
        {"export.packaging_canceled", "已取消打包。"},
        {"export.packaging_failed_1", "打包失败。\n\n%1"},
        {"export.preparing_package", "正在准备打包…"},
        {"export.the_chart_is_empty_there", "谱面为空，没有可打包的内容。"},

        {"export_page.batch_export", "批量导出"},
        {"export_page.export_cover", "封面导出"},
        {"export_page.export_video", "视频导出"},
        {"export_page.no_difficulty_has_chart_content", "暂无包含谱面内容的难度，无法导出。"},
        {"export_page.no_difficulty_is_available_to", "暂无可导出的难度。"},
        {"export_page.open_composer", "打开合成器… ↗"},
        {"export_page.open_queue", "打开队列… ↗"},
        {"export_page.pack_as_zip", "打包 ZIP"},
        {"export_page.pack_now", "立即打包"},
        {"export_page.the_selected_difficulty_has_no", "当前难度暂无谱面内容，无法导出视频。"},
        {"export_page.the_video_export_panel_is", "视频导出面板暂不可用。"},

        {"latency.auto_detect", "自动检测"},
        {"latency.audio_decode_failed", "音频解码失败"},
        {"latency.audio_decoder", "音频解码器"},
        {"latency.back_to_chart_info", "← 返回谱面信息"},
        {"latency.bpm_not_detected", "未检测到 BPM"},
        {"latency.chart_parameters", "谱面参数"},
        {"latency.detected_1", "检测结果: %1"},
        {"latency.detected_1_s", "检测结果: %1 秒"},
        {"latency.offset", "偏移"},
        {"latency.open_audio_video_tools_sample", "打开音频/视频处理工具：采样率转换 / 视频压缩 / 开头静音 / 开头黑幕。"},
        {"latency.pause", "⏸ 暂停"},
        {"latency.requires_a_loaded_track_audio", "需要先加载歌曲音频"},
        {"latency.reset_volume", "重置音量"},
        {"latency.rhythm_calibration_audition", "节奏校准试听"},
        {"latency.s", " 秒"},
        {"latency.set_or_detect_bpm_first", "先设置/检测 BPM"},
        {"latency.sfx_volume", "SFX 音量"},
        {"latency.start_audition", "▶ 开始试听"},
        {"latency.subdivision", "分音:"},
        {"latency.track_audio_missing", "缺少歌曲音频"},

        {"net.also_create_zip_after_success", "成功后额外生成 ZIP"},
        {"net.artist", "曲师"},
        {"net.background_download_thread_started", "后台下载线程已启动。"},
        {"net.browse", "浏览..."},
        {"net.cancel_download", "取消下载"},
        {"net.canceling", "正在取消..."},
        {"net.chart_complete_1_2", "谱面完成：%1 -> %2"},
        {"net.chart_speed_summary_1_total", "谱面速度汇总：%1 total=%2 bytes network=%3ms avg=%4 slowest=%5/%6ms"},
        {"net.choose_output_directory", "选择输出目录"},
        {"net.clear_selection", "取消全选"},
        {"net.could_not_create_chart_folder", "无法创建谱面文件夹。"},
        {"net.designer", "谱师"},
        {"net.done_folder", "完成（文件夹）"},
        {"net.done_folder_zip", "完成（文件夹 + ZIP）"},
        {"net.download_canceled", "下载已取消。"},
        {"net.download_complete_1_succeeded_2", "下载完成：成功 %1，失败 %2。"},
        {"net.download_complete_with_errors_1", "有 %1 个谱面未能完整下载，残缺文件未被保存。请查看下载日志了解详情。"},
        {"net.download_resource_chart_1_resource", "下载资源：chart=%1 resource=%2 attempt=%3"},
        {"net.download_selected", "下载选中"},
        {"net.downloading", "下载中..."},
        {"net.downloading_1", "正在下载：%1"},
        {"net.downloading_1_2", "下载中：%1"},
        {"net.end", "结束"},
        {"net.enter_a_user_id_or", "输入用户 ID 或 Tag，再选择日期范围查询。"},
        {"net.failed_1", "失败：%1"},
        {"net.found_1_chart_s_from", "找到 %1 个谱面（查询返回 %2 个）。"},
        {"net.fuzzy_case_insensitive_match", "模糊大小写匹配"},
        {"net.hide_log", "隐藏日志"},
        {"net.levels", "等级"},
        {"net.net_batch_download", "Net 批量下载"},
        {"net.net_batch_download_action", "Net 批量下载..."},
        {"net.no_charts_are_selected", "没有选中的谱面。"},
        {"net.not_selected", "未选中"},
        {"net.output_directory", "输出目录"},
        {"net.package_failed_1", "打包失败：%1"},
        {"net.packaging_zip", "正在打包 ZIP..."},
        {"net.paused", "已暂停"},
        {"net.pending", "待下载"},
        {"net.please_choose_a_valid_output", "请选择有效的输出目录。"},
        {"net.please_enter_a_user_id", "请输入用户 ID、Tag 或歌曲名。"},
        {"net.query", "查询"},
        {"net.query_and_download_diagnostics_will", "查询和下载诊断日志会显示在这里。"},
        {"net.query_complete_1_ms_api", "查询完成（%1 ms）：接口返回 %2，日期筛选后 %3，本地 ID/Tag/歌曲名筛选后 %4。"},
        {"net.query_failed", "查询失败。"},
        {"net.query_failed_1_ms_2", "查询失败（%1 ms）：%2"},
        {"net.querying_net", "正在查询 Net..."},
        {"net.queue_canceled_1_succeeded_2", "队列取消：成功 %1，失败 %2。"},
        {"net.queue_complete_1_succeeded_2", "队列完成：成功 %1，失败 %2，网络总量 %3 bytes，平均 %4。"},
        {"net.queue_paused_net_cloudflare_blocked", "队列已暂停：Net/Cloudflare 阻断了请求。"},
        {"net.queue_paused_net_cloudflare_blocked_2", "队列暂停：Net/Cloudflare 阻断了请求。"},
        {"net.resource_download_failed", "资源下载失败。"},
        {"net.resource_result_1_http_2", "资源结果：%1 HTTP=%2 bytes=%3 elapsed=%4ms speed=%5"},
        {"net.retrying_1", "重试：%1"},
        {"net.select", "选择"},
        {"net.select_all", "全选"},
        {"net.show_log", "查看日志"},
        {"net.show_log_2", "查看日志 *"},
        {"net.skip_existing_file_1_2", "跳过已有文件：%1（%2 bytes）"},
        {"net.song_title", "歌曲名"},
        {"net.start", "开始"},
        {"net.start_chart_1_2", "开始谱面：%1 [%2]"},
        {"net.start_download_queue_selected_1", "开始下载队列：选中 %1，输出 %2，额外 ZIP=%3"},
        {"net.start_query_user_1_tag", "开始查询：用户=%1，tag=%2，歌曲名=%3，日期=%4..%5，模糊大小写=%6"},
        {"net.status", "状态"},
        {"net.title", "标题"},
        {"net.uploaded", "上传时间"},
        {"net.user_id", "用户 ID"},
        {"net.zip_package_1_2_3", "ZIP 打包：%1 -> %2（%3 ms）"},

        {"preferences.auto_closes_brackets_suggests_durations", "自动补全括号、给出括号/时值建议，并在输入 h 时提示 [8:1] 等 hold 时值。"},
        {"preferences.auto_completion", "自动补全"},
        {"preferences.chinese_input", "输入法禁止"},
        {"preferences.conflicts_with_1", "与「%1」重复"},
        {"preferences.disable_ime", "禁止输入法"},
        {"preferences.filter_full_width_chars", "转换全角符号"},
        {"preferences.hides_muri_from_the_editor", "开启后不在编辑器标题栏和时间轴小点中提示无理。设置保存到当前谱面文件夹的 .miacode。"},
        {"preferences.ignore_muri_issue_prompts", "忽略无理报错提示"},
        {"preferences.off", "关闭"},
        {"preferences.on", "开启"},

        {"shell.follow_code", "代码跟随"},
        {"shell.timeline_measure_line_brightness", "网格线亮度"},
        {"shell.timeline_waveform_brightness", "波形图亮度"},
        {"shell.timeline_sync", "时轴同步"},
        {"shell.view_lock", "光标居中"},

        {"shortcut.editor.font_decrease", "编辑器字号减小"},
        {"shortcut.editor.font_increase", "编辑器字号增大"},
        {"shortcut.editor.overwrite_mode", "覆写模式"},
        {"shortcut.file.quit", "退出"},
        {"shortcut.preview.pause_display_hold", "暂停时切换 判定区 / PV（按住）"},
        {"shortcut.preview.play_pause_global", "播放/暂停预览"},
        {"shortcut.preview.speed_down", "播放速度降低"},
        {"shortcut.preview.speed_up", "播放速度提高"},
        {"shortcut.preview.stop_or_play", "停止或播放预览"},
        {"shortcut.timeline.zoom_in", "Timeline 放大"},
        {"shortcut.timeline.zoom_out", "Timeline 缩小"},
        {"shortcut.transform.clear_complete_elements", "一键清空要素"},

        {"timeline.follow_code", "代码跟随"},
        {"timeline.follow_code_tooltip", "仅在播放中将编辑器光标绑定到预览时间前最近的逗号"},
        {"timeline.playback_speed", "当前倍速"},
        {"timeline.progress_follow", "进度跟随"},
        {"timeline.progress_follow_tooltip", "播放中让时间轴视图跟随预览进度线"},
        {"timeline.view_lock", "光标居中"},
        {"timeline.view_lock_tooltip", "将编辑器光标尽量保持在代码区中央"},

        {"ui.click_to_type_a_value", "点击可输入数值"},

        {"validation.adjust_the_static_tap_on", "调整静态“撞尾无理”参考检查阈值。"},
        {"validation.click_an_icon_to_jump", "点击图标可跳转到对应选项卡"},
        {"validation.copy_info", "复制信息"},
        {"validation.ignore_this_issue_type", "忽视该类型提示"},
        {"validation.issue_info_copied", "已复制信息。"},
        {"validation.jump_to_source", "跳转到源"},
        {"validation.muri.alert.muri", "无理"},
        {"validation.muri.alert.warning", "警告"},
        {"validation.muri.kind.multi_touch", "多押"},
        {"validation.muri.kind.overlap", "叠键"},
        {"validation.muri.kind.slide_head_tap", "外无"},
        {"validation.muri.kind.slide_too_fast", "内无"},
        {"validation.muri.kind.tap_on_slide", "撞尾"},
        {"validation.no_muri_issues_detected", "未检测到无理。"},
        {"validation.no_syntax_errors_detected", "未检测到语法错误。"},
        {"validation.stop_ignoring_this_issue_type", "取消忽视该类型提示"},
        {"validation.tap_on_slide_threshold", "撞尾阈值"},
        {"validation.tap_on_slide_threshold_set", "撞尾阈值已更新为 %1 ms。"},

        {"video_export.add_intro", "添加片头"},
        {"video_export.cancel_export", "取消导出"},
        {"video_export.current_export_range_1_2", "当前导出区间：[%1, %2]，共 %3 秒。"},
        {"video_export.enable_clock_count_1", "启用 clock_count (%1)"},
        {"video_export.export_range", "导出区间"},
        {"video_export.export_range_is_empty", "导出区间为空。"},
        {"video_export.export_start_is_out_of", "导出起始时间超出范围。"},
        {"video_export.export_video", "导出视频"},
        {"video_export.gameplay", "游戏"},
        {"video_export.intro", "片头"},
        {"video_export.layout_size", "Layout整图大小"},
        {"video_export.level_text_tooltip", "原生材质仅支持等级为数字0~9与“+”；如果等级需要其他字符，请勾选这个选项。"},
        {"video_export.output", "输出"},
        {"video_export.output_directory_does_not_exist", "输出目录不存在。"},
        {"video_export.please_choose_an_output_path", "请先选择输出路径。"},
        {"video_export.prepend_the_maimai_track_start", "在每个视频开头加入 maimai 风格片头（批量导出整谱）。"},
        {"video_export.resolution_is_invalid", "分辨率无效。"},
        {"video_export.show_bottom_left_timestamp", "显示左下角时间戳"},
        {"video_export.skin", "皮肤"},
        {"video_export.smooth_brightness", "平滑亮度"},
        {"video_export.start_export", "开始导出"},
        {"video_export.video", "视频"},

        {"window.collapse_left_sidebar", "折叠左侧字段栏"},
        {"window.expand_left_sidebar", "展开左侧字段栏"},
        {"window.muri", "无理"},
        {"window.replaced_1_occurrence_s", "已替换 %1 处。"},
        {"window.syntax", "语法"},
        {"window.timeline", "时间轴"},
        {"window.restore", "还原"},

        {"track_metadata.artist", "曲师"},
        {"track_metadata.bg_jpg_already_exists_overwrite", "bg.jpg 已经存在，是否覆盖？"},
        {"track_metadata.extract_cover_to_bg_jpg", "提取封面为 bg.jpg"},
        {"track_metadata.failed_to_decode_embedded_cover", "内嵌封面解码失败（MIME=%1）。"},
        {"track_metadata.failed_to_write_bg_jpg", "写入 bg.jpg 失败。"},
        {"track_metadata.loaded_artist_from_mp3", "已从 MP3 读取曲师。"},
        {"track_metadata.loaded_title_from_mp3", "已从 MP3 读取标题。"},
        {"track_metadata.mp3_audio_mp3_all_files", "MP3 音频 (*.mp3);;所有文件 (*.*)"},
        {"track_metadata.no_id3v2_tag_was_found", "没能在所选 MP3 中读取到 ID3v2 标签。"},
        {"track_metadata.overwrote_bg_jpg_with_embedded", "已覆盖 bg.jpg（来源：所选 MP3 内嵌封面）。"},
        {"track_metadata.read_artist_from_mp3", "从 MP3 读取曲师"},
        {"track_metadata.read_title_from_mp3", "从 MP3 读取标题"},
        {"track_metadata.the_selected_mp3_has_no", "所选 MP3 中没有内嵌的封面图。"},
        {"track_metadata.the_selected_mp3_s_id3", "所选 MP3 的 ID3 标签里没有%1信息。"},
        {"track_metadata.title", "标题"},
        {"track_metadata.wrote_bg_jpg_from_the", "已生成 bg.jpg（来源：所选 MP3 内嵌封面）。"},

        {"media_tools.1_was_not_found_next", "当前谱面目录缺少 %1。"},
        {"media_tools.a_black_screen", "黑幕"},
        {"media_tools.audio_video_processing", "音频/视频处理"},
        {"media_tools.background_mp4_video", "背景视频 .mp4"},
        {"media_tools.backup_restored", "已还原备份。"},
        {"media_tools.beats", "拍数"},
        {"media_tools.black_screen", "黑幕"},
        {"media_tools.cancel", "取消"},
        {"media_tools.canceled", "已取消。"},
        {"media_tools.compress_1_under_20_mib", "将压缩 %1 到 20M 内，并生成/覆盖备份 %2。是否继续？"},
        {"media_tools.compress_the_background_video_under", "将背景视频压缩到 20 MiB 以内，并自动备份原文件。"},
        {"media_tools.compress_video", "视频压缩"},
        {"media_tools.compressed_1_under_20_mib", "已压缩 %1 到 20M 内。"},
        {"media_tools.compressed_1_under_20_mib_2", "已压缩 %1 到 20M 内（原文件已备份为 %2）。"},
        {"media_tools.compressing_video", "正在压缩视频..."},
        {"media_tools.convert_track_mp3_to_44100", "将 track.mp3 处理为 44100Hz，并生成/覆盖备份 track_bak.mp3。是否继续？"},
        {"media_tools.convert_track_mp3_to_44100_2", "将 track.mp3 转换为 44100Hz，并自动备份原文件。"},
        {"media_tools.converted_track_mp3_to_44100", "已将 track.mp3 处理为 44100Hz。"},
        {"media_tools.converted_track_mp3_to_44100_2", "已将 track.mp3 处理为 44100Hz（原文件已备份为 track_bak.mp3）。"},
        {"media_tools.detect", "自动检测"},
        {"media_tools.failed_to_restore_backup_to", "还原备份失败：%1\n\n文件可能正在被预览、播放器、资源管理器预览窗格或其他程序占用。"},
        {"media_tools.failed_to_stage_original_file", "无法替换原文件：%1\n\n文件可能仍被预览、播放器、资源管理器预览窗格或其他程序占用。请停止预览并关闭占用该文件的程序后重试。"},
        {"media_tools.compressed_but_replace_failed", "压缩已完成，但原文件仍被占用，无法就地替换。压缩后的视频已另存为：\n%1\n原文件保持不变。请关闭占用该文件的程序后手动替换，或稍后重新运行本功能。"},
        {"media_tools.failed_to_write_file_1", "无法写入文件：%1\n\n文件可能正在被预览、播放器、资源管理器预览窗格或其他程序占用。"},
        {"media_tools.ffmpeg_was_not_found_place", "未找到 ffmpeg。请将 ffmpeg 放到程序目录，或设置 MIACODE_FFMPEG_PATH。"},
        {"media_tools.insert_a_black_screen_at", "在背景视频开头插入一段黑幕，并自动备份原文件。"},
        {"media_tools.insert_silence_at_the_start", "在 track.mp3 开头插入一段静音，并自动备份原文件。"},
        {"media_tools.no_background_mp4_video_was", "当前谱面目录缺少背景视频 .mp4。"},
        {"media_tools.open_or_save_a_chart", "请先打开或保存一个谱面文件。"},
        {"media_tools.prepend_pv_black_screen", "视频前置黑幕"},
        {"media_tools.prepend_track_silence", "音频前置空白"},
        {"media_tools.prepended_2_s_of_3", "已为 %1 开头添加 %2 秒%3（原文件已备份为 %4）。"},
        {"media_tools.prepended_2_seconds_of_blank", "已为 %1 开头添加 %2 秒空白。"},
        {"media_tools.prepends_1_to_2_3", "将在 %2 开头增加一段%1，时长为 BPM %4 下的 %3 个 4 分音（约 %5 秒）。"},
        {"media_tools.processing_audio", "正在处理音频..."},
        {"media_tools.processing_pv_mp4", "正在处理 pv.mp4..."},
        {"media_tools.processing_track_mp3", "正在处理 track.mp3..."},
        {"media_tools.restore_backup", "还原备份"},
        {"media_tools.sample_rate", "采样率转换"},
        {"media_tools.sample_rate_conversion_canceled", "已取消采样率转换。"},
        {"media_tools.silence", "空白"},
        {"media_tools.the_background_video", "背景视频"},
        {"media_tools.the_current_video_is_already", "当前视频已经小于 20 MiB，无需压缩。"},
        {"media_tools.the_current_video_is_already_2", "当前视频已经小于 20 MiB（%1），无需压缩。"},
        {"media_tools.track_mp3_failed", "track.mp3 处理失败"},
        {"media_tools.track_mp3_processing_canceled", "已取消 track.mp3 处理。"},
        {"media_tools.track_mp3_was_not_found", "当前谱面目录缺少 track.mp3。"},
        {"media_tools.video_compression_canceled", "已取消视频压缩。"},
        {"media_tools.video_failed", "视频处理失败"},
        {"media_tools.video_processing_canceled", "已取消视频处理。"},
        {"media_tools.prepend_track_silence_action", "????????"},
        {"media_tools.prepend_pv_black_screen_action", "????????"},
        {"media_tools.compress_video_action", "????"},
        {"media_tools.sample_rate_action", "?????"},

        {"net.upload_action", "Net 批量上传..."},
        {"net.upload_add", "加入"},
        {"net.upload_added_1_total_2", "本次加入 %1 个谱面文件夹，队列共 %2 个。"},
        {"net.upload_assets", "文件"},
        {"net.upload_cancel", "取消上传"},
        {"net.upload_canceled", "上传已取消。"},
        {"net.upload_chart_folder", "谱面文件夹"},
        {"net.upload_choose_root_directory", "选择谱面根目录"},
        {"net.upload_complete_1_succeeded_2", "上传完成：成功 %1，失败 %2。"},
        {"net.upload_could_not_open_file_1", "无法打开文件：%1"},
        {"net.upload_credentials_required", "请输入 Majdata Net 用户名和密码。"},
        {"net.upload_done", "已上传"},
        {"net.upload_found_1_folders", "找到 %1 个可上传的谱面文件夹。"},
        {"net.upload_invalid_root_directory", "请选择有效的谱面根目录。"},
        {"net.upload_logging_in", "正在登录 Majdata Net..."},
        {"net.upload_login_failed_1", "Majdata Net 登录失败：%1"},
        {"net.upload_network_error", "网络请求失败。"},
        {"net.upload_no_folders_scanned", "上传队列为空。"},
        {"net.upload_password", "密码"},
        {"net.upload_path", "路径"},
        {"net.upload_pending", "待上传"},
        {"net.upload_clear_queue", "清空队列"},
        {"net.upload_queue", "上传队列"},
        {"net.upload_queue_total_1", "上传队列中有 %1 个谱面文件夹。"},
        {"net.upload_remove_selected", "移除所选"},
        {"net.upload_remember_credentials", "记住账号和密码"},
        {"net.upload_request_timed_out", "请求超时。"},
        {"net.upload_root_directory", "谱面根目录"},
        {"net.upload_scan", "扫描"},
        {"net.upload_selected", "上传选中项"},
        {"net.upload_title", "Net 批量上传"},
        {"net.upload_uploading", "上传中..."},
        {"net.upload_uploading_1", "正在上传：%1"},
        {"net.upload_username", "用户名"},
        {"net.upload_batch_stopped_1", "上传批次已停止：%1"},
        {"net.upload_cloudflare_challenge", "检测到 Cloudflare 验证或访问拦截"},
        {"net.upload_could_not_open_file_1_2", "无法打开文件：%1（%2）"},
        {"net.upload_detail_attempt_1", "第 %1 次尝试"},
        {"net.upload_detail_chart_1", "谱面：%1"},
        {"net.upload_detail_cloudflare_challenge", "判定：Cloudflare 验证或访问拦截；为避免重复触发拦截，已停止整个批次。"},
        {"net.upload_detail_directory_1", "目录：%1"},
        {"net.upload_detail_http_1_2", "HTTP：%1 %2"},
        {"net.upload_detail_local_error_1", "本地文件错误：%1"},
        {"net.upload_detail_network_1", "网络错误：%1"},
        {"net.upload_detail_payload_too_large", "判定：服务器拒绝了过大的上传请求（HTTP 413），通常是 PV 视频过大。请压缩或移除视频后重试；其他谱面将继续上传。"},
        {"net.upload_detail_rate_limited", "判定：服务器或 Cloudflare 已限制请求频率。"},
        {"net.upload_detail_response_body", "服务器响应："},
        {"net.upload_detail_stage_1", "阶段：%1"},
        {"net.upload_detail_url_1", "URL：%1"},
        {"net.upload_failure_details_available", "展开“详细信息”可查看并复制完整失败原因。"},
        {"net.upload_failed_count_1", "%1 个谱面上传失败。"},
        {"net.upload_not_uploaded_batch_stopped", "未上传（批次已停止）"},
        {"net.upload_payload_too_large", "上传内容过大（HTTP 413）；请压缩或移除视频"},
        {"net.upload_rate_limit_retry_exhausted", "延迟重试一次后仍被限制请求频率"},
        {"net.upload_rate_limited", "请求频率受到限制"},
        {"net.upload_rate_limited_retrying_1", "请求受限，%1 秒后重试"},
        {"net.upload_retry_failed", "再次上传"},
        {"net.upload_stage_local_file", "打开本地上传文件"},
        {"net.upload_stage_login", "登录 Majdata Net"},
        {"net.upload_stage_upload", "上传谱面"},
        {"net.upload_waiting_before_next_1", "%1 秒后开始下一项上传"},
        {"media_tools.batch_pv_add", "加入"},
        {"media_tools.batch_pv_added_1_total_2", "本次加入 %1 个文件夹，列表共 %2 个文件夹。"},
        {"media_tools.batch_pv_already_small", "已小于 20 MiB，未压缩"},
        {"media_tools.batch_pv_backup_failed", "无法创建或覆盖视频备份。"},
        {"media_tools.batch_pv_cancel", "取消压缩"},
        {"media_tools.batch_pv_canceled", "批量压缩已取消。"},
        {"media_tools.batch_pv_chart_folder", "文件夹"},
        {"media_tools.batch_pv_choose_folder", "选择要扫描视频的文件夹"},
        {"media_tools.batch_pv_clear_queue", "清空队列"},
        {"media_tools.batch_pv_complete_1_2", "批量压缩完成：成功 %1，失败 %2。"},
        {"media_tools.batch_pv_compressing", "正在压缩..."},
        {"media_tools.batch_pv_compressing_1", "正在压缩：%1"},
        {"media_tools.batch_pv_confirm_1", "压缩列表中的 %1 个视频？原文件会备份为 bg_bak.mp4 或 pv_bak.mp4。"},
        {"media_tools.batch_pv_description", "扫描所选文件夹及其直接子文件夹中的 bg.mp4 或 pv.mp4，并将视频压缩到 20 MiB 以内。"},
        {"media_tools.batch_pv_done_1", "压缩成功（%1）"},
        {"media_tools.batch_pv_duration_failed", "无法读取视频时长。"},
        {"media_tools.batch_pv_empty", "视频压缩列表为空。"},
        {"media_tools.batch_pv_ffmpeg_failed_1", "FFmpeg 执行失败：%1"},
        {"media_tools.batch_pv_ffmpeg_missing", "未找到 FFmpeg。"},
        {"media_tools.batch_pv_file", "文件"},
        {"media_tools.batch_pv_folder", "文件夹"},
        {"media_tools.batch_pv_invalid_file", "视频文件无效或为空"},
        {"media_tools.batch_pv_invalid_folder", "请选择有效文件夹。"},
        {"media_tools.batch_pv_original_size", "原始大小"},
        {"media_tools.batch_pv_output_invalid", "输出未同时满足小于原文件且不超过 20 MiB。"},
        {"media_tools.batch_pv_path", "路径"},
        {"media_tools.batch_pv_pending", "等待压缩"},
        {"media_tools.batch_pv_no_video", "无视频"},
        {"media_tools.batch_pv_failed_1", "压缩失败：%1"},
        {"media_tools.batch_pv_queue_total_1", "压缩列表中有 %1 个文件夹。"},
        {"media_tools.batch_pv_remove_selected", "移除所选"},
        {"media_tools.batch_pv_replace_failed_1", "压缩完成但替换失败，输出保留在 %1"},
        {"media_tools.batch_pv_start", "压缩视频"},
        {"media_tools.batch_pv_title", "批量压缩视频"},
    };
    return map;
}

const QHash<QString, QString>& jaMap()
{
    static const QHash<QString, QString> map{
        {"menu.file", "ファイル(&F)"},
        {"menu.tools", "道具(&T)"},
        {"menu.transform", "編集(&M)"},
        {"menu.help", "ヘルプ(&H)"},
        {"menu.bpm_latency", "BPM && 遅延検出"},
        {"menu.clear_elements", "すべて消去"},
        {"menu.export_as_zip", "ZIP で出力"},
        {"menu.find_replace", "検索/置換"},
        {"menu.format_chart", "譜面を整形"},
        {"menu.official_chart_mirror", "公式譜面ミラー"},
        {"menu.preview_mode_chart_review", "プレビューモード：譜面確認"},
        {"menu.preview_mode_muri_check", "プレビューモード：無理チェック"},
        {"menu.preview_mode_erase_by_area", "プレビューモード：エリア単位で消去"},
        {"menu.simaiwiki", "simaiwiki"},
        {"menu.swap_side_panels", "左右パネルを入れ替え"},
        {"menu.tap_on_slide_threshold", "末尾衝突しきい値..."},

        {"action.new", "新規"},
        {"action.open", "開く"},
        {"action.open_folder", "フォルダーを開く"},
        {"action.open_current_folder", "今のフォルダーを開く"},
        {"action.open_recent", "最近開いたファイル"},
        {"action.open_recent.empty", "最近開いたファイルはありません"},
        {"dialog.open_startup_folder.missing_maidata.title", "maidata.txt が見つかりません"},
        {"dialog.open_startup_folder.missing_maidata.message", "ドロップしたフォルダーに maidata.txt がありません：\n%1"},
        {"dialog.open_startup_target.missing.title", "開けませんでした"},
        {"dialog.open_startup_target.missing.message", "ドロップしたファイルまたはフォルダーが存在しません：\n%1"},
        {"action.save", "保存"},
        {"action.ok", "OK"},
        {"action.discard", "放棄"},
        {"action.cancel", "キャンセル"},
        {"drop_chart.create_chart_file", "譜面ファイルを作成"},
        {"drop_chart.drop_hint", "マウスを離すと新しい譜面プロジェクトを作成します"},
        {"drop_chart.preview.title", "譜面作成の確認"},
        {"drop_chart.preview.message", "次の場所に %1 個の譜面を作成します：\n\n"},
        {"drop_chart.preview.create", "%1 個の譜面を作成"},
        {"drop_chart.error.title", "譜面の作成に失敗"},
        {"drop_chart.create_failed", "譜面を作成できませんでした。不完全な譜面プロジェクトは残していません。"},
        {"drop_chart.created_title", "譜面を作成しました"},
        {"drop_chart.confirm_switch", "最初に作成した譜面を開きますか？（合計 %1 個）"},
        {"drop_chart.created_chart", "%1 個の新しい譜面を作成しました。"},
        {"drop_chart.created_with_failures", "%1 個の譜面を作成しましたが、%2 個は作成できませんでした。不完全な譜面プロジェクトは残していません。"},
        {"action.close", "閉じる"},
        {"action.yes", "はい"},
        {"action.no", "いいえ"},
        {"action.browse", "参照..."},
        {"action.save_as", "名前を付けて保存"},
        {"action.preferences", "設定..."},
        {"action.restore_backup", "予備から復元"},
        {"action.restore_backup.empty", "使える予備はありません"},
        {"dialog.restore_backup.title", "予備から復元"},
        {"dialog.restore_backup.missing", "予備ファイルがありません：\n%1"},
        {"dialog.restore_backup.read_failed", "予備ファイルを読めません：\n%1"},
        {"dialog.restore_backup.confirm", "%1 の予備を復元しますか？"},
        {"dialog.restore_backup.abnormal_exit_confirm", "前回 MiaCode は正常に終了しませんでした。\n\n%1 の予備を復元しますか？"},
        {"status.restore_backup.loaded", "予備から復元しました。変更を残すには保存してください。"},
        {"action.about", "MiaCode について"},
        {"dialog.invalid_star_preview.title", "隠し設定"},
        {"dialog.invalid_star_preview.enable", "不正な星の表示を有効にしました。今回の起動中だけ有効です。"},
        {"dialog.invalid_star_preview.already_enabled", "不正な星の表示はすでに有効です。今回の起動中だけ有効です。"},
        {"dialog.invalid_star_preview.enabled_status", "不正な星の表示を有効にしました（今回の起動中のみ）。"},
        {"action.cut", "切り取り"},
        {"action.copy", "コピー"},
        {"action.paste", "貼り付け"},
        {"action.undo", "元に戻す"},
        {"action.redo", "やり直し"},
        {"action.stop_preview", "停止"},
        {"action.pause_preview", "再生/一時停止"},
        {"action.preview_speed_down", "再生速度 ↓"},
        {"action.preview_speed_up", "再生速度 ↑"},
        {"action.audio_settings", "音量調整"},
        {"action.video_settings", "表示設定"},
        {"action.skin_settings", "スキン"},
        {"toolbar.skin", "スキン"},
        {"toolbar.export", "出力"},
        {"action.export_chart", "出力"},
        {"action.export_cover", "ジャケット出力"},
        {"action.batch_export", "一括出力"},
        {"action.transform.mirror_lr", "左右反転"},
        {"action.transform.mirror_ud", "上下反転"},
        {"action.transform.rotate_180", "180° 回転"},
        {"action.transform.rotate_ccw_45", "左へ 45° 回転"},
        {"action.transform.rotate_cw_45", "右へ 45° 回転"},
        {"action.transform.more", "さらに..."},
        {"action.transform.toggle_break", "すべて Break 切替"},
        {"action.transform.toggle_ex", "すべて Ex 切替"},
        {"action.transform.toggle_firework", "すべて Firework 切替"},
        {"action.transform.random_rotate", "すべてランダム回転"},
        {"context.batch_transform", "一括操作"},
        {"context.more_transform", "さらに..."},

        {"toolbar.settings_placeholder", "設定"},

        {"preview.play", "再生"},
        {"preview.pause", "一時停止"},
        {"preview.fullscreen.window_title", "全画面表示"},
        {"preview.fullscreen.exit_hint", "Esc で全画面を終了"},
        {"preview.fullscreen.exit_tooltip", "全画面表示を終了（Esc）"},
        {"preview.fullscreen.enter_tooltip", "全画面表示を開く"},
        {"preview.canvas.free_aspect", "フリーアスペクト"},
        {"preview.canvas.menu_tooltip", "プレビューキャンバス"},
        {"preview.canvas.menu_description", "プレビューキャンバスメニューを開く"},

        {"editor.metadata", "譜面情報設定"},
        {"editor.welcome", "MiaCode へようこそ！"},
        {"editor.des", "譜面作者"},
        {"editor.export", "出力"},
        {"editor.delete_bookmark", "ブックマークを削除"},
        {"editor.delete_bookmark_1_this_will", "ブックマーク「%1」を削除しますか？その行の譜面コメントも削除されます。"},
        {"editor.l_1", "%1 行目"},
        {"editor.new_bookmark", "新規ブックマーク"},
        {"editor.target_line_already_has_a", "移動先の行には既にコメントがあるため、ブックマークの移動をキャンセルしました。"},
        {"editor.untitled_bookmark", "無名のブックマーク"},

        {"metadata.information", "基本情報"},
        {"metadata.other_fields", "その他の &xx 欄"},
        {"metadata.field.title", "Title"},
        {"metadata.field.artist", "Artist"},
        {"metadata.field.first", "開始ずれ"},
        {"metadata.field.des", "Designer"},
        {"metadata.field.cover", "Jacket"},
        {"metadata.choose_an_mp3_and_pull", "MP3 を選び、その ID3 タグからタイトルを読み込みます。"},
        {"metadata.choose_an_mp3_and_pull_2", "MP3 を選び、その ID3 タグからアーティストを読み込みます。"},
        {"metadata.choose_an_mp3_and_write", "MP3 を選び、その埋め込みカバー画像を現在の譜面フォルダーの bg.jpg に書き出します。"},
        {"metadata.click_to_open_the_muri", "クリックして「無理」タブを開く"},
        {"metadata.click_to_open_the_syntax", "クリックして「構文」タブを開く"},
        {"metadata.close", "検索バーを閉じる"},
        {"metadata.copy_area", "コピー範囲"},
        {"metadata.copy_area_2", "コピー範囲"},
        {"metadata.delete", "削除"},
        {"metadata.delete_1", "%1 を削除"},
        {"metadata.edit_e", "編集(&E)"},
        {"metadata.empty_hint", "← クリックして難易度を追加"},
        {"metadata.find", "検索"},
        {"metadata.find_next", "次を検索"},
        {"metadata.find_previous", "前を検索"},
        {"metadata.full_copy_area", "コピー範囲全体"},
        {"metadata.insert_bookmark", "ブックマークを挿入"},
        {"metadata.jump_to_timeline_position", "タイムライン位置へジャンプ"},
        {"metadata.latency_card.title", "遅延と開始ずれの調整"},
        {"metadata.latency_card.open", "遅延設定を開く →"},
        {"metadata.ln_1_col_1", "1 行 1 列"},
        {"metadata.manage_per_difficulty_designers", "難易度ごとの作者を管理"},
        {"metadata.open_the_latency_settings_page", "遅延設定ページを開く：BPM/Offset を調整し、試聴で校正します。"},
        {"metadata.preview_p", "プレビュー(&P)"},
        {"metadata.read_from_mp3", "MP3 から読み込む"},
        {"metadata.rename", "名前を変更"},
        {"metadata.rename_bookmark", "ブックマークの名前を変更"},
        {"metadata.replace", "置換"},
        {"metadata.replace_all", "すべて置換"},
        {"metadata.set_each_difficulty_designer", "各難易度（&des_1 … &des_7）の譜面作者名を個別に設定し、「すべての難易度で同じ作者名を使用」トグルも含みます。"},
        {"metadata.show_in_sidebar", "サイドバーに表示"},

        {"sidebar.metadata", "譜面情報設定"},
        {"sidebar.add_difficulty", "難易度を追加"},
        {"sidebar.export", "出力"},

        {"tab.timeline", "時間軸"},
        {"tab.validation_errors", "検査エラー"},

        {"dialog.preferences.title", "設定"},
        {"dialog.preferences.interface_group", "外観"},
        {"dialog.preferences.background_group", "背景"},
        {"dialog.preferences.background.enabled", "背景を有効化"},
        {"dialog.preferences.background.image", "画像"},
        {"dialog.preferences.background.choose", "選択..."},
        {"dialog.preferences.background.clear", "クリア"},
        {"dialog.preferences.background.image_filter", "画像 (*.png *.jpg *.jpeg *.bmp *.webp);;すべてのファイル (*)"},
        {"dialog.preferences.background.image_error", "選択した画像を読み込めません。"},
        {"dialog.preferences.background.save_error", "背景設定を保存できません。"},
        {"dialog.preferences.background.file_picker_unavailable", "画像選択を利用できません。"},
        {"dialog.preferences.background.opacity", "不透明度"},
        {"dialog.preferences.background.overlay", "カバー不透明度"},
        {"dialog.preferences.background.overlay_button", "カバー不透明度..."},
        {"dialog.preferences.background.overlay_dialog_title", "背景カバー不透明度"},
        {"dialog.preferences.background.overlay.toolbar", "上部メニューとツールバー"},
        {"dialog.preferences.background.overlay.status", "下部ステータスバー"},
        {"dialog.preferences.background.overlay.panel", "プレビュー枠"},
        {"dialog.preferences.background.overlay.editor_header", "譜面編集ページのタイトルバー"},
        {"dialog.preferences.background.overlay.input", "入力欄"},
        {"dialog.preferences.background.overlay.code_editor", "コード編集エリア"},
        {"dialog.preferences.background.overlay.dark", "%1（ダーク）"},
        {"dialog.preferences.background.overlay.light", "%1（ライト）"},
        {"dialog.preferences.background.overlay.reset_defaults", "既定に戻す"},
        {"dialog.preferences.background.blur", "ぼかし"},
        {"dialog.preferences.background.scale", "拡大縮小"},
        {"dialog.preferences.background.scale.cover", "Cover"},
        {"dialog.preferences.background.scale.contain", "Contain"},
        {"dialog.preferences.background.scale.stretch", "Stretch"},
        {"dialog.preferences.background.scale.center", "Center"},
        {"dialog.preferences.background.scale.repeat", "Repeat"},
        {"dialog.preferences.background.position", "位置"},
        {"dialog.preferences.background.position.center", "中央"},
        {"dialog.preferences.background.position.left", "左"},
        {"dialog.preferences.background.position.right", "右"},
        {"dialog.preferences.background.position.top", "上"},
        {"dialog.preferences.background.position.bottom", "下"},
        {"dialog.preferences.background.position.left_top", "左上"},
        {"dialog.preferences.background.position.right_top", "右上"},
        {"dialog.preferences.background.position.left_bottom", "左下"},
        {"dialog.preferences.background.position.right_bottom", "右下"},
        {"dialog.preferences.language", "言語"},
        {"dialog.preferences.language.system", "システムに合わせる"},
        {"dialog.preferences.language.english", "English"},
        {"dialog.preferences.language.chinese", "简体中文"},
        {"dialog.preferences.language.japanese", "日本語"},
        {"dialog.preferences.theme", "モード"},
        {"dialog.preferences.theme.system", "システムに合わせる"},
        {"dialog.preferences.theme.light", "明るい"},
        {"dialog.preferences.theme.dark", "暗い"},
        {"dialog.preferences.preview_side", "プレビュー位置"},
        {"dialog.preferences.preview_side.right", "右"},
        {"dialog.preferences.preview_side.left", "左"},

        {"dialog.welcome.title", "MiaCode へようこそ"},
        {"dialog.welcome.heading", "MiaCode へようこそ！"},
        {"dialog.welcome.subtitle", "作業画面の見た目を選びます。あとで上部メニューの歯車からいつでも変更できます。"},
        {"dialog.welcome.preview.editor", "編集欄"},
        {"dialog.welcome.preview.preview", "プレビュー"},
        {"dialog.welcome.preview_side", "プレビュー位置"},
        {"dialog.welcome.preview_side.right", "右側"},
        {"dialog.welcome.preview_side.left", "左側"},
        {"dialog.welcome.theme", "モード"},
        {"dialog.welcome.theme.light", "明るい"},
        {"dialog.welcome.theme.dark", "暗い"},
        {"dialog.welcome.chinese_input", "IME禁止"},
        {"dialog.welcome.chinese_input.disable", "IMEを禁止"},
        {"dialog.welcome.chinese_input.enable", "IMEを許可"},
        {"dialog.welcome.chinese_input.fullwidth", "全角記号を変換"},
        {"dialog.welcome.chinese_input.hint",
         "コメント機能を使わない場合は「IMEを禁止」がおすすめです。\n"
         "コメント機能を使い、1h【8:1】のような全角記号の誤入力を減らしたい場合は「全角記号を変換」がおすすめです。"},
        {"dialog.welcome.get_started", "始める"},
        {"dialog.preferences.editor_group", "編集"},
        {"dialog.preferences.editor_font_size", "文字サイズ"},
        {"dialog.preferences.editor_line_spacing", "行間"},
        {"dialog.preferences.editor_half_width_input", "半角記号入力に固定"},
        {"dialog.preferences.editor_auto_completion", "括弧を自動補完"},
        {"dialog.preferences.editor_ime_input_disabled", "IMEを禁止"},
        {"dialog.preferences.performance_group", "性能"},
        {"dialog.preferences.shortcuts_group", "ショートカット"},
        {"dialog.preferences.shortcuts.edit", "キー設定を編集"},
        {"dialog.preferences.shortcuts.reset", "キー設定を戻す"},
        {"dialog.preferences.shortcuts.title", "キー設定"},
        {"dialog.preferences.shortcuts.command", "命令"},
        {"dialog.preferences.shortcuts.keybinding", "キー"},
        {"dialog.preferences.shortcuts.change", "キーを変更"},
        {"dialog.preferences.shortcuts.capture_title", "キーを変更"},
        {"dialog.preferences.shortcuts.capture_prompt", "使うキーの組み合わせまたはマウスホイール操作を入力してから Enter を押してください。"},
        {"dialog.preferences.shortcuts.capture_prompt_hold", "押し続け用のキーを押してから Enter を押してください（Alt など単独の修飾キーも可）。"},
        {"dialog.preferences.shortcuts.reset_confirm_title", "キー設定を戻す"},
        {"dialog.preferences.shortcuts.reset_confirm_message", "変更できるキー設定をすべて初期値に戻しますか？"},
        {"dialog.preferences.extensions_group", "拡張"},
        {"dialog.preferences.extensions.open_folder", "拡張フォルダーを開く"},
        {"dialog.preferences.extensions.refresh", "拡張を更新"},
        {"dialog.preferences.extensions.open_logs", "ログの場所を開く"},
        {"dialog.preferences.extensions.devtools", "DevTools パネル"},
        {"dialog.preferences.extensions.devtools.summary", "スナップショット概要"},
        {"dialog.preferences.extensions.devtools.api_count", "API"},
        {"dialog.preferences.extensions.devtools.extension_count", "拡張"},
        {"dialog.preferences.extensions.devtools.raw_count", "Experimental raw"},
        {"dialog.preferences.extensions.devtools.callback_count", "イベントコールバック"},
        {"dialog.preferences.extensions.devtools.call_count", "最近の呼び出し"},
        {"dialog.preferences.extensions.devtools.diagnostic_count", "診断"},
        {"dialog.preferences.extensions.devtools.api", "API レジストリ"},
        {"dialog.preferences.extensions.devtools.open_bridge", "Open Bridge"},
        {"dialog.preferences.extensions.devtools.recent_calls", "最近の呼び出し"},
        {"dialog.preferences.extensions.devtools.ui", "UI コントリビューション"},
        {"dialog.preferences.extensions.devtools.diagnostics", "診断"},
        {"dialog.preferences.extensions.devtools.raw_json", "Raw JSON"},
        {"dialog.preferences.extensions.devtools.refresh", "スナップショットを更新"},
        {"dialog.preferences.extensions.enabled", "有効"},
        {"dialog.preferences.extensions.name", "拡張"},
        {"dialog.preferences.extensions.version", "版"},
        {"dialog.preferences.extensions.contributions", "追加内容"},
        {"dialog.preferences.extensions.status", "状態"},
        {"dialog.preferences.extensions.permissions", "権限"},
        {"extension.menu.extensions", "拡張"},
        {"extension.menu.empty", "拡張コマンドはありません"},
        {"extension.dialog.permission_title", "拡張の権限"},
        {"extension.dialog.permission_message", "拡張 %1 が高リスク権限を要求しています：\n%2\n\n対象：%3\n\n許可してこの承認を記憶しますか？"},
        {"extension.dialog.input_title", "拡張入力"},
        {"extension.dialog.quick_pick_title", "拡張選択"},
        {"extension.dialog.canceled", "キャンセルしました。"},
        {"extension.command.unavailable",
         "拡張コマンドを利用できません：%1\n\n考えられる原因：拡張が無効、manifest が無効、または更新後にコマンドが存在しなくなりました。"},
        {"extension.command.runtime_not_running",
         "拡張ランタイムが起動していないため、MiaCode は実行できません：%1\n\n考えられる原因：拡張に読み込める JS エントリがない、起動に失敗した、または拡張の更新が必要です。"},
        {"extension.command.failed", "拡張コマンドが正常に応答しませんでした：%1\n\n理由：%2"},
        {"extension.command.ran", "拡張コマンドを実行しました：%1"},
        {"editor.bookmark.insert", "ブックマークを挿入"},
        {"editor.bookmark.rename", "ブックマーク名を変更"},
        {"editor.bookmark.delete", "ブックマークを削除"},
        {"editor.bookmark.show_in_sidebar", "サイドバーに表示"},
        {"editor.bookmark.untitled", "無名のブックマーク"},
        {"editor.bookmark.delete_confirm", "ブックマーク「%1」を削除しますか？その行の譜面コメントも削除されます。"},
        {"action.reset", "戻す"},
        {"dialog.preferences.restart_title", "再起動が必要です"},
        {"dialog.preferences.restart_message", "言語設定を保存しました。メニュー、字体、画面の文字に反映するには MiaCode を再起動してください。"},
        {"dialog.unsaved_changes.title", "未保存の変更"},
        {"dialog.unsaved_changes.message", "今の文書に未保存の変更があります。先に保存しますか？"},
        {"dialog.unsaved_field_changes.title", "未保存の欄があります"},
        {"dialog.unsaved_field_changes.message", "%1 に未保存の変更があります。切り替える前に保存しますか？"},
        {"dialog.unsaved_field_changes.field.metadata", "譜面情報"},
        {"dialog.unsaved_tab_changes.title", "タブに未保存の変更があります"},
        {"dialog.unsaved_tab_changes.message", "%1 に未保存の変更があります。このタブを閉じる前に保存しますか？"},
        {"dialog.unsaved_tab_changes.details.difficulty", "保存するのはこの難易度だけです。破棄すると前回保存時の内容に戻します。"},
        {"dialog.unsaved_tab_changes.details.document", "文書全体を保存します。破棄すると前回保存時の内容に戻します。"},

        {"dialog.audio_settings.title", "音量調整"},
        {"dialog.video_settings.title", "表示設定"},
        {"dialog.render_settings.audio_group", "音量"},
        {"dialog.render_settings.video_group", "映像"},
        {"dialog.render_settings.gameplay_group", "ゲーム"},
        {"dialog.render_settings.music_group", "音楽"},
        {"dialog.render_settings.performance_group", "性能"},
        {"dialog.render_settings.preview_group", "プレビュー"},

        {"dialog.skin_settings.title", "スキン"},
        {"dialog.skin_settings.dialog_title", "スキン設定"},
        {"dialog.skin_settings.section.chart_skin", "譜面スキン"},
        {"dialog.skin_settings.open_skin_folder", "スキンフォルダーを開く"},
        {"dialog.skin_settings.open_judge_line_folder", "判定線フォルダーを開く"},
        {"dialog.skin_settings.open_directory", "フォルダーを開く"},
        {"dialog.skin_settings.chart_effect", "判定エフェクト"},
        {"dialog.skin_settings.chart_effect.standard", "SD"},
        {"dialog.skin_settings.chart_effect.starry", "DX"},
        {"dialog.render_settings.button.close", "閉じる"},
        {"dialog.render_settings.button.set_software_default_audio", "デフォルトに設定する"},
        {"dialog.render_settings.button.restore_project_default", "デフォルトに戻す"},
        {"dialog.render_settings.button.mute_non_bgm", "BGM 以外を消音"},
        {"dialog.render_settings.button.restore_non_bgm", "BGM 以外の音量を戻す"},
        {"dialog.render_settings.option.enabled", "有効"},
        {"dialog.render_settings.option.disabled", "無効"},
        {"dialog.render_settings.audio.master", "全体音量"},
        {"dialog.render_settings.audio.bgm", "BGM"},
        {"dialog.render_settings.audio.answer", "Answer"},
        {"dialog.render_settings.audio.tap", "Tap"},
        {"dialog.render_settings.audio.break", "Break"},
        {"dialog.render_settings.audio.break_slide", "Break Slide"},
        {"dialog.render_settings.audio.slide", "Slide"},
        {"dialog.render_settings.audio.break_slide_tail_cheer_mute", "Break Slideのチアボイスを消す"},
        {"dialog.render_settings.audio.ex", "EX"},
        {"dialog.render_settings.audio.touch", "Touch"},
        {"dialog.render_settings.audio.track", "Track"},
        {"dialog.render_settings.audio.global", "Global"},
        {"dialog.render_settings.audio.firework", "Firework"},
        {"dialog.render_settings.audio.button.mute", "%1 を消音"},
        {"dialog.render_settings.audio.button.unmute", "%1 を戻す"},
        {"dialog.render_settings.music.intro_sound", "開始音"},
        {"dialog.render_settings.music.intro_sound_volume", "開始音の音量"},
        {"dialog.render_settings.music.default_intro_sound", "既定の開始音"},
        {"dialog.render_settings.music.audition", "試聴"},
        {"dialog.render_settings.music.open_folder", "フォルダーを開く"},
        {"dialog.render_settings.music.open_folder.tooltip", "ここに開始音ファイルを置きます。ない場合は assets/music/track_start.wav、assets/SFX/track_start.wav の順に読みます。"},
        {"dialog.render_settings.video.brightness", "背景/PV 明るさ"},
        {"dialog.render_settings.video.scale.fill", "埋める（必要なら切り取り）"},
        {"dialog.render_settings.video.scale.fit", "収める（全体を表示）"},
        {"dialog.render_settings.video.scale.square_fit", "1:1 に収める（中央正方形）"},
        {"dialog.render_settings.video.scale.inner_circle_fit_outer_fill", "内側 1:1 + 外側を埋める"},
        {"dialog.render_settings.video.canvas_aspect.square", "1:1（正方形）"},
        {"dialog.render_settings.video.canvas_aspect.4_3", "4:3"},
        {"dialog.render_settings.video.canvas_aspect.16_9", "16:9"},
        {"dialog.render_settings.video.auto_restore_square", "出力後に 1:1 へ自動復帰"},
        {"dialog.render_settings.video.smooth_brightness", "明るさをなめらかに"},
        {"dialog.render_settings.video.touch_pad_authoring_shortcut", "touchクリック入力を有効化"},
        {"dialog.render_settings.video.brightness_outer", "明るさ（外側）"},
        {"dialog.render_settings.video.brightness_inner", "明るさ（内側）"},
        {"dialog.render_settings.video.layout_square_scale", "譜面表示範囲"},
        {"dialog.render_settings.video.flow_speed", "流速"},
        {"dialog.render_settings.video.tap_flow_speed", "Tap 流速"},
        {"dialog.render_settings.video.touch_flow_speed", "Touch 流速"},
        {"dialog.render_settings.video.skin", "スキン"},
        {"dialog.render_settings.video.skin.standard", "SD"},
        {"dialog.render_settings.video.skin.dx", "DX"},
        {"dialog.render_settings.video.skin.import", "取り込み..."},
        {"dialog.render_settings.video.scale_mode", "背景 / PV 拡大方法"},
        {"dialog.render_settings.video.canvas_aspect", "プレビュー画面比"},
        {"dialog.render_settings.gameplay.judge_effect", "判定演出表示"},
        {"dialog.render_settings.gameplay.judge_effect.slide", "slide"},
        {"dialog.render_settings.gameplay.judge_effect.tap", "tap"},
        {"dialog.render_settings.gameplay.judge_effect.break", "break"},
        {"dialog.render_settings.gameplay.judge_effect.touch", "touch"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance", "Tap 判定文字距離"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance.inner", "内"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance.middle", "中"},
        {"dialog.render_settings.gameplay.tap_judge_text_distance.outer", "外"},
        {"dialog.render_settings.gameplay.judge_line", "判定線"},
        {"dialog.render_settings.gameplay.judge_line.point", "点"},
        {"dialog.render_settings.gameplay.judge_line.line", "線"},
        {"dialog.render_settings.gameplay.judge_line.area", "判定範囲"},
        {"dialog.render_settings.gameplay.judge_line.area_labeled", "判定範囲（番号付き）"},
        {"dialog.render_settings.gameplay.judge_line.import", "取り込み..."},
        {"dialog.render_settings.gameplay.force_labeled_judge_line_when_paused", "一時停止中は判定範囲を表示"},
        {"dialog.render_settings.gameplay.center_display", "中央表示"},
        {"dialog.render_settings.gameplay.slide_stack_order", "slide の重なり順"},
        {"dialog.render_settings.gameplay.slide_stack_order.dx_style", "DX 風"},
        {"dialog.render_settings.gameplay.slide_stack_order.finale_style", "FiNALE 風"},
        {"dialog.render_settings.preview.debug", "プレビューの検査情報を表示"},
        {"dialog.render_settings.preview.canvas_frame_rate", "プレビュー更新率"},
        {"dialog.render_settings.preview.canvas_frame_rate.30", "30 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.display", "画面の最大更新率"},
        {"dialog.preferences.performance.pv_frame_rate", "PV 更新率"},
        {"dialog.preferences.performance.timeline_frame_rate", "時間軸更新率"},
        {"dialog.preferences.performance.video_decode", "PV 描画"},
        {"dialog.preferences.performance.video_decode.hardware", "ハード描画"},
        {"dialog.preferences.performance.video_decode.software", "ソフト描画"},
        {"dialog.render_settings.preview.show_object_stats", "プレビュー/出力に物量を表示"},
        {"dialog.render_settings.preview.show_validation_summary", "上部にエラー/警告数を表示"},
        {"editor.validation_summary.tooltip_with_muri", "エラー %1、警告 %2、無理 %3"},
        {"dialog.render_settings.preview.show_object_stats_preview", "プレビューに物量を表示"},
        {"dialog.render_settings.preview.show_object_stats_export", "出力に物量を表示"},

        {"dialog.video_export.title", "動画出力"},
        {"dialog.video_export.output", "出力先"},
        {"dialog.video_export.browse", "参照..."},
        {"dialog.video_export.resolution", "解像度"},
        {"dialog.video_export.fps", "FPS"},
        {"dialog.video_export.audio_bitrate", "音質"},
        {"dialog.video_export.preset", "出力品質"},
        {"dialog.video_export.preset.fast", "高速"},
        {"dialog.video_export.preset.high_quality", "高品質"},
        {"dialog.video_export.size_preset", "ファイルサイズ"},
        {"dialog.video_export.size_preset.standard", "標準"},
        {"dialog.video_export.size_preset.compact", "小さめ"},
        {"dialog.video_export.size_preset.ultra_compact_with_pv", "最小（PVあり）"},
        {"dialog.video_export.size_preset.ultra_compact", "最小（PVなし）"},
        {"dialog.video_export.section.options", "項目"},
        {"dialog.video_export.section.font", "字体"},
        {"dialog.video_export.section.range", "出力範囲"},
        {"dialog.video_export.range.start", "開始"},
        {"dialog.video_export.range.end", "終了"},
        {"dialog.video_export.range.set_left", "← 設定"},
        {"dialog.video_export.range.set_end", "終了を設定"},
        {"dialog.video_export.range.set_current", "今の値に設定"},
        {"dialog.video_export.range.set_current.tip", "今のプレビュー位置を使います"},
        {"dialog.video_export.range.intro_tag", "開始演出あり"},
        {"dialog.video_export.preview.stop", "停止"},
        {"dialog.video_export.preview.play", "再生"},
        {"dialog.video_export.preview.pause", "一時停止"},
        {"dialog.video_export.button.export", "出力"},
        {"dialog.video_export.button.cancel", "キャンセル"},
        {"dialog.video_export.button.close", "閉じる"},
        {"dialog.video_export.option.show_object_stats", "物量を表示"},
        {"dialog.video_export.option.show_chart_info", "左上に譜面情報を表示"},
        {"dialog.video_export.option.fix_hud_text_layout", "HUD フォントの行間を修正"},
        {"dialog.video_export.option.hud_font", "HUD 字体"},
        {"dialog.video_export.option.hud_font_settings", "字体設定"},
        {"dialog.video_export.option.import_hud_font", "字体を取り込む"},
        {"dialog.video_export.option.reset_hud_font", "戻す"},
        {"dialog.video_export.option.hud_font_default", "既定の字体"},
        {"dialog.video_export.option.hud_font_area", "領域"},
        {"dialog.video_export.option.hud_font_area.chart_info", "左上の出力情報"},
        {"dialog.video_export.option.hud_font_area.timestamp", "左下の時刻"},
        {"dialog.video_export.option.hud_font_area.object_stats", "物量統計"},
        {"dialog.video_export.option.hud_font_area.debug", "デバッグ情報"},
        {"dialog.video_export.error.invalid_hud_font", "有効な .ttf または .otf 字体ファイルを選んでください。"},
        {"dialog.video_export.error.copy_hud_font_failed", "字体を字体置き場へコピーできませんでした。"},
        {"dialog.video_export.option.show_timestamp", "左下に時刻を表示"},
        {"dialog.video_export.option.smooth_brightness", "明るさをなめらかに"},
        {"dialog.video_export.option.brightness_outer", "明るさ（外側）"},
        {"dialog.video_export.option.brightness_inner", "明るさ（内側）"},
        {"dialog.video_export.option.layout_size", "譜面表示範囲"},
        {"dialog.video_export.option.flow_speed", "流速"},
        {"dialog.video_export.option.tap_flow_speed", "Tap 流速"},
        {"dialog.video_export.option.touch_flow_speed", "Touch 流速"},
        {"dialog.video_export.option.scale_mode", "背景 / PV 拡大方法"},
        {"dialog.video_export.option.scale.fill", "埋める（必要なら切り取り）"},
        {"dialog.video_export.option.scale.fit", "収める（全体を表示）"},
        {"dialog.video_export.option.scale.square_fit", "1:1 に収める（中央正方形）"},
        {"dialog.video_export.option.scale.inner_circle_fit_outer_fill", "内側 1:1 + 外側を埋める"},
        {"dialog.batch_export.title", "一括出力"},
        {"dialog.batch_export.task", "タスク"},
        {"dialog.batch_export.difficulty", "難易度"},
        {"dialog.batch_export.output_dir", "出力フォルダー"},
        {"dialog.batch_export.chart_folders", "譜面フォルダー"},
        {"dialog.batch_export.add_folders", "フォルダーを追加"},
        {"dialog.batch_export.remove_selected", "選択を削除"},
        {"dialog.batch_export.clear", "空にする"},
        {"dialog.batch_export.select_charts", "譜面フォルダーを選択"},
        {"dialog.batch_export.select_folder", "フォルダーを選択"},
        {"dialog.batch_export.error.no_difficulty", "有効な難易度がありません。"},
        {"dialog.batch_export.error.no_preview", "プレビュー画面がまだ初期化されていません。"},
        {"dialog.batch_export.error.no_chart_dirs", "譜面フォルダーを 1 つ以上追加してください。"},
        {"dialog.batch_export.error.no_difficulties", "難易度を 1 つ以上選んでください。"},
        {"dialog.batch_export.error.no_output_dir", "出力フォルダーを選んでください。"},
        {"dialog.batch_export.error.output_dir_create_failed", "出力フォルダーを作成できません。"},
        {"dialog.batch_export.error.invalid_selection", "必要なファイルがないフォルダーは飛ばしました。"},
        {"dialog.batch_export.error.invalid_folder", "選んだ場所は有効なフォルダーではありません。"},
        {"dialog.batch_export.error.missing_chart_file", "maidata.txt がありません。"},
        {"dialog.batch_export.error.missing_track_file", "track.mp3 がありません。"},
        {"dialog.batch_export.error.read_chart_failed", "%1 を読めません。"},
        {"dialog.batch_export.error.missing_requested_difficulty", "選んだフォルダーに現在の難易度がありません。"},
        {"dialog.batch_export.error.no_selected_difficulties_in_folder", "このフォルダーには選択中の難易度がありません：%1"},
        {"dialog.batch_export.error.no_markers", "この難易度には出力できる解析済み物件がありません。"},
        {"dialog.batch_export.error.invalid_duration", "この譜面の出力時間を決められません。"},
        {"dialog.batch_export.error.validation_failed_count", "構文検査に失敗しました。エラー %1 件。"},
        {"dialog.batch_export.error.validation_failed_detail", "構文検査に失敗しました：%1"},
        {"dialog.batch_export.progress.preparing", "一括出力を準備中..."},
        {"dialog.batch_export.progress.exporting", "%1/%2 を出力中...\n%3"},
        {"dialog.batch_export.progress.exporting_named", "%1/%2 を出力中\n%3"},
        {"dialog.batch_export.progress.current_item", "%1\n%2"},
        {"dialog.batch_export.message.canceled", "一括出力を取り消しました。"},
        {"dialog.batch_export.message.success", "一括出力が完了しました：成功 %1 件。"},
        {"dialog.batch_export.message.partial_failed", "一括出力は完了しましたが、一部失敗しました。\n成功：%1\n失敗：%2"},
        {"dialog.batch_export.message.output_files", "出力ファイル："},
        {"dialog.video_export.error.preview_unavailable", "プレビュー画面が初期化されていません。"},
        {"dialog.video_export.progress.preparing", "出力を準備中..."},
        {"dialog.video_export.progress.worker_ready", "裏側の処理を準備済み..."},
        {"dialog.video_export.progress.starting_export", "出力を開始中..."},
        {"dialog.video_export.progress.preparing_audio", "音を準備中..."},
        {"dialog.video_export.progress.starting_ffmpeg", "ffmpeg を起動中..."},
        {"dialog.video_export.progress.rendering", "フレームを描画中..."},
        {"dialog.video_export.progress.rendering_count", "フレームを描画中... %1/%2"},
        {"dialog.video_export.progress.finalizing_encode", "動画を仕上げ中..."},
        {"dialog.video_export.progress.repacking", "動画を仕上げ中..."},
        {"dialog.video_export.progress.finishing", "最後の処理中..."},
        {"dialog.video_export.progress.remaining", "残り約 %1"},
        {"dialog.video_export.progress.generic", "出力中..."},
        {"dialog.video_export.progress.canceling", "出力を取り消し中..."},
        {"dialog.video_export.progress.retrying_safe_mode", "出力用の別処理が落ちました。安全設定で再試行中..."},
        {"dialog.video_export.progress.done", "出力完了。"},
        {"dialog.video_export.status.canceled", "出力を取り消しました。"},
        {"dialog.video_export.status.completed", "出力完了。"},
        {"dialog.video_export.message.canceled", "出力を取り消しました。"},
        {"dialog.video_export.message.completed", "出力完了。"},
        {"dialog.video_export.error.failed", "出力に失敗しました。"},
        {"dialog.video_export.error.worker_crash", "出力用の別処理が落ちました。"},
        {"dialog.video_export.error.worker_exit", "出力用の別処理が異常終了しました。"},
        {"dialog.video_export.error.worker_retry_note", "出力用の別処理が一度落ちたため、PBO を自動で無効にして再試行しました。"},
        {"dialog.video_export.error.worker_retry_first_attempt", "初回試行の診断"},
        {"dialog.video_export.error.worker_retry_final_attempt", "安全設定での再試行診断"},
        {"dialog.video_export.error.failed_title", "出力失敗"},

        {"status.audio_restored_default", "既定の音量設定に戻しました"},
        {"status.audio_saved_software_default", "今の音量設定を本体の初期値として保存しました"},
        {"status.touch_trail_enabled", "Touch 軌跡を表示しました"},
        {"status.touch_trail_disabled", "Touch 軌跡を隠しました"},
        {"status.judge_marker_enabled", "判定印を表示しました"},
        {"status.judge_marker_disabled", "判定印を隠しました"},
        {"status.editor_text_display_updated", "文字欄の表示を更新しました。"},
        {"status.preferences_updated", "設定を更新しました。"},
        {"status.preferences_saved", "設定を保存しました。再起動後に反映されます。"},
        {"status.syntax.select_difficulty", "先に難易度の本文を選んでください。"},
        {"status.syntax.failed_counts", "構文検査に失敗しました：エラー %1 件、警告 %2 件。"},

        {"about.platform", "リリース環境"},
        {"about.build_type", "ビルド種別"},
        {"dialog.batch_export.error.export_failed", "出力に失敗しました。"},
        {"dialog.batch_export.error.invalid_first", "譜面情報の &first の値が正しくありません。"},
        {"dialog.batch_export.error.skin_missing", "プレビュー用スキン素材が見つかりません。"},
        {"dialog.normalize.title", "譜面を整形"},
        {"dialog.normalize.failed", "現在の譜面を整形できませんでした。"},
        {"dialog.normalize.options", "微調整"},
        {"dialog.render_settings.gameplay.center_display.off", "なし"},
        {"dialog.render_settings.gameplay.center_display.combo", "COMBO"},
        {"dialog.render_settings.gameplay.center_display.achievement_dx_plus", "ACHIEVEMENT DX (+)"},
        {"dialog.render_settings.gameplay.center_display.achievement_dx_minus_100", "ACHIEVEMENT DX (100-)"},
        {"dialog.render_settings.gameplay.center_display.achievement_dx_minus_101", "ACHIEVEMENT DX (101-)"},
        {"dialog.render_settings.gameplay.center_display.dx_score_plus", "DX SCORE (+)"},
        {"dialog.render_settings.gameplay.center_display.dx_score_minus", "DX SCORE (-)"},
        {"dialog.render_settings.gameplay.center_display.achievement_finale_plus", "ACHIEVEMENT FINALE (+)"},
        {"dialog.video_export.button.cancel_export", "出力を取り消す"},
        {"dialog.video_export.button.start_export", "出力開始"},
        {"dialog.video_export.error.executable_missing", "MiaCode の実行ファイルが見つかりません。"},
        {"dialog.video_export.error.invalid_flow_speed", "流速の値が正しくありません。"},
        {"dialog.video_export.error.launch_failed", "バックグラウンド出力を開始できませんでした。"},
        {"dialog.video_export.error.no_difficulty", "有効な難易度が選択されていません。"},
        {"dialog.video_export.error.no_markers", "出力できる解析済みノーツがありません。"},
        {"dialog.video_export.error.skin_missing", "プレビュー用スキン素材が見つかりません。"},
        {"dialog.video_export.error.sync_failed", "現在の編集状態を同期できませんでした。"},
        {"dialog.video_export.error.worker_busy", "別の出力がすでに実行中です。"},
        {"dialog.video_export.error.worker_write_failed", "出力スナップショットをワーカーへ送信できませんでした。"},
        {"dialog.video_export.section.intro", "開始演出"},
        {"dialog.video_export.section.output", "出力"},
        {"editor.validation_summary.tooltip", "エラー %1、警告 %2"},
        {"status.muri_render_mode_dx", "プレビューモード：無理チェック。"},
        {"status.muri_render_mode_native", "プレビューモード：譜面確認。"},
        {"status.muri_render_mode_erase_by_area", "プレビューモード：エリア単位で消去。"},
        {"status.normalize.already_normalized", "譜面整形：すでに整形済みです。"},
        {"status.normalize.applied", "譜面整形を適用しました：%1 小節行。"},
        {"status.transform.mirror_lr", "左右反転を適用しました。"},
        {"status.transform.mirror_ud", "上下反転を適用しました。"},
        {"status.transform.rotate_180", "180° 回転を適用しました。"},
        {"status.transform.rotate_ccw_45", "-45° 回転を適用しました。"},
        {"status.transform.rotate_cw_45", "+45° 回転を適用しました。"},


        {"card_font.body", "本文フォント"},
        {"card_font.copy_failed", "フォントをライブラリにコピーできませんでした。"},
        {"card_font.default", "デフォルトフォント"},
        {"card_font.import", "フォントを読み込み…"},
        {"card_font.invalid_font", "有効なフォントファイルではありません。"},
        {"card_font.reset", "リセット"},
        {"card_font.title", "タイトルフォント"},
        {"cover.add_image", "画像を追加"},
        {"cover.add_layer", "追加 ▾"},
        {"cover.add_text", "テキストを追加"},
        {"cover.bold", "太字"},
        {"cover.choose_image", "画像を選択"},
        {"cover.font", "フォント"},
        {"cover.image_file", "画像ファイル"},
        {"cover.image_layer", "画像"},
        {"cover.image_options", "画像オプション"},
        {"cover.text_color", "色"},
        {"cover.text_content", "テキスト"},
        {"cover.text_layer", "テキスト"},
        {"cover.text_layer_default", "テキスト"},
        {"cover.text_options", "テキストオプション"},
        {"cover.add_a_chart_frame_a", "譜面フレームを追加（ショートカット A）"},
        {"cover.add_chart_frame", "譜面フレームを追加"},
        {"cover.add_difficulty_card", "難易度カードを追加"},
        {"cover.add_frame", "＋ 譜面フレームを追加"},
        {"cover.apply_preset", "プリセットを適用"},
        {"cover.backdrop_brightness", "背景の明るさ（下地の明暗）"},
        {"cover.background", "背景"},
        {"cover.background_brightness", "背景の明るさ"},
        {"cover.background_transparency", "背景の透明度"},
        {"cover.blur_background", "背景をぼかす"},
        {"cover.brightness", "明るさ"},
        {"cover.brightness_2", "背景の明るさ"},
        {"cover.bring_to_front", "最前面へ"},
        {"cover.browse", "参照…"},
        {"cover.canvas", "キャンバス"},
        {"cover.card_chart_frame", "カード + 譜面フレーム"},
        {"cover.card_drop_shadow", "カードのドロップシャドウ"},
        {"cover.centered_card_default", "カード中央寄せ（既定）"},
        {"cover.chart_frame", "譜面フレーム"},
        {"cover.chart_frame_background_brightness", "譜面フレームの背景の明るさ"},
        {"cover.chart_frame_background_transparency", "譜面フレームの背景の透明度"},
        {"cover.chart_frame_inner_background", "譜面フレームの内側背景"},
        {"cover.chart_frame_options", "譜面フレームのオプション"},
        {"cover.chart_jacket", "ジャケット"},
        {"cover.chart_type", "譜面タイプ"},
        {"cover.chart_type_auto_result", "自動検出（%1）"},
        {"cover.choose_background_image", "背景画像を選択"},
        {"cover.clear_recent", "最近の履歴を消去"},
        {"cover.close", "オフ"},
        {"cover.close_without_exporting_esc", "出力せずに閉じる（Esc）"},
        {"cover.could_not_read_the_layout", "レイアウトファイルを読み込めません。"},
        {"cover.could_not_render_the_chart", "譜面フレームをレンダリングできません。"},
        {"cover.could_not_write_the_layout", "レイアウトファイルを書き込めません。"},
        {"cover.cover_export_completed", "カバーの出力が完了しました。"},
        {"cover.cover_export_failed_1", "カバーの出力に失敗しました：\n%1"},
        {"cover.cover_layout_miacover", "カバーレイアウト (*.miacover)"},
        {"cover.cover_layout_miacover_legacy_json", "カバーレイアウト (*.miacover);;旧版 JSON (*.json)"},
        {"cover.custom_background_image_path", "カスタム背景画像のパス"},
        {"cover.custom_image", "カスタム画像"},
        {"cover.delete_preset", "プリセットを削除"},
        {"cover.duplicate_layer", "レイヤーを複製"},
        {"cover.delete_the_selected_layer_delete", "選択中のレイヤーを削除（Delete）"},
        {"cover.delete_this_preset", "このプリセットを削除しますか？"},
        {"cover.difficulty_card", "難易度カード"},
        {"cover.difficulty_card_2", "難易度カード"},
        {"cover.difficulty_card_options", "難易度カードのオプション"},
        {"cover.dual_chart_frame_collage", "譜面フレーム 2 枚のコラージュ"},
        {"cover.export", "出力"},
        {"cover.export_cover", "カバーを出力"},
        {"cover.failed_to_start_the_composer", "コンポーザーの起動に失敗しました：\n%1"},
        {"cover.file_not_found", "ファイルが存在しません"},
        {"cover.frame", "フレーム"},
        {"cover.frame_2", "フレーム時間 "},
        {"cover.frame_time", "フレーム時間"},
        {"cover.frame_time_2", "フレーム時間"},
        {"cover.frame_time_for_the_selected", "選択中の譜面フレームの時間"},
        {"cover.frame_time_for_the_selected_2", "選択中の譜面フレームのフレーム時間"},
        {"cover.hidden", " · 非表示"},
        {"cover.hide", "非表示"},
        {"cover.hide_layer_v", "レイヤーを非表示（ショートカット V）"},
        {"cover.horizontal_position", "水平位置"},
        {"cover.images_png_jpg_jpeg_bmp", "画像 (*.png *.jpg *.jpeg *.bmp *.webp)"},
        {"cover.import_cover_layout", "カバーレイアウトを読み込む"},
        {"cover.import_layout", "レイアウトを読み込む…"},
        {"cover.import_layout_2", "レイアウトを読み込む"},
        {"cover.import_layout_file", "レイアウトファイルを読み込む…"},
        {"cover.inner_bg", "内側の背景"},
        {"cover.jacket", "ジャケット"},
        {"cover.keep_size_ellipsis", "文字サイズを保持し省略記号(…)で切り詰め"},
        {"cover.layer", "レイヤー"},
        {"cover.layer_2", "レイヤー · "},
        {"cover.layer_opacity", "レイヤーの不透明度"},
        {"cover.layer_size", "レイヤーサイズ"},
        {"cover.layers", "レイヤー"},
        {"cover.layout", "レイアウト ▾"},
        {"cover.lock", "ロック"},
        {"cover.lock_geometry_l", "位置とサイズをロック（ショートカット L）"},
        {"cover.lock_position_and_size_l", "位置とサイズをロックしてドラッグを防止（ショートカット L）"},
        {"cover.long_text", "文字が長すぎます"},
        {"cover.manage_presets", "プリセットを管理..."},
        {"cover.manage_presets_2", "プリセットを管理"},
        {"cover.move_down", "下へ移動"},
        {"cover.move_up", "上へ移動"},
        {"cover.no_recent_files", "（最近のファイルなし）"},
        {"cover.no_presets", "保存されたプリセットはありません"},
        {"cover.no_difficulty_selected", "難易度が選択されていないため、カバーを出力できません。"},
        {"cover.no_output_directory", "カバーを出力する前に、出力フォルダーを選んでください。"},
        {"cover.opacity", "不透明度"},
        {"cover.open", "開く"},
        {"cover.open_recent", "最近のファイルを開く"},
        {"cover.play_pause_space", "再生 / 一時停止（スペース）"},
        {"cover.play_pause_visual_only", "再生 / 一時停止（映像のみ）"},
        {"cover.preset_name", "プリセット名："},
        {"cover.pure_chart_frame", "譜面フレームのみ（カードなし）"},
        {"cover.rename_preset", "プリセットの名前を変更"},
        {"cover.render_and_save_the_cover", "カバー画像をレンダリングして保存"},
        {"cover.render_level_as_text", "レベルをテキストで表示"},
        {"cover.reset_canvas_zoom", "キャンバスの拡大率をリセット"},
        {"cover.reset_canvas_zoom_ctrl_0", "キャンバスの拡大率をリセット（Ctrl+0）"},
        {"cover.reset_discards_all_current_layers", "リセットすると現在のすべてのレイヤーと位置が破棄されます。続行しますか？"},
        {"cover.reset_layout", "レイアウトをリセット"},
        {"cover.reset_save_import_recent_layouts", "リセット / 保存 / 読み込み / 最近のレイアウト"},
        {"cover.reset_to_default", "既定のレイアウトにリセット…"},
        {"cover.save_cover_layout", "カバーレイアウトを保存"},
        {"cover.save_current_as_preset", "現在の設定をプリセットとして保存..."},
        {"cover.save_layout", "レイアウトを保存…"},
        {"cover.save_layout_2", "レイアウトを保存"},
        {"cover.save_layout_to_file", "レイアウトをファイルに保存…"},
        {"cover.save_preset", "プリセットを保存"},
        {"cover.select_a_chart_frame_layer", "譜面フレームレイヤーを選んでフレーム時間を編集"},
        {"cover.send_to_back", "最背面へ"},
        {"cover.show", "表示"},
        {"cover.show_layer_v", "レイヤーを表示（ショートカット V）"},
        {"cover.show_or_hide_this_layer", "このレイヤーの表示/非表示（ショートカット V）"},
        {"cover.shrink_to_fit", "縮小して全体を収める"},
        {"cover.size", "サイズ"},
        {"cover.size_2", "サイズ"},
        {"cover.step_back", "1 コマ戻る（←）"},
        {"cover.step_forward", "1 コマ進む（→）"},
        {"cover.the_chart_frame_could_not", "譜面フレームをレンダリングできないため、カバーには含まれません。"},
        {"cover.the_custom_background_image_was", "カスタム背景画像が見つからないため、譜面ジャケットに戻しました。"},
        {"cover.the_imported_layout_included_a_chart_frame", "読み込んだレイアウトには譜面フレームが含まれていましたが、この難易度にはレンダリング可能なノーツがないため、譜面フレームをスキップしました。"},
        {"cover.the_layout_file_is_not", "レイアウトファイルが有効な JSON ではありません。"},
        {"cover.this_difficulty_has_no_chart", "この難易度にはレンダリングできる譜面ノーツがありません。"},
        {"cover.this_file_is_not_a", "このファイルは MiaCode のカバーレイアウトファイルではありません。"},
        {"cover.this_preset_needs_a_renderable", "このプリセットにはレンダリング可能な譜面フレームが必要です"},
        {"cover.transparency", "透明度"},
        {"cover.transparent", "透明"},
        {"cover.unlock", "ロック解除"},
        {"cover.unlock_geometry_l", "位置とサイズのロックを解除（ショートカット L）"},
        {"cover.vertical_position", "垂直位置"},
        {"cover.visible", "表示"},
        {"cover.x", "水平位置"},
        {"cover.y", "垂直位置"},
        {"cover.zoom_canvas_in", "キャンバスを拡大"},
        {"cover.zoom_canvas_in_ctrl", "キャンバスを拡大（Ctrl++）"},
        {"cover.zoom_canvas_out", "キャンバスを縮小"},
        {"cover.zoom_canvas_out_ctrl", "キャンバスを縮小（Ctrl+-）"},

        {"dialogs.open_folder", "フォルダーを開く"},

        {"document.1_comment_bookmarks", "%1 · コメントブックマーク"},
        {"document.1_line_2_double_click", "%1\n%2 行目 · ダブルクリックで名前変更"},
        {"document.all_difficulties_share_this_designer", "すべての難易度で同じ作者名を使用"},
        {"document.bpm_1_offset_2_s", "BPM %1　·　オフセット %2 s"},
        {"document.clear_all", "すべて消去"},
        {"document.delete_1", "%1 を削除しますか？この難易度のレベル・作者・譜面内容もすべて削除されます。"},
        {"document.delete_difficulty", "難易度を削除"},
        {"document.deleted_1", "%1 を削除しました。"},
        {"document.deleted_1_changes_are_still", "%1 を削除しました。変更はまだ保存されていません。"},
        {"document.designer_management", "譜面作者名の管理"},
        {"document.designers", "譜面作者"},
        {"document.file_already_exists", "ファイルが既に存在します"},
        {"document.latency_settings", "遅延設定"},
        {"document.ln_1_col_2", "%1 行 %2 列"},
        {"document.ln_9999_col_9999", "9999 行 9999 列"},
        {"document.maidata_txt_already_exists_in", "選択したフォルダーに既に maidata.txt があります。上書きしますか？"},
        {"document.multiple_distinct_designer_names_were_detected", "複数の異なる譜面作者名が検出されました。すべてで使用する名前を 1 つ選ぶか、「すべて消去」を選んで全フィールドを空にしてください。"},
        {"document.no_chart_yet_records_des", "譜面がまだないため、&des_%1 のみ記録します。"},
        {"document.open_the_export_page_video", "出力ページを開く：動画 / カバー / 一括 / ZIP"},
        {"document.open_toolbox_muri_check_format", "ツールボックスを開く：無理チェック / 譜面整形 / 公式譜面ミラー"},
        {"document.pick_the_canonical_designer", "統一する作者名を選択"},
        {"document.select_chart_folder", "譜面フォルダーを選択"},
        {"document.selection_full_chart", "選択範囲：譜面全体"},
        {"document.selection_l_1c_2_l", "選択範囲：%1 行 %2 列 ~ %3 行 %4 列"},
        {"document.snap_approximately_to_384_grid", "384分音に約分"},
        {"document.chart_sectioning", "譜面の区切り"},
        {"document.chart_section_every_4_measures", "4小節ごと"},
        {"document.chart_section_every_2_measures", "2小節ごと"},
        {"document.chart_section_none", "区切りなし"},
        {"document.subdivision_minus_1", "分音を 1 段下げる"},
        {"document.subdivision_minus_half", "分音を半段下げる"},
        {"document.subdivision_plus_1", "分音を 1 段上げる"},
        {"document.subdivision_plus_half", "分音を半段上げる"},
        {"document.toolbox", "ツールボックス"},
        {"document.treat_selection_start_as_measure", "選択の始点を小節線の開始とみなす"},
        {"document.untitled_bookmark", "無名のブックマーク"},
        {"document.when_checked_des_and_every", "チェックすると &des と各難易度の &des_N が一致します。"},

        {"export.export_as_zip", "ZIP で出力"},
        {"export.exported_to_1_2_file", "出力先：\n%1\n\n%2 個のファイルを含みます：\n%3"},
        {"export.packaging_1_2_3", "パッケージ化中 %1/%2\n%3"},
        {"export.packaging_canceled", "パッケージ化をキャンセルしました。"},
        {"export.packaging_failed_1", "パッケージ化に失敗しました。\n\n%1"},
        {"export.preparing_package", "パッケージ化を準備中…"},
        {"export.the_chart_is_empty_there", "譜面が空で、パッケージ化する内容がありません。"},

        {"export_page.batch_export", "一括出力"},
        {"export_page.export_cover", "カバー出力"},
        {"export_page.export_video", "動画出力"},
        {"export_page.no_difficulty_has_chart_content", "譜面内容を含む難易度がまだないため、出力できません。"},
        {"export_page.no_difficulty_is_available_to", "出力できる難易度がありません。"},
        {"export_page.open_composer", "コンポーザーを開く… ↗"},
        {"export_page.open_queue", "キューを開く… ↗"},
        {"export_page.pack_as_zip", "ZIP パッケージ化"},
        {"export_page.pack_now", "今すぐパッケージ化"},
        {"export_page.the_selected_difficulty_has_no", "選択中の難易度には出力できる譜面内容がありません。"},
        {"export_page.the_video_export_panel_is", "動画出力パネルは現在利用できません。"},

        {"latency.auto_detect", "自動検出"},
        {"latency.audio_decode_failed", "オーディオのデコードに失敗しました"},
        {"latency.audio_decoder", "オーディオデコーダー"},
        {"latency.back_to_chart_info", "← 譜面情報に戻る"},
        {"latency.bpm_not_detected", "BPM を検出できません"},
        {"latency.chart_parameters", "譜面パラメーター"},
        {"latency.detected_1", "検出結果: %1"},
        {"latency.detected_1_s", "検出結果: %1 秒"},
        {"latency.offset", "オフセット"},
        {"latency.open_audio_video_tools_sample", "オーディオ/動画ツールを開く：サンプルレート変換 / 動画圧縮 / 先頭無音 / 先頭黒画面。"},
        {"latency.pause", "⏸ 一時停止"},
        {"latency.requires_a_loaded_track_audio", "先に曲のオーディオファイルを読み込む必要があります"},
        {"latency.reset_volume", "音量をリセット"},
        {"latency.rhythm_calibration_audition", "リズム校正の試聴"},
        {"latency.s", " 秒"},
        {"latency.set_or_detect_bpm_first", "先に BPM を設定/検出してください"},
        {"latency.sfx_volume", "SFX 音量"},
        {"latency.start_audition", "▶ 試聴を開始"},
        {"latency.subdivision", "分音:"},
        {"latency.track_audio_missing", "曲のオーディオがありません"},

        {"net.also_create_zip_after_success", "Also create ZIP after success"},
        {"net.artist", "アーティスト"},
        {"net.background_download_thread_started", "Background download thread started."},
        {"net.browse", "参照..."},
        {"net.cancel_download", "Cancel Download"},
        {"net.canceling", "Canceling..."},
        {"net.chart_complete_1_2", "Chart complete: %1 -> %2"},
        {"net.chart_speed_summary_1_total", "Chart speed summary: %1 total=%2 bytes network=%3ms avg=%4 slowest=%5/%6ms"},
        {"net.choose_output_directory", "Choose Output Directory"},
        {"net.clear_selection", "Clear Selection"},
        {"net.could_not_create_chart_folder", "Could not create chart folder."},
        {"net.designer", "譜面作者"},
        {"net.done_folder", "Done (folder)"},
        {"net.done_folder_zip", "Done (folder + ZIP)"},
        {"net.download_canceled", "Download canceled."},
        {"net.download_complete_1_succeeded_2", "Download complete: %1 succeeded, %2 failed."},
        {"net.download_complete_with_errors_1", "%1 chart(s) could not be downloaded completely. Incomplete files were not saved. Please check the download log for details."},
        {"net.download_resource_chart_1_resource", "Download resource: chart=%1 resource=%2 attempt=%3"},
        {"net.download_selected", "Download Selected"},
        {"net.downloading", "Downloading..."},
        {"net.downloading_1", "Downloading: %1"},
        {"net.downloading_1_2", "Downloading: %1"},
        {"net.end", "End"},
        {"net.enter_a_user_id_or", "Enter a user ID or tag, choose a date range, then query."},
        {"net.failed_1", "Failed: %1"},
        {"net.found_1_chart_s_from", "Found %1 chart(s) from %2 returned chart(s)."},
        {"net.fuzzy_case_insensitive_match", "Fuzzy case-insensitive match"},
        {"net.hide_log", "Hide Log"},
        {"net.levels", "Levels"},
        {"net.net_batch_download", "Net Batch Download"},
        {"net.net_batch_download_action", "Net Batch Download..."},
        {"net.no_charts_are_selected", "No charts are selected."},
        {"net.not_selected", "Not selected"},
        {"net.output_directory", "Output Directory"},
        {"net.package_failed_1", "Package failed: %1"},
        {"net.packaging_zip", "Packaging ZIP..."},
        {"net.paused", "Paused"},
        {"net.pending", "Pending"},
        {"net.please_choose_a_valid_output", "Please choose a valid output directory."},
        {"net.please_enter_a_user_id", "Please enter a user ID, tag, or song title."},
        {"net.query", "Query"},
        {"net.query_and_download_diagnostics_will", "Query and download diagnostics will appear here."},
        {"net.query_complete_1_ms_api", "Query complete (%1 ms): API returned %2, date filter kept %3, local ID/tag/title filter kept %4."},
        {"net.query_failed", "Query failed."},
        {"net.query_failed_1_ms_2", "Query failed (%1 ms): %2"},
        {"net.querying_net", "Querying Net..."},
        {"net.queue_canceled_1_succeeded_2", "Queue canceled: %1 succeeded, %2 failed."},
        {"net.queue_complete_1_succeeded_2", "Queue complete: %1 succeeded, %2 failed, network total %3 bytes, average %4."},
        {"net.queue_paused_net_cloudflare_blocked", "Queue paused: Net/Cloudflare blocked a request."},
        {"net.queue_paused_net_cloudflare_blocked_2", "Queue paused: Net/Cloudflare blocked a request."},
        {"net.resource_download_failed", "Resource download failed."},
        {"net.resource_result_1_http_2", "Resource result: %1 HTTP=%2 bytes=%3 elapsed=%4ms speed=%5"},
        {"net.retrying_1", "Retrying: %1"},
        {"net.select", "Select"},
        {"net.select_all", "Select All"},
        {"net.show_log", "Show Log"},
        {"net.show_log_2", "Show Log *"},
        {"net.skip_existing_file_1_2", "Skip existing file: %1 (%2 bytes)"},
        {"net.song_title", "Song Title"},
        {"net.start", "Start"},
        {"net.start_chart_1_2", "Start chart: %1 [%2]"},
        {"net.start_download_queue_selected_1", "Start download queue: selected=%1, output=%2, extra ZIP=%3"},
        {"net.start_query_user_1_tag", "Start query: user=%1, tag=%2, title=%3, dates=%4..%5, fuzzy case=%6"},
        {"net.status", "Status"},
        {"net.title", "タイトル"},
        {"net.uploaded", "Uploaded"},
        {"net.user_id", "User ID"},
        {"net.zip_package_1_2_3", "ZIP package: %1 -> %2 (%3 ms)"},

        {"preferences.auto_closes_brackets_suggests_durations", "括弧を自動補完し、括弧/音価の候補を表示します。h を入力すると [8:1] のような hold 音価を提示します。"},
        {"preferences.auto_completion", "自動補完"},
        {"preferences.chinese_input", "IME禁止"},
        {"preferences.conflicts_with_1", "「%1」と重複しています"},
        {"preferences.disable_ime", "IMEを禁止"},
        {"preferences.filter_full_width_chars", "全角記号を変換"},
        {"preferences.hides_muri_from_the_editor", "有効にするとエディタのタイトルバーとタイムラインの点に無理を表示しません。設定は現在の譜面フォルダーの .miacode に保存されます。"},
        {"preferences.ignore_muri_issue_prompts", "無理の警告表示を無視"},
        {"preferences.off", "オフ"},
        {"preferences.on", "オン"},

        {"shell.follow_code", "コード追従"},
        {"shell.timeline_measure_line_brightness", "グリッド線の明るさ"},
        {"shell.timeline_waveform_brightness", "波形の明るさ"},
        {"shell.timeline_sync", "タイムライン同期"},
        {"shell.view_lock", "ビューロック"},

        {"shortcut.editor.font_decrease", "エディタの文字を小さく"},
        {"shortcut.editor.font_increase", "エディタの文字を大きく"},
        {"shortcut.editor.overwrite_mode", "上書きモード"},
        {"shortcut.file.quit", "終了"},
        {"shortcut.preview.pause_display_hold", "一時停止中に判定エリア / PV を切替（長押し）"},
        {"shortcut.preview.play_pause_global", "プレビューを再生/一時停止"},
        {"shortcut.preview.speed_down", "再生速度 -"},
        {"shortcut.preview.speed_up", "再生速度 +"},
        {"shortcut.preview.stop_or_play", "プレビューを停止または再生"},
        {"shortcut.timeline.zoom_in", "Timeline を拡大"},
        {"shortcut.timeline.zoom_out", "Timeline を縮小"},
        {"shortcut.transform.clear_complete_elements", "要素をすべて消去"},

        {"timeline.follow_code", "コード追従"},
        {"timeline.follow_code_tooltip", "再生中、エディタカーソルをプレビュー時刻以前の直近のカンマに連動させます"},
        {"timeline.playback_speed", "再生速度"},
        {"timeline.progress_follow", "進行追従"},
        {"timeline.progress_follow_tooltip", "再生中、タイムライン表示をプレビュー進行線の中央に保ちます"},
        {"timeline.view_lock", "ビューロック"},
        {"timeline.view_lock_tooltip", "可能な場合、エディタカーソルをコード領域の中央付近に保ちます"},

        {"ui.click_to_type_a_value", "クリックして数値を入力"},

        {"validation.adjust_the_static_tap_on", "静的な「末尾衝突無理」の参照チェックしきい値を調整します。"},
        {"validation.click_an_icon_to_jump", "アイコンをクリックして対応するタブへ移動"},
        {"validation.copy_info", "情報をコピー"},
        {"validation.ignore_this_issue_type", "この種類の警告を無視"},
        {"validation.issue_info_copied", "情報をコピーしました。"},
        {"validation.jump_to_source", "ソースへジャンプ"},
        {"validation.muri.alert.muri", "無理"},
        {"validation.muri.alert.warning", "警告"},
        {"validation.muri.kind.multi_touch", "多点押し"},
        {"validation.muri.kind.overlap", "重なり"},
        {"validation.muri.kind.slide_head_tap", "外無"},
        {"validation.muri.kind.slide_too_fast", "内無"},
        {"validation.muri.kind.tap_on_slide", "末尾衝突"},
        {"validation.no_muri_issues_detected", "無理は検出されませんでした。"},
        {"validation.no_syntax_errors_detected", "構文エラーは検出されませんでした。"},
        {"validation.stop_ignoring_this_issue_type", "この種類の警告表示を再開"},
        {"validation.tap_on_slide_threshold", "末尾衝突しきい値"},
        {"validation.tap_on_slide_threshold_set", "末尾衝突しきい値を %1 ms に更新しました。"},

        {"video_export.add_intro", "イントロを追加"},
        {"video_export.cancel_export", "出力を取り消す"},
        {"video_export.current_export_range_1_2", "現在の出力範囲：[%1, %2]、合計 %3 秒。"},
        {"video_export.enable_clock_count_1", "clock_count を有効化 (%1)"},
        {"video_export.export_range", "出力範囲"},
        {"video_export.export_range_is_empty", "出力範囲が空です。"},
        {"video_export.export_start_is_out_of", "出力開始時間が範囲外です。"},
        {"video_export.export_video", "動画を出力"},
        {"video_export.gameplay", "ゲーム"},
        {"video_export.intro", "イントロ"},
        {"video_export.layout_size", "Layout 全体サイズ"},
        {"video_export.level_text_tooltip", "焼き込み済みの LV スプライトは数字 0-9 と「+」のみ対応しています。他の文字が必要な場合は、これをオンにしてレベルをテキストで描画します。"},
        {"video_export.output", "出力"},
        {"video_export.output_directory_does_not_exist", "出力フォルダーが存在しません。"},
        {"video_export.please_choose_an_output_path", "先に出力先を選択してください。"},
        {"video_export.prepend_the_maimai_track_start", "各動画の先頭に maimai 風のイントロを追加します（全譜面の一括出力）。"},
        {"video_export.resolution_is_invalid", "解像度が無効です。"},
        {"video_export.show_bottom_left_timestamp", "左下にタイムスタンプを表示"},
        {"video_export.skin", "スキン"},
        {"video_export.smooth_brightness", "明るさを滑らかに"},
        {"video_export.start_export", "出力を開始"},
        {"video_export.video", "動画"},

        {"window.collapse_left_sidebar", "左のフィールド欄を折りたたむ"},
        {"window.expand_left_sidebar", "左のフィールド欄を展開"},
        {"window.muri", "無理"},
        {"window.replaced_1_occurrence_s", "%1 か所を置換しました。"},
        {"window.syntax", "構文"},
        {"window.timeline", "タイムライン"},
        {"window.restore", "元のサイズに戻す"},

        {"track_metadata.artist", "アーティスト"},
        {"track_metadata.bg_jpg_already_exists_overwrite", "bg.jpg は既に存在します。上書きしますか？"},
        {"track_metadata.extract_cover_to_bg_jpg", "カバーを bg.jpg に抽出"},
        {"track_metadata.failed_to_decode_embedded_cover", "埋め込みカバーのデコードに失敗しました（MIME=%1）。"},
        {"track_metadata.failed_to_write_bg_jpg", "bg.jpg の書き込みに失敗しました。"},
        {"track_metadata.loaded_artist_from_mp3", "MP3 からアーティストを読み込みました。"},
        {"track_metadata.loaded_title_from_mp3", "MP3 からタイトルを読み込みました。"},
        {"track_metadata.mp3_audio_mp3_all_files", "MP3 オーディオ (*.mp3);;すべてのファイル (*.*)"},
        {"track_metadata.no_id3v2_tag_was_found", "選択した MP3 から ID3v2 タグを読み取れませんでした。"},
        {"track_metadata.overwrote_bg_jpg_with_embedded", "bg.jpg を上書きしました（選択した MP3 の埋め込みカバーから）。"},
        {"track_metadata.read_artist_from_mp3", "MP3 からアーティストを読み込む"},
        {"track_metadata.read_title_from_mp3", "MP3 からタイトルを読み込む"},
        {"track_metadata.the_selected_mp3_has_no", "選択した MP3 に埋め込みカバー画像がありません。"},
        {"track_metadata.the_selected_mp3_s_id3", "選択した MP3 の ID3 タグに%1情報がありません。"},
        {"track_metadata.title", "タイトル"},
        {"track_metadata.wrote_bg_jpg_from_the", "bg.jpg を生成しました（選択した MP3 の埋め込みカバーから）。"},

        {"media_tools.1_was_not_found_next", "現在の譜面フォルダーに %1 がありません。"},
        {"media_tools.a_black_screen", "黒画面"},
        {"media_tools.audio_video_processing", "オーディオ/動画処理"},
        {"media_tools.background_mp4_video", "背景動画 .mp4"},
        {"media_tools.backup_restored", "バックアップを復元しました。"},
        {"media_tools.beats", "拍数"},
        {"media_tools.black_screen", "黒画面"},
        {"media_tools.cancel", "キャンセル"},
        {"media_tools.canceled", "キャンセルしました。"},
        {"media_tools.compress_1_under_20_mib", "%1 を 20 MiB 以内に圧縮し、バックアップ %2 を作成/上書きします。続行しますか？"},
        {"media_tools.compress_the_background_video_under", "背景動画を 20 MiB 以内に圧縮し、元ファイルを自動でバックアップします。"},
        {"media_tools.compress_video", "動画圧縮"},
        {"media_tools.compressed_1_under_20_mib", "%1 を 20 MiB 以内に圧縮しました。"},
        {"media_tools.compressed_1_under_20_mib_2", "%1 を 20 MiB 以内に圧縮しました（元ファイルは %2 にバックアップ）。"},
        {"media_tools.compressing_video", "動画を圧縮中..."},
        {"media_tools.convert_track_mp3_to_44100", "track.mp3 を 44100Hz に変換し、バックアップ track_bak.mp3 を作成/上書きします。続行しますか？"},
        {"media_tools.convert_track_mp3_to_44100_2", "track.mp3 を 44100Hz に変換し、元ファイルを自動でバックアップします。"},
        {"media_tools.converted_track_mp3_to_44100", "track.mp3 を 44100Hz に変換しました。"},
        {"media_tools.converted_track_mp3_to_44100_2", "track.mp3 を 44100Hz に変換しました（元ファイルは track_bak.mp3 にバックアップ）。"},
        {"media_tools.detect", "自動検出"},
        {"media_tools.failed_to_restore_backup_to", "バックアップの復元に失敗しました：%1\n\nこのファイルはプレビュー、メディアプレーヤー、エクスプローラーのプレビューウィンドウ、または別のプログラムで開かれている可能性があります。"},
        {"media_tools.failed_to_stage_original_file", "元ファイルを差し替えできません：%1\n\nこのファイルはまだプレビュー、メディアプレーヤー、エクスプローラーのプレビューウィンドウ、または別のプログラムで開かれている可能性があります。プレビューを停止し、使用中のプログラムを閉じてから再試行してください。"},
        {"media_tools.compressed_but_replace_failed", "圧縮は完了しましたが、元ファイルが使用中のため差し替えできませんでした。圧縮後の動画は次の名前で保存しました：\n%1\n元ファイルは変更していません。使用中のプログラムを閉じてから手動で差し替えるか、この機能をもう一度実行してください。"},
        {"media_tools.failed_to_write_file_1", "ファイルを書き込めません：%1\n\nこのファイルはプレビュー、メディアプレーヤー、エクスプローラーのプレビューウィンドウ、または別のプログラムで開かれている可能性があります。"},
        {"media_tools.ffmpeg_was_not_found_place", "ffmpeg が見つかりません。ffmpeg をアプリと同じ場所に置くか、MIACODE_FFMPEG_PATH を設定してください。"},
        {"media_tools.insert_a_black_screen_at", "背景動画の先頭に黒画面を挿入し、元ファイルを自動でバックアップします。"},
        {"media_tools.insert_silence_at_the_start", "track.mp3 の先頭に無音を挿入し、元ファイルを自動でバックアップします。"},
        {"media_tools.no_background_mp4_video_was", "現在の譜面フォルダーに背景動画 .mp4 がありません。"},
        {"media_tools.open_or_save_a_chart", "先に譜面ファイルを開くか保存してください。"},
        {"media_tools.prepend_pv_black_screen", "動画の先頭に黒画面を追加"},
        {"media_tools.prepend_track_silence", "オーディオの先頭に無音を追加"},
        {"media_tools.prepended_2_s_of_3", "%1 の先頭に %2 秒の%3を追加しました（元ファイルは %4 にバックアップ）。"},
        {"media_tools.prepended_2_seconds_of_blank", "%1 の先頭に %2 秒の空白を追加しました。"},
        {"media_tools.prepends_1_to_2_3", "%2 の先頭に%1を追加します。長さは BPM %4 で %3 個の 4 分音（約 %5 秒）です。"},
        {"media_tools.processing_audio", "オーディオを処理中..."},
        {"media_tools.processing_pv_mp4", "pv.mp4 を処理中..."},
        {"media_tools.processing_track_mp3", "track.mp3 を処理中..."},
        {"media_tools.restore_backup", "バックアップを復元"},
        {"media_tools.sample_rate", "サンプルレート変換"},
        {"media_tools.sample_rate_conversion_canceled", "サンプルレート変換をキャンセルしました。"},
        {"media_tools.silence", "空白"},
        {"media_tools.the_background_video", "背景動画"},
        {"media_tools.the_current_video_is_already", "現在の動画は既に 20 MiB 未満です。圧縮は不要です。"},
        {"media_tools.the_current_video_is_already_2", "現在の動画は既に 20 MiB 未満です（%1）。圧縮は不要です。"},
        {"media_tools.track_mp3_failed", "track.mp3 の処理に失敗しました"},
        {"media_tools.track_mp3_processing_canceled", "track.mp3 の処理をキャンセルしました。"},
        {"media_tools.track_mp3_was_not_found", "現在の譜面フォルダーに track.mp3 がありません。"},
        {"media_tools.video_compression_canceled", "動画の圧縮をキャンセルしました。"},
        {"media_tools.video_failed", "動画の処理に失敗しました"},
        {"media_tools.video_processing_canceled", "動画の処理をキャンセルしました。"},
        {"media_tools.prepend_track_silence_action", "音声の先頭に無音を追加..."},
        {"media_tools.prepend_pv_black_screen_action", "動画の先頭に黒画面を追加..."},
        {"media_tools.compress_video_action", "動画圧縮..."},
        {"media_tools.sample_rate_action", "サンプルレート変換..."},

        {"net.upload_action", "Net Batch Upload..."},
        {"net.upload_add", "Add"},
        {"net.upload_added_1_total_2", "Added %1 folder(s); %2 in the queue."},
        {"net.upload_assets", "Files"},
        {"net.upload_cancel", "Cancel Upload"},
        {"net.upload_canceled", "Upload canceled."},
        {"net.upload_chart_folder", "Chart Folder"},
        {"net.upload_choose_root_directory", "Choose Chart Root Directory"},
        {"net.upload_complete_1_succeeded_2", "Upload complete: %1 succeeded, %2 failed."},
        {"net.upload_could_not_open_file_1", "Could not open file: %1"},
        {"net.upload_credentials_required", "Enter your Majdata Net username and password."},
        {"net.upload_done", "Uploaded"},
        {"net.upload_found_1_folders", "Found %1 uploadable chart folder(s)."},
        {"net.upload_invalid_root_directory", "Choose a valid chart root directory."},
        {"net.upload_logging_in", "Signing in to Majdata Net..."},
        {"net.upload_login_failed_1", "Majdata Net sign-in failed: %1"},
        {"net.upload_network_error", "Network request failed."},
        {"net.upload_no_folders_scanned", "The upload queue is empty."},
        {"net.upload_password", "Password"},
        {"net.upload_path", "Path"},
        {"net.upload_pending", "Pending upload"},
        {"net.upload_clear_queue", "キューを空にする"},
        {"net.upload_queue", "Upload Queue"},
        {"net.upload_queue_total_1", "%1 folder(s) in the upload queue."},
        {"net.upload_remove_selected", "Remove Selected"},
        {"net.upload_remember_credentials", "Remember account and password"},
        {"net.upload_request_timed_out", "Request timed out."},
        {"net.upload_root_directory", "Chart Root"},
        {"net.upload_scan", "Scan"},
        {"net.upload_selected", "Upload Selected"},
        {"net.upload_title", "Net Batch Upload"},
        {"net.upload_uploading", "Uploading..."},
        {"net.upload_uploading_1", "Uploading: %1"},
        {"net.upload_username", "Username"},
        {"net.upload_batch_stopped_1", "Upload batch stopped: %1"},
        {"net.upload_cloudflare_challenge", "Cloudflare challenge or access block detected"},
        {"net.upload_could_not_open_file_1_2", "Could not open file: %1 (%2)"},
        {"net.upload_detail_attempt_1", "Attempt %1"},
        {"net.upload_detail_chart_1", "Chart: %1"},
        {"net.upload_detail_cloudflare_challenge", "Diagnosis: Cloudflare challenge/access block; the batch was stopped to avoid repeated blocked requests."},
        {"net.upload_detail_directory_1", "Directory: %1"},
        {"net.upload_detail_http_1_2", "HTTP: %1 %2"},
        {"net.upload_detail_local_error_1", "Local file error: %1"},
        {"net.upload_detail_network_1", "Network error: %1"},
        {"net.upload_detail_payload_too_large", "Diagnosis: the server rejected an oversized upload request (HTTP 413), usually because the PV video is too large. Compress or remove the video and retry. Other charts will continue uploading."},
        {"net.upload_detail_rate_limited", "Diagnosis: rate limited by the server or Cloudflare."},
        {"net.upload_detail_response_body", "Server response:"},
        {"net.upload_detail_stage_1", "Stage: %1"},
        {"net.upload_detail_url_1", "URL: %1"},
        {"net.upload_failure_details_available", "Expand Details to view and copy the complete failure diagnostics."},
        {"net.upload_failed_count_1", "%1 chart(s) failed to upload."},
        {"net.upload_not_uploaded_batch_stopped", "Not uploaded (batch stopped)"},
        {"net.upload_payload_too_large", "Upload content is too large (HTTP 413); compress or remove the video"},
        {"net.upload_rate_limit_retry_exhausted", "Rate limit remained after one delayed retry"},
        {"net.upload_rate_limited", "Request rate limited"},
        {"net.upload_rate_limited_retrying_1", "Rate limited; retrying in %1 s"},
        {"net.upload_retry_failed", "Retry Failed Charts"},
        {"net.upload_stage_local_file", "Open local upload file"},
        {"net.upload_stage_login", "Majdata Net sign-in"},
        {"net.upload_stage_upload", "Chart upload"},
        {"net.upload_waiting_before_next_1", "Next upload starts in %1 s"},
        {"media_tools.batch_pv_add", "Add"},
        {"media_tools.batch_pv_added_1_total_2", "Added %1 folder(s); %2 folder(s) in the list."},
        {"media_tools.batch_pv_already_small", "Already under 20 MiB; not compressed"},
        {"media_tools.batch_pv_backup_failed", "Could not create or replace the video backup."},
        {"media_tools.batch_pv_cancel", "Cancel Compression"},
        {"media_tools.batch_pv_canceled", "Batch compression canceled."},
        {"media_tools.batch_pv_chart_folder", "Folder"},
        {"media_tools.batch_pv_choose_folder", "Choose a Folder to Scan for Videos"},
        {"media_tools.batch_pv_clear_queue", "キューを空にする"},
        {"media_tools.batch_pv_complete_1_2", "Batch compression complete: %1 succeeded, %2 failed."},
        {"media_tools.batch_pv_compressing", "Compressing..."},
        {"media_tools.batch_pv_compressing_1", "Compressing: %1"},
        {"media_tools.batch_pv_confirm_1", "Compress all %1 videos in the list? Originals will be backed up as bg_bak.mp4 or pv_bak.mp4."},
        {"media_tools.batch_pv_description", "Scan the selected folder and its immediate subfolders for bg.mp4 or pv.mp4, then compress videos under 20 MiB."},
        {"media_tools.batch_pv_done_1", "Compression succeeded (%1)"},
        {"media_tools.batch_pv_duration_failed", "Could not read the video duration."},
        {"media_tools.batch_pv_empty", "The video compression list is empty."},
        {"media_tools.batch_pv_ffmpeg_failed_1", "FFmpeg failed: %1"},
        {"media_tools.batch_pv_ffmpeg_missing", "FFmpeg was not found."},
        {"media_tools.batch_pv_file", "File"},
        {"media_tools.batch_pv_folder", "Folder"},
        {"media_tools.batch_pv_invalid_file", "Invalid or empty video file"},
        {"media_tools.batch_pv_invalid_folder", "Choose a valid folder."},
        {"media_tools.batch_pv_original_size", "Original Size"},
        {"media_tools.batch_pv_output_invalid", "Output was not smaller than the original and under 20 MiB."},
        {"media_tools.batch_pv_path", "Path"},
        {"media_tools.batch_pv_pending", "Pending"},
        {"media_tools.batch_pv_no_video", "No video"},
        {"media_tools.batch_pv_failed_1", "Compression failed: %1"},
        {"media_tools.batch_pv_queue_total_1", "%1 folder(s) in the compression list."},
        {"media_tools.batch_pv_remove_selected", "Remove Selected"},
        {"media_tools.batch_pv_replace_failed_1", "Compressed, but replacement failed; output kept at %1"},
        {"media_tools.batch_pv_start", "Compress Videos"},
        {"media_tools.batch_pv_title", "Batch Compress Videos"},
    };
    return map;
}

UiText::LanguagePreference& preferredLanguageStorage()
{
    static UiText::LanguagePreference preference = loadStoredLanguagePreference();
    return preference;
}

UiText::ThemePreference& preferredThemeStorage()
{
    static UiText::ThemePreference preference = loadStoredThemePreference();
    return preference;
}

}  // namespace

UiText::LanguagePreference UiText::preferredLanguage()
{
    return preferredLanguageStorage();
}

void UiText::setPreferredLanguage(LanguagePreference preference)
{
    preferredLanguageStorage() = preference;
    saveStoredLanguagePreference(preference);
}
QString UiText::preferredLanguageToken()
{
    const QJsonObject root = loadPreferencesObject();
    return normalizedLanguageToken(
        root.value(QString::fromLatin1(kUiSectionKey)).toObject()
            .value(QString::fromLatin1(kLanguageKey)).toString(QStringLiteral("system")));
}

void UiText::setPreferredLanguageToken(const QString& token)
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

QString UiText::resolvedLanguageToken()
{
    return resolvedLanguageTokenFromStorage();
}

QVector<UiText::LanguageOption> UiText::availableLanguageOptions()
{
    QVector<UiText::LanguageOption> options{
        {QStringLiteral("system"), text(QStringLiteral("dialog.preferences.language.system")).isEmpty()
            ? QStringLiteral("Follow System")
            : text(QStringLiteral("dialog.preferences.language.system")), true},
        {QStringLiteral("en"), text(QStringLiteral("dialog.preferences.language.english")).isEmpty()
            ? QStringLiteral("English")
            : text(QStringLiteral("dialog.preferences.language.english")), true},
        {QStringLiteral("zh"), text(QStringLiteral("dialog.preferences.language.chinese")).isEmpty()
            ? QStringLiteral("Simplified Chinese")
            : text(QStringLiteral("dialog.preferences.language.chinese")), true},
        {QStringLiteral("ja"), text(QStringLiteral("dialog.preferences.language.japanese")).isEmpty()
            ? QStringLiteral("Japanese")
            : text(QStringLiteral("dialog.preferences.language.japanese")), true},
    };
    return options;
}

bool UiText::isLanguageAvailable(const QString& token)
{
    const QString normalized = normalizedLanguageToken(token);
    if (isBuiltInLanguageToken(normalized)) {
        return true;
    }
    return false;
}

bool UiText::ensurePreferredLanguageAvailable()
{
    const QString token = preferredLanguageToken();
    if (token.isEmpty() || isLanguageAvailable(token)) {
        return false;
    }
    setPreferredLanguageToken(QStringLiteral("system"));
    return true;
}

UiText::ThemePreference UiText::preferredTheme()
{
    return preferredThemeStorage();
}

void UiText::setPreferredTheme(ThemePreference preference)
{
    preferredThemeStorage() = preference;
    saveStoredThemePreference(preference);
}

bool UiText::isChineseUi()
{
    return resolvedLanguagePreference() == LanguagePreference::Chinese;
}

UiText::LanguagePreference UiText::resolvedLanguage()
{
    return resolvedLanguagePreference();
}

QStringList UiText::translationKeyMismatches()
{
    QStringList mismatches;
    const struct {
        const char* name;
        const QHash<QString, QString>* map;
    } maps[] = {
        {"enMap", &enMap()},
        {"zhMap", &zhMap()},
        {"jaMap", &jaMap()},
    };

    for (const auto& expected : maps) {
        for (const auto& actual : maps) {
            if (expected.map == actual.map) {
                continue;
            }
            for (auto it = expected.map->constBegin(); it != expected.map->constEnd(); ++it) {
                if (!actual.map->contains(it.key())) {
                    mismatches.append(QStringLiteral("%1 missing key from %2: %3")
                        .arg(QString::fromLatin1(actual.name), QString::fromLatin1(expected.name), it.key()));
                }
            }
        }
    }
    mismatches.sort();
    mismatches.removeDuplicates();
    return mismatches;
}

bool UiText::hasTranslationKey(const QString& key)
{
    return enMap().contains(key) && zhMap().contains(key) && jaMap().contains(key);
}

QString UiText::preferencesFilePath()
{
    return preferencesPath();
}

QString UiText::currentPreferencesSchema()
{
    return QString::fromLatin1(kPreferencesSchema);
}

QString UiText::storedPreferencesSchema()
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

QJsonObject UiText::loadPreferencesObject()
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

bool UiText::savePreferencesObject(const QJsonObject& root)
{
    return saveJsonObjectToFile(preferencesPath(), normalizedPreferencesRoot(root));
}

QJsonObject UiText::normalizePreferencesObject(const QJsonObject& root)
{
    return normalizedPreferencesRoot(root);
}

QString UiText::themeTokenFromPreferencesObject(const QJsonObject& root)
{
    return root.value(QStringLiteral("ui")).toObject()
        .value(QStringLiteral("theme")).toString(QStringLiteral("system"));
}

void UiText::setThemeTokenInPreferencesObject(QJsonObject* root, const QString& token)
{
    if (root == nullptr) {
        return;
    }
    QJsonObject ui = root->value(QStringLiteral("ui")).toObject();
    ui.insert(QStringLiteral("theme"), token);
    root->insert(QStringLiteral("ui"), ui);
    root->remove(QStringLiteral("theme"));
}

QString UiText::text(const QString& key)
{
    const QHash<QString, QString>* preferredMap = &enMap();
    switch (resolvedLanguagePreference()) {
    case LanguagePreference::Chinese:
        preferredMap = &zhMap();
        break;
    case LanguagePreference::Japanese:
        preferredMap = &jaMap();
        break;
    case LanguagePreference::English:
    case LanguagePreference::System:
    default:
        preferredMap = &enMap();
        break;
    }
    const QString localized = mapValue(*preferredMap, key);
    if (!localized.isEmpty()) {
        return localized;
    }
    const QString english = mapValue(enMap(), key);
    if (!english.isEmpty()) {
        return english;
    }
    return key;
}

namespace {

// UIv2 QML passes visible Chinese source strings (the cover page passes catalog
// keys). Several of those Chinese strings are homographs in the v1 catalog —
// “关闭” is Close / Off / Disabled, “还原” is Reset / restore-window — so a bare
// reverse lookup could land on an unrelated legacy meaning, and QHash iteration
// order would make the pick differ between runs. This table names the canonical
// key for every homograph v2 actually uses.
//
// It maps to a KEY, never to a translation: the catalog stays the single source
// of truth, so a wording fix there reaches QML with no second table to update.
// A source whose meaning is genuinely split *inside* QML (the caption bar's
// 还原 = restore window vs. the HUD font's 还原 = reset) cannot be pinned here —
// that call site passes an explicit key instead.
const QHash<QString, QString>& qmlSourceKeys()
{
    static const QHash<QString, QString> map{
        {QStringLiteral("偏移"), QStringLiteral("latency.offset")},
        {QStringLiteral("停止"), QStringLiteral("dialog.video_export.preview.stop")},
        {QStringLiteral("关闭"), QStringLiteral("action.close")},
        {QStringLiteral("删除书签"), QStringLiteral("editor.bookmark.delete")},
        {QStringLiteral("取消"), QStringLiteral("action.cancel")},
        {QStringLiteral("取消导出"), QStringLiteral("video_export.cancel_export")},
        {QStringLiteral("字体"), QStringLiteral("cover.font")},
        {QStringLiteral("导出"), QStringLiteral("sidebar.export")},
        {QStringLiteral("导出区间"), QStringLiteral("video_export.export_range")},
        {QStringLiteral("平滑亮度"), QStringLiteral("video_export.smooth_brightness")},
        {QStringLiteral("开始"), QStringLiteral("dialog.video_export.range.start")},
        {QStringLiteral("开始导出"), QStringLiteral("video_export.start_export")},
        {QStringLiteral("性能"), QStringLiteral("dialog.preferences.performance_group")},
        {QStringLiteral("打开"), QStringLiteral("action.open")},
        {QStringLiteral("批量导出"), QStringLiteral("action.batch_export")},
        {QStringLiteral("播放"), QStringLiteral("preview.play")},
        {QStringLiteral("显示左下角时间戳"), QStringLiteral("video_export.show_bottom_left_timestamp")},
        {QStringLiteral("暂停"), QStringLiteral("preview.pause")},
        {QStringLiteral("曲绘"), QStringLiteral("cover.jacket")},
        {QStringLiteral("标题"), QStringLiteral("net.title")},
        {QStringLiteral("浏览..."), QStringLiteral("action.browse")},
        {QStringLiteral("清空队列"), QStringLiteral("media_tools.batch_pv_clear_queue")},
        {QStringLiteral("游戏"), QStringLiteral("video_export.gameplay")},
        {QStringLiteral("片头"), QStringLiteral("video_export.intro")},
        {QStringLiteral("皮肤"), QStringLiteral("video_export.skin")},
        {QStringLiteral("结束"), QStringLiteral("dialog.video_export.range.end")},
        {QStringLiteral("编辑器"), QStringLiteral("dialog.preferences.editor_group")},
        {QStringLiteral("背景"), QStringLiteral("dialog.preferences.background_group")},
        {QStringLiteral("自动检测"), QStringLiteral("latency.auto_detect")},
        {QStringLiteral("视频"), QStringLiteral("video_export.video")},
        {QStringLiteral("谱师"), QStringLiteral("net.designer")},
        {QStringLiteral("谱面文件夹"), QStringLiteral("dialog.batch_export.chart_folders")},
        {QStringLiteral("输出"), QStringLiteral("video_export.output")},
        {QStringLiteral("还原"), QStringLiteral("action.reset")},
        {QStringLiteral("重命名书签"), QStringLiteral("editor.bookmark.rename")},
        {QStringLiteral("音频设置"), QStringLiteral("action.audio_settings")},
        {QStringLiteral("预览设置"), QStringLiteral("action.video_settings")},
    };
    return map;
}

// Chinese value -> key, built once. Resolving a source used to iterate the whole
// catalog on every lookup, i.e. on every QML binding evaluation. Duplicate
// Chinese values resolve to the lexicographically first key so the choice is
// stable across runs; every duplicate QML actually uses is pinned by
// qmlSourceKeys() above, which is consulted first.
const QHash<QString, QString>& qmlSourceReverseIndex()
{
    static const QHash<QString, QString> map = [] {
        QHash<QString, QString> reverse;
        const auto& chinese = zhMap();
        for (auto it = chinese.constBegin(); it != chinese.constEnd(); ++it) {
            const auto existing = reverse.constFind(it.value());
            if (existing == reverse.constEnd() || it.key() < existing.value()) {
                reverse.insert(it.value(), it.key());
            }
        }
        return reverse;
    }();
    return map;
}

// UIv2-only strings that have no v1 catalog entry at all. Their three language
// forms live next to the canonical catalog rather than in QML (where `qsTr` had
// no installed translator). Promoting one into the keyed catalog is the
// preferred fix once v1 grows an equivalent label.
const QHash<QString, QPair<QString, QString>>& qmlOnlyEntries()
{
    static const QHash<QString, QPair<QString, QString>> map{
        {QStringLiteral(" · 谱师：%1"), {QStringLiteral(" · Chart Designer: %1"), QStringLiteral(" ・譜面制作者：%1")}},
        {QStringLiteral("%1 个错误，%2 个警告"), {QStringLiteral("%1 errors, %2 warnings"), QStringLiteral("エラー %1 件、警告 %2 件")}},
        {QStringLiteral("%1 尚未更新到 QML 界面。"), {QStringLiteral("%1 has not yet been migrated to the QML interface."), QStringLiteral("%1 はまだ QML インターフェースに移行されていません。")}},
        {QStringLiteral("%1%"), {QStringLiteral("%1%"), QStringLiteral("%1%")}},
        {QStringLiteral("%1x"), {QStringLiteral("%1x"), QStringLiteral("%1倍")}},
        {QStringLiteral("%1，第 %2 行"), {QStringLiteral("%1, line %2"), QStringLiteral("%1、%2 行目")}},
        {QStringLiteral("A / B / C / D / E 各区半径不同，暂不可调。"), {QStringLiteral("The A / B / C / D / E areas use different radii and cannot be adjusted yet."), QStringLiteral("A / B / C / D / E の各エリアは半径が異なるため、現在は調整できません。")}},
        {QStringLiteral("BPM"), {QStringLiteral("BPM"), QStringLiteral("BPM")}},
        {QStringLiteral("Enter 跳转；Ctrl+Shift+B 创建；Delete 删除；F2 重命名；右键打开书签菜单"), {QStringLiteral("Enter to jump; Ctrl+Shift+B to create; Delete to remove; F2 to rename; right-click for the bookmark menu"), QStringLiteral("Enter で移動、Ctrl+Shift+B で作成、Delete で削除、F2 で名前変更、右クリックでブックマークメニュー")}},
        {QStringLiteral("HUD 字体区域"), {QStringLiteral("HUD font area"), QStringLiteral("HUD フォント領域")}},
        {QStringLiteral("Muri"), {QStringLiteral("Muri"), QStringLiteral("無理")}},
        {QStringLiteral("PV 帧率"), {QStringLiteral("PV frame rate"), QStringLiteral("PV フレームレート")}},
        {QStringLiteral("Simai 文件 (*.txt *.simai)"), {QStringLiteral("Simai files (*.txt *.simai)"), QStringLiteral("Simai ファイル (*.txt *.simai)")}},
        {QStringLiteral("「%1」有未保存的更改。"), {QStringLiteral("\"%1\" has unsaved changes."), QStringLiteral("「%1」には未保存の変更があります。")}},
        {QStringLiteral("上一个"), {QStringLiteral("Previous"), QStringLiteral("前へ")}},
        {QStringLiteral("下一个"), {QStringLiteral("Next"), QStringLiteral("次へ")}},
        {QStringLiteral("书签"), {QStringLiteral("Bookmarks"), QStringLiteral("ブックマーク")}},
        {QStringLiteral("书签 %1：%2"), {QStringLiteral("Bookmark %1: %2"), QStringLiteral("ブックマーク %1：%2")}},
        {QStringLiteral("书签与行号：第 %1 行无书签"), {QStringLiteral("Bookmarks and line numbers: no bookmark on line %1"), QStringLiteral("ブックマークと行番号：%1 行目にブックマークはありません")}},
        {QStringLiteral("书签与行号：第 %1 行有书签"), {QStringLiteral("Bookmarks and line numbers: line %1 has a bookmark"), QStringLiteral("ブックマークと行番号：%1 行目にブックマークがあります")}},
        {QStringLiteral("书签名称"), {QStringLiteral("Bookmark name"), QStringLiteral("ブックマーク名")}},
        {QStringLiteral("从左侧打开元数据或难度"), {QStringLiteral("Open metadata or a difficulty from the sidebar"), QStringLiteral("サイドバーからメタデータまたは難易度を開いてください")}},
        {QStringLiteral("保存 simai 文件"), {QStringLiteral("Save Simai file"), QStringLiteral("Simai ファイルを保存")}},
        {QStringLiteral("保存只写入这个难度，其他难度在文件里保持原样；放弃把这个难度还原到上次保存时的内容。"), {QStringLiteral("Save writes only this difficulty and leaves the others unchanged; discard restores this difficulty to its last saved content."), QStringLiteral("保存はこの難易度だけを書き込み、他の難易度は変更しません。破棄するとこの難易度を前回保存時の内容に戻します。")}},
        {QStringLiteral("修正 HUD 文本布局"), {QStringLiteral("Fix HUD text layout"), QStringLiteral("HUD テキスト配置を補正")}},
        {QStringLiteral("入口会保留，功能完成后将在此处提供。"), {QStringLiteral("This entry will remain here and become available when the feature is complete."), QStringLiteral("この入口は残し、機能の完成後にここで提供します。")}},
        {QStringLiteral("全词"), {QStringLiteral("Whole words"), QStringLiteral("単語全体")}},
        {QStringLiteral("全部恢复默认"), {QStringLiteral("Restore all defaults"), QStringLiteral("すべて既定値に戻す")}},
        {QStringLiteral("关于 MiaCode"), {QStringLiteral("About MiaCode"), QStringLiteral("MiaCode について")}},
        {QStringLiteral("关闭 %1"), {QStringLiteral("Close %1"), QStringLiteral("%1 を閉じる")}},
        {QStringLiteral("关闭 (Ctrl+W)"), {QStringLiteral("Close (Ctrl+W)"), QStringLiteral("閉じる (Ctrl+W)")}},
        {QStringLiteral("关闭「%1」"), {QStringLiteral("Close \"%1\""), QStringLiteral("「%1」を閉じる")}},
        {QStringLiteral("关闭文档"), {QStringLiteral("Close document"), QStringLiteral("ドキュメントを閉じる")}},
        {QStringLiteral("内圈亮度"), {QStringLiteral("Inner brightness"), QStringLiteral("内側の明るさ")}},
        {QStringLiteral("切换侧栏"), {QStringLiteral("Toggle sidebar"), QStringLiteral("サイドバーを切り替え")}},
        {QStringLiteral("切换底部面板"), {QStringLiteral("Toggle bottom panel"), QStringLiteral("下部パネルを切り替え")}},
        {QStringLiteral("创建书签"), {QStringLiteral("Create bookmark"), QStringLiteral("ブックマークを作成")}},
        {QStringLiteral("删除当前难度"), {QStringLiteral("Delete current difficulty"), QStringLiteral("現在の難易度を削除")}},
        {QStringLiteral("区分大小写"), {QStringLiteral("Match case"), QStringLiteral("大文字と小文字を区別")}},
        {QStringLiteral("半角输入转换"), {QStringLiteral("Convert half-width input"), QStringLiteral("半角入力を変換")}},
        {QStringLiteral("取消静音"), {QStringLiteral("Unmute"), QStringLiteral("ミュート解除")}},
        {QStringLiteral("右侧"), {QStringLiteral("Right"), QStringLiteral("右側")}},
        {QStringLiteral("启用 clock_count"), {QStringLiteral("Enable clock_count"), QStringLiteral("clock_count を有効化")}},
        {QStringLiteral("在 %1 开头插入 %2 拍（BPM %3）静音，约 %4 秒，并自动备份原文件。"), {QStringLiteral("Insert %2 beats of silence (BPM %3, about %4 seconds) at the beginning of %1. The original file is backed up."), QStringLiteral("%1 の先頭に %2 拍（BPM %3、約 %4 秒）の無音を挿入します。元ファイルは自動でバックアップされます。")}},
        {QStringLiteral("在 %1 开头插入 %2 拍（BPM %3）黑屏，约 %4 秒，并自动备份原文件。"), {QStringLiteral("Insert %2 beats of black screen (BPM %3, about %4 seconds) at the beginning of %1. The original file is backed up."), QStringLiteral("%1 の先頭に %2 拍（BPM %3、約 %4 秒）の黒画面を挿入します。元ファイルは自動でバックアップされます。")}},
        {QStringLiteral("在 track.mp3 开头插入一段静音。"), {QStringLiteral("Insert silence at the start of track.mp3."), QStringLiteral("track.mp3 の先頭に無音を挿入します。")}},
        {QStringLiteral("在背景视频开头插入一段黑屏。"), {QStringLiteral("Insert black screen at the start of the background video."), QStringLiteral("背景動画の先頭に黒画面を挿入します。")}},
        {QStringLiteral("外圈亮度"), {QStringLiteral("Outer brightness"), QStringLiteral("外側の明るさ")}},
        {QStringLiteral("字段源码"), {QStringLiteral("Field source"), QStringLiteral("フィールドのソース")}},
        {QStringLiteral("对齐到 384 分网格"), {QStringLiteral("Align to the 1/384 grid"), QStringLiteral("384 分グリッドに整列")}},
        {QStringLiteral("导入 HUD 字体"), {QStringLiteral("Import HUD font"), QStringLiteral("HUD フォントを読み込む")}},
        {QStringLiteral("导入片头难度卡字体"), {QStringLiteral("Import intro difficulty-card fonts"), QStringLiteral("イントロ難度カードのフォントを読み込む")}},
        {QStringLiteral("将规范化整份谱面正文。"), {QStringLiteral("Normalize the entire chart source."), QStringLiteral("譜面ソース全体を正規化します。")}},
        {QStringLiteral("将规范化选中的第 %1 - %2 行。"), {QStringLiteral("Normalize selected lines %1–%2."), QStringLiteral("選択した %1～%2 行を正規化します。")}},
        {QStringLiteral("展开书签"), {QStringLiteral("Expand bookmarks"), QStringLiteral("ブックマークを展開")}},
        {QStringLiteral("展开难度"), {QStringLiteral("Expand difficulties"), QStringLiteral("難易度を展開")}},
        {QStringLiteral("工具"), {QStringLiteral("Tools"), QStringLiteral("ツール")}},
        {QStringLiteral("等级"), {QStringLiteral("Level"), QStringLiteral("レベル")}},
        {QStringLiteral("延迟"), {QStringLiteral("Offset"), QStringLiteral("オフセット")}},
        {QStringLiteral("左侧"), {QStringLiteral("Left"), QStringLiteral("左側")}},
        {QStringLiteral("常规渲染"), {QStringLiteral("Normal rendering"), QStringLiteral("通常レンダリング")}},
        {QStringLiteral("平滑星星消去动画"), {QStringLiteral("Smooth star-clear animation"), QStringLiteral("星消去アニメーションを滑らかにする")}},
        {QStringLiteral("延迟校准"), {QStringLiteral("Latency calibration"), QStringLiteral("レイテンシー調整")}},
        {QStringLiteral("开始压缩"), {QStringLiteral("Start compression"), QStringLiteral("圧縮を開始")}},
        {QStringLiteral("开始试听"), {QStringLiteral("Start audition"), QStringLiteral("試聴を開始")}},
        {QStringLiteral("当前存在多个谱师名义，请选择要统一使用的值。"), {QStringLiteral("Multiple chart-designer names exist. Choose the value to use everywhere."), QStringLiteral("複数の譜面制作者名があります。統一して使用する値を選択してください。")}},
        {QStringLiteral("当前难度及其正文将从文档中删除。"), {QStringLiteral("The current difficulty and its source will be removed from the document."), QStringLiteral("現在の難易度とその本文をドキュメントから削除します。")}},
        {QStringLiteral("录制中…"), {QStringLiteral("Recording…"), QStringLiteral("記録中…")}},
        {QStringLiteral("总时长 %1 s"), {QStringLiteral("Total duration: %1 s"), QStringLiteral("合計時間：%1 秒")}},
        {QStringLiteral("恢复备份"), {QStringLiteral("Restore backup"), QStringLiteral("バックアップを復元")}},
        {QStringLiteral("恢复备份 (%1)"), {QStringLiteral("Restore backup (%1)"), QStringLiteral("バックアップを復元 (%1)")}},
        {QStringLiteral("恢复本地默认"), {QStringLiteral("Restore local defaults"), QStringLiteral("ローカル既定値に戻す")}},
        {QStringLiteral("恢复默认"), {QStringLiteral("Restore defaults"), QStringLiteral("既定値に戻す")}},
        {QStringLiteral("返回"), {QStringLiteral("Back"), QStringLiteral("戻る")}},
        {QStringLiteral("所有文件 (*)"), {QStringLiteral("All files (*)"), QStringLiteral("すべてのファイル (*)")}},
        {QStringLiteral("所有文件 (*.*)"), {QStringLiteral("All files (*.*)"), QStringLiteral("すべてのファイル (*.*)")}},
        {QStringLiteral("打开 simai 文件"), {QStringLiteral("Open Simai file"), QStringLiteral("Simai ファイルを開く")}},
        {QStringLiteral("打开播放速度预设"), {QStringLiteral("Open playback-speed presets"), QStringLiteral("再生速度プリセットを開く")}},
        {QStringLiteral("打开时间轴缩放预设"), {QStringLiteral("Open timeline zoom presets"), QStringLiteral("タイムライン拡大率プリセットを開く")}},
        {QStringLiteral("打开波形和小节线亮度设置"), {QStringLiteral("Open waveform and beat-line brightness settings"), QStringLiteral("波形と拍線の明るさ設定を開く")}},
        {QStringLiteral("打开预览渲染模式菜单"), {QStringLiteral("Open preview rendering-mode menu"), QStringLiteral("プレビューレンダリングモードメニューを開く")}},
        {QStringLiteral("转换采样率"), {QStringLiteral("Convert sample rate"), QStringLiteral("サンプルレートを変換")}},
        {QStringLiteral("将 track.mp3 的采样率转换为 44100 Hz，并自动备份原文件。"), {QStringLiteral("Convert the sample rate of track.mp3 to 44100 Hz and automatically back up the original file."), QStringLiteral("track.mp3 のサンプルレートを 44100 Hz に変換し、元のファイルを自動的にバックアップします。")}},
        {QStringLiteral("音频前置空白"), {QStringLiteral("Prepend blank audio"), QStringLiteral("音声の先頭に無音を追加")}},
        {QStringLiteral("在 track.mp3 开头插入指定时长的空白音频，并自动备份原文件。"), {QStringLiteral("Insert blank audio of the specified duration at the beginning of track.mp3 and automatically back up the original file."), QStringLiteral("track.mp3 の先頭に指定した長さの無音を挿入し、元のファイルを自動的にバックアップします。")}},
        {QStringLiteral("视频前置黑幕"), {QStringLiteral("Prepend black screen"), QStringLiteral("動画の先頭に黒画面を追加")}},
        {QStringLiteral("在背景视频开头插入指定时长的黑幕，并自动备份原文件。"), {QStringLiteral("Insert a black screen of the specified duration at the beginning of the background video and automatically back up the original file."), QStringLiteral("背景動画の先頭に指定した長さの黒画面を挿入し、元のファイルを自動的にバックアップします。")}},
        {QStringLiteral("将背景视频压缩至 20 MiB 以内，并自动备份原文件。"), {QStringLiteral("Compress the background video to 20 MiB or less and automatically back up the original file."), QStringLiteral("背景動画を 20 MiB 以下に圧縮し、元のファイルを自動的にバックアップします。")}},
        {QStringLiteral("批量压缩视频"), {QStringLiteral("Batch-compress videos"), QStringLiteral("動画を一括圧縮")}},
        {QStringLiteral("选择目录并批量压缩其中的背景视频。"), {QStringLiteral("Choose a folder and batch-compress the background videos it contains."), QStringLiteral("フォルダーを選択し、その中の背景動画を一括圧縮します。")}},
        {QStringLiteral("折叠书签"), {QStringLiteral("Collapse bookmarks"), QStringLiteral("ブックマークを折りたたむ")}},
        {QStringLiteral("折叠难度"), {QStringLiteral("Collapse difficulties"), QStringLiteral("難易度を折りたたむ")}},
        {QStringLiteral("按下新的快捷键，Esc 取消。"), {QStringLiteral("Press a new shortcut; Esc cancels."), QStringLiteral("新しいショートカットを押してください。Esc でキャンセルします。")}},
        {QStringLiteral("撤销"), {QStringLiteral("Undo"), QStringLiteral("元に戻す")}},
        {QStringLiteral("播放速度"), {QStringLiteral("Playback speed"), QStringLiteral("再生速度")}},
        {QStringLiteral("整理语法"), {QStringLiteral("Format syntax"), QStringLiteral("構文を整形")}},
        {QStringLiteral("整谱规范化"), {QStringLiteral("Normalize whole chart"), QStringLiteral("譜面全体を正規化")}},
        {QStringLiteral("无理判定半径"), {QStringLiteral("Muri judgement radius"), QStringLiteral("無理判定半径")}},
        {QStringLiteral("无理检测"), {QStringLiteral("Muri analysis"), QStringLiteral("無理検出")}},
        {QStringLiteral("时间轴亮度"), {QStringLiteral("Timeline brightness"), QStringLiteral("タイムラインの明るさ")}},
        {QStringLiteral("时间轴帧率"), {QStringLiteral("Timeline frame rate"), QStringLiteral("タイムラインのフレームレート")}},
        {QStringLiteral("时间轴缩放"), {QStringLiteral("Timeline zoom"), QStringLiteral("タイムラインの拡大率")}},
        {QStringLiteral("显示所有已打开的编辑器"), {QStringLiteral("Show all open editors"), QStringLiteral("開いているすべてのエディターを表示")}},
        {QStringLiteral("显示谱面信息"), {QStringLiteral("Show chart information"), QStringLiteral("譜面情報を表示")}},
        {QStringLiteral("暂无备份"), {QStringLiteral("No backups"), QStringLiteral("バックアップはありません")}},
        {QStringLiteral("暂无最近文档"), {QStringLiteral("No recent documents"), QStringLiteral("最近のドキュメントはありません")}},
        {QStringLiteral("暂未更新支持"), {QStringLiteral("Not yet available in QML"), QStringLiteral("QML 版ではまだ利用できません")}},
        {QStringLiteral("更多"), {QStringLiteral("More"), QStringLiteral("その他")}},
        {QStringLiteral("最大化"), {QStringLiteral("Maximize"), QStringLiteral("最大化")}},
        {QStringLiteral("最小化"), {QStringLiteral("Minimize"), QStringLiteral("最小化")}},
        {QStringLiteral("未发现 Muri 问题"), {QStringLiteral("No Muri issues found"), QStringLiteral("無理の問題は見つかりませんでした")}},
        {QStringLiteral("未发现验证问题"), {QStringLiteral("No validation issues found"), QStringLiteral("検証上の問題は見つかりませんでした")}},
        {QStringLiteral("查找与替换"), {QStringLiteral("Find and replace"), QStringLiteral("検索と置換")}},
        {QStringLiteral("标题字体预览"), {QStringLiteral("Title-font preview"), QStringLiteral("タイトルフォントのプレビュー")}},
        {QStringLiteral("检查谱面"), {QStringLiteral("Validate chart"), QStringLiteral("譜面を検証")}},
        {QStringLiteral("检测"), {QStringLiteral("Detect"), QStringLiteral("検出")}},
        {QStringLiteral("正在分析…"), {QStringLiteral("Analyzing…"), QStringLiteral("解析中…")}},
        {QStringLiteral("正在取消…"), {QStringLiteral("Cancelling…"), QStringLiteral("キャンセル中…")}},
        {QStringLiteral("正文字体预览"), {QStringLiteral("Body-font preview"), QStringLiteral("本文フォントのプレビュー")}},
        {QStringLiteral("每行一个 &字段=值"), {QStringLiteral("One &field=value per line"), QStringLiteral("1 行に 1 つの &フィールド=値")}},
        {QStringLiteral("添加"), {QStringLiteral("Add"), QStringLiteral("追加")}},
        {QStringLiteral("点击一行以录制新的快捷键。"), {QStringLiteral("Click a row to record a new shortcut."), QStringLiteral("行をクリックして新しいショートカットを記録します。")}},
        {QStringLiteral("片头标题字体"), {QStringLiteral("Intro title font"), QStringLiteral("イントロタイトルフォント")}},
        {QStringLiteral("片头正文字体"), {QStringLiteral("Intro body font"), QStringLiteral("イントロ本文フォント")}},
        {QStringLiteral("画布帧率"), {QStringLiteral("Canvas frame rate"), QStringLiteral("キャンバスのフレームレート")}},
        {QStringLiteral("界面"), {QStringLiteral("Interface"), QStringLiteral("インターフェース")}},
        {QStringLiteral("硬件解码"), {QStringLiteral("Hardware decoding"), QStringLiteral("ハードウェアデコード")}},
        {QStringLiteral("禁用输入法"), {QStringLiteral("Disable IME"), QStringLiteral("IME を無効化")}},
        {QStringLiteral("移除"), {QStringLiteral("Remove"), QStringLiteral("削除")}},
        {QStringLiteral("第 %1 行：%2"), {QStringLiteral("Line %1: %2"), QStringLiteral("%1 行目：%2")}},
        {QStringLiteral("等级文本渲染"), {QStringLiteral("Render level as text"), QStringLiteral("レベルをテキストで描画")}},
        {QStringLiteral("细分"), {QStringLiteral("Subdivision"), QStringLiteral("細分")}},
        {QStringLiteral("统一谱师"), {QStringLiteral("Unify chart designer"), QStringLiteral("譜面制作者を統一")}},
        {QStringLiteral("背景缩放"), {QStringLiteral("Background scaling"), QStringLiteral("背景の拡大縮小")}},
        {QStringLiteral("启用应用背景"), {QStringLiteral("Enable application background"), QStringLiteral("アプリ背景を有効にする")}},
        {QStringLiteral("未选择背景图片"), {QStringLiteral("No background image selected"), QStringLiteral("背景画像が選択されていません")}},
        {QStringLiteral("背景覆盖层"), {QStringLiteral("Background overlays"), QStringLiteral("背景オーバーレイ")}},
        {QStringLiteral("缩放模式"), {QStringLiteral("Scale mode"), QStringLiteral("拡大縮小モード")}},
        {QStringLiteral("工具栏（深色）"), {QStringLiteral("Toolbar (Dark)"), QStringLiteral("ツールバー（ダーク）")}},
        {QStringLiteral("工具栏（浅色）"), {QStringLiteral("Toolbar (Light)"), QStringLiteral("ツールバー（ライト）")}},
        {QStringLiteral("状态栏（深色）"), {QStringLiteral("Status bar (Dark)"), QStringLiteral("ステータスバー（ダーク）")}},
        {QStringLiteral("状态栏（浅色）"), {QStringLiteral("Status bar (Light)"), QStringLiteral("ステータスバー（ライト）")}},
        {QStringLiteral("面板（深色）"), {QStringLiteral("Panel (Dark)"), QStringLiteral("パネル（ダーク）")}},
        {QStringLiteral("面板（浅色）"), {QStringLiteral("Panel (Light)"), QStringLiteral("パネル（ライト）")}},
        {QStringLiteral("编辑器标题（深色）"), {QStringLiteral("Editor header (Dark)"), QStringLiteral("エディターヘッダー（ダーク）")}},
        {QStringLiteral("编辑器标题（浅色）"), {QStringLiteral("Editor header (Light)"), QStringLiteral("エディターヘッダー（ライト）")}},
        {QStringLiteral("输入控件（深色）"), {QStringLiteral("Input controls (Dark)"), QStringLiteral("入力コントロール（ダーク）")}},
        {QStringLiteral("输入控件（浅色）"), {QStringLiteral("Input controls (Light)"), QStringLiteral("入力コントロール（ライト）")}},
        {QStringLiteral("代码编辑器（深色）"), {QStringLiteral("Code editor (Dark)"), QStringLiteral("コードエディター（ダーク）")}},
        {QStringLiteral("代码编辑器（浅色）"), {QStringLiteral("Code editor (Light)"), QStringLiteral("コードエディター（ライト）")}},
        {QStringLiteral("自动"), {QStringLiteral("Automatic"), QStringLiteral("自動")}},
        {QStringLiteral("自定义"), {QStringLiteral("Custom"), QStringLiteral("カスタム")}},
        {QStringLiteral("行 %1，列 %2"), {QStringLiteral("Line %1, column %2"), QStringLiteral("%1 行、%2 列")}},
        {QStringLiteral("表单"), {QStringLiteral("Form"), QStringLiteral("フォーム")}},
        {QStringLiteral("视频解码"), {QStringLiteral("Video decoding"), QStringLiteral("動画デコード")}},
        {QStringLiteral("视图设置"), {QStringLiteral("View settings"), QStringLiteral("ビュー設定")}},
        {QStringLiteral("解码器"), {QStringLiteral("Decoder"), QStringLiteral("デコーダー")}},
        {QStringLiteral("警报"), {QStringLiteral("Alerts"), QStringLiteral("警告")}},
        {QStringLiteral("计数拍"), {QStringLiteral("Count-in beats"), QStringLiteral("カウントイン拍")}},
        {QStringLiteral("设为本地默认"), {QStringLiteral("Set as local default"), QStringLiteral("ローカル既定値に設定")}},
        {QStringLiteral("语言与主题的更改将在重启后生效。"), {QStringLiteral("Language and theme changes take effect after restarting."), QStringLiteral("言語とテーマの変更は再起動後に反映されます。")}},
        {QStringLiteral("谱面"), {QStringLiteral("Chart"), QStringLiteral("譜面")}},
        {QStringLiteral("跟随当前谱面代码位置"), {QStringLiteral("Follow current chart-source position"), QStringLiteral("現在の譜面ソース位置に追従")}},
        {QStringLiteral("跳转到此行"), {QStringLiteral("Jump to this line"), QStringLiteral("この行へ移動")}},
        {QStringLiteral("软件解码"), {QStringLiteral("Software decoding"), QStringLiteral("ソフトウェアデコード")}},
        {QStringLiteral("输出文件夹"), {QStringLiteral("Output folder"), QStringLiteral("出力フォルダー")}},
        {QStringLiteral("还原 HUD 字体"), {QStringLiteral("Reset HUD font"), QStringLiteral("HUD フォントをリセット")}},
        {QStringLiteral("退出全屏预览"), {QStringLiteral("Exit fullscreen preview"), QStringLiteral("全画面プレビューを終了")}},
        {QStringLiteral("选择当前行"), {QStringLiteral("Select current line"), QStringLiteral("現在の行を選択")}},
        {QStringLiteral("选择统一谱师"), {QStringLiteral("Choose unified chart designer"), QStringLiteral("統一する譜面制作者を選択")}},
        {QStringLiteral("选择要扫描的目录"), {QStringLiteral("Choose a folder to scan"), QStringLiteral("スキャンするフォルダーを選択")}},
        {QStringLiteral("重新检查"), {QStringLiteral("Recheck"), QStringLiteral("再チェック")}},
        {QStringLiteral("重置片头难度卡字体"), {QStringLiteral("Reset intro difficulty-card fonts"), QStringLiteral("イントロ難度カードのフォントをリセット")}},
        {QStringLiteral("错误"), {QStringLiteral("Errors"), QStringLiteral("エラー")}},
        {QStringLiteral("难度卡字体"), {QStringLiteral("Difficulty-card fonts"), QStringLiteral("難度カードのフォント")}},
        {QStringLiteral("静音"), {QStringLiteral("Mute"), QStringLiteral("ミュート")}},
        {QStringLiteral("静音 Break 星星尾判音"), {QStringLiteral("Mute Break star-tail judgement sound"), QStringLiteral("Break 星の終端判定音をミュート")}},
        {QStringLiteral("音效音量"), {QStringLiteral("Sound-effect volume"), QStringLiteral("効果音の音量")}},

    };
    return map;
}

QString localizedQmlOnlyEntry(const QPair<QString, QString>& entry, const QString& chineseSource)
{
    switch (resolvedLanguagePreference()) {
    case UiText::LanguagePreference::Japanese:
        return entry.second;
    case UiText::LanguagePreference::Chinese:
        return chineseSource;
    case UiText::LanguagePreference::English:
    case UiText::LanguagePreference::System:
    default:
        return entry.first;
    }
}

}  // namespace

QString UiText::textForQmlSource(const QString& source)
{
    if (source.isEmpty()) {
        return source;
    }

    const auto& pinned = qmlSourceKeys();
    if (const auto it = pinned.constFind(source); it != pinned.constEnd()) {
        return text(it.value());
    }

    // All authored UIv2 strings are Simplified Chinese, and the v1 catalog is the
    // product's canonical wording, so its Chinese table is the reverse index
    // rather than a duplicate copy of those already-translated strings in QML.
    const auto& reverse = qmlSourceReverseIndex();
    if (const auto it = reverse.constFind(source); it != reverse.constEnd()) {
        return text(it.value());
    }

    const auto& only = qmlOnlyEntries();
    if (const auto it = only.constFind(source); it != only.constEnd()) {
        return localizedQmlOnlyEntry(it.value(), source);
    }

    // Deliberately visible rather than an opaque marker, so a new label stays
    // readable while it is promoted into the catalog. ui_text_locale_spec fails
    // the build before that can turn into silent debt: it requires every QML
    // `UiText.text("…")` literal to satisfy hasQmlSourceTranslation() below.
    return source;
}

bool UiText::hasQmlSourceTranslation(const QString& source)
{
    if (source.isEmpty()) {
        return false;
    }
    if (hasTranslationKey(source)) {
        return true;
    }
    // A pinned source has to name a key that really exists: a typo in the table
    // would otherwise reach the UI as the raw key, which is exactly the failure
    // this predicate exists to catch.
    const auto& pinned = qmlSourceKeys();
    if (const auto it = pinned.constFind(source); it != pinned.constEnd()) {
        return hasTranslationKey(it.value());
    }
    return qmlSourceReverseIndex().contains(source) || qmlOnlyEntries().contains(source);
}
