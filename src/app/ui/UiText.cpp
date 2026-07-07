#include "UiText.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace {

// v4 (2026-06-19): re-runs first-run onboarding so existing users see the
// welcome dialog's new 中文输入法 choice. Bump this whenever onboarding gains a
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
    QJsonObject normalized;
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
    if (!ui.contains(kThemeKey)) {
        // Default theme for fresh installs is dark (user-voted default). Existing
        // users keep whatever they explicitly stored (incl. "system"/"light").
        ui.insert(kThemeKey, "dark");
    }
    normalized.insert(kUiSectionKey, ui);

    QJsonObject app = raw.value(kAppSectionKey).toObject();
    if (raw.contains("last_open_dir")) {
        app.insert("last_open_dir", raw.value("last_open_dir").toString());
    }
    if (raw.contains("last_track_path")) {
        app.insert("last_track_path", raw.value("last_track_path").toString());
    }
    if (raw.contains("show_slide_tracks")) {
        app.insert("show_slide_tracks", raw.value("show_slide_tracks").toBool(true));
    }

    QJsonObject preview = app.value(kPreviewSectionKey).toObject();
    if (raw.contains("show_judge_markers")) {
        preview.insert("show_judge_markers", raw.value("show_judge_markers").toBool(false));
    }
    if (raw.contains("show_touch_trail")) {
        preview.insert("show_touch_trail", raw.value("show_touch_trail").toBool(false));
    }
    if (raw.contains("preview_background_brightness")) {
        preview.insert("background_brightness", raw.value("preview_background_brightness").toDouble(0.2));
    }
    if (raw.contains("preview_show_debug_info")) {
        preview.insert("show_debug_info", raw.value("preview_show_debug_info").toBool(false));
    }
    if (raw.contains("preview_audio") && raw.value("preview_audio").isObject()) {
        preview.insert("audio", raw.value("preview_audio").toObject());
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
        QJsonObject audio = preview.value("audio").toObject();
        if (raw.contains("master_volume")) {
            audio.insert("global_volume", raw.value("master_volume").toDouble());
        }
        if (raw.contains("master_restore_volume")) {
            audio.insert("global_restore_volume", raw.value("master_restore_volume").toDouble());
        }
        if (raw.contains("bgm_volume")) {
            audio.insert("track_volume", raw.value("bgm_volume").toDouble());
            audio.insert("bgm_volume", raw.value("bgm_volume").toDouble());
        }
        if (raw.contains("answer_volume")) {
            audio.insert("answer_volume", raw.value("answer_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("judge_volume")) {
            audio.insert("tap_volume", raw.value("judge_volume").toDouble(raw.value("sfx_volume").toDouble()));
            audio.insert("judge_volume", raw.value("judge_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("slide_volume")) {
            audio.insert("slide_volume", raw.value("slide_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("break_volume")) {
            audio.insert("break_volume", raw.value("break_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("break_slide_volume")) {
            audio.insert(
                "break_slide_volume",
                raw.value("break_slide_volume").toDouble(
                    raw.value("slide_volume").toDouble(raw.value("sfx_volume").toDouble())));
        }
        if (raw.contains("break_slide_restore_volume")) {
            audio.insert(
                "break_slide_restore_volume",
                raw.value("break_slide_restore_volume").toDouble(
                    raw.value("break_slide_volume").toDouble(
                        raw.value("slide_volume").toDouble(raw.value("sfx_volume").toDouble()))));
        }
        if (raw.contains("break_slide_tail_cheer_muted")) {
            audio.insert("break_slide_tail_cheer_muted", raw.value("break_slide_tail_cheer_muted").toBool(false));
        }
        if (raw.contains("ex_volume")) {
            audio.insert("ex_volume", raw.value("ex_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("touch_volume")) {
            audio.insert("touch_volume", raw.value("touch_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("touchhold_volume")) {
            const double touchHoldVolume = raw.value("touchhold_volume").toDouble(raw.value("sfx_volume").toDouble());
            audio.insert("touchhold_volume", touchHoldVolume);
            if (!raw.contains("touch_volume")) {
                audio.insert("touch_volume", touchHoldVolume);
            } else {
                audio.insert(
                    "touch_volume",
                    qMax(audio.value("touch_volume").toDouble(), touchHoldVolume)
                );
            }
        }
        if (raw.contains("firework_volume")) {
            audio.insert("firework_volume", raw.value("firework_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("hanabi_volume")) {
            audio.insert("firework_volume", raw.value("hanabi_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        preview.insert("audio", audio);
    }

    if (!preview.isEmpty()) {
        app.insert(kPreviewSectionKey, preview);
    }
    normalized.insert(kAppSectionKey, app);
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
        {"editor.metadata", "Metadata"},
        {"editor.welcome", "Welcome to MiaCode!"},
        {"editor.des", "Des"},
        {"editor.export", "Export"},
        {"metadata.information", "Information"},
        {"metadata.other_fields", "Other &xx Fields"},
        {"metadata.field.title", "title"},
        {"metadata.field.artist", "artist"},
        {"metadata.field.first", "Offset"},
        {"metadata.field.des", "des"},
        {"metadata.field.cover", "cover"},
        {"metadata.empty_hint", "← Click to add a chart difficulty"},
        {"metadata.latency_card.title", "Latency && Offset Calibration"},
        {"metadata.latency_card.open", "Open Latency Settings →"},
        {"sidebar.metadata", "Metadata"},
        {"sidebar.add_difficulty", "+ Add Diff."},
        {"sidebar.export", "Export"},
        {"tab.timeline", "Timeline"},
        {"tab.validation_errors", "Validation Errors"},
        {"dialog.preferences.title", "Preferences"},
        {"dialog.preferences.interface_group", "Appearance"},
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
        {"dialog.welcome.chinese_input", "Chinese input"},
        {"dialog.welcome.chinese_input.disable", "Disable IME"},
        {"dialog.welcome.chinese_input.enable", "Enable IME"},
        {"dialog.welcome.chinese_input.fullwidth", "Convert full-width"},
        {"dialog.welcome.chinese_input.hint",
         "If you don't use comments, choose \"Disable IME\".\n"
         "If you do use comments and want to avoid mistyped input such as 1h【8:1】, "
         "choose \"Convert full-width\"."},
        {"dialog.welcome.get_started", "Get Started"},
        {"dialog.preferences.editor_group", "Editor"},
        {"dialog.preferences.editor_top_display", "Header Field"},
        {"dialog.preferences.editor_top_display.latency", "Offset"},
        {"dialog.preferences.editor_top_display.designer", "Designer"},
        {"dialog.preferences.editor_font_size", "Text Font Size"},
        {"dialog.preferences.editor_line_spacing", "Line Spacing"},
        {"dialog.preferences.editor_half_width_input", "Lock half-width symbol input"},
        {"dialog.preferences.editor_auto_completion", "Auto-complete brackets"},
        {"dialog.preferences.editor_ime_input_disabled", "Disable Chinese IME input"},
        {"dialog.preferences.performance_group", "Performance"},
        {"dialog.preferences.shortcuts_group", "Shortcuts"},
        {"dialog.preferences.shortcuts.edit", "Edit Shortcuts"},
        {"dialog.preferences.shortcuts.reset", "Restore Shortcuts"},
        {"dialog.preferences.shortcuts.title", "Keyboard Shortcuts"},
        {"dialog.preferences.shortcuts.command", "Command"},
        {"dialog.preferences.shortcuts.keybinding", "Keybinding"},
        {"dialog.preferences.shortcuts.change", "Change Keybinding"},
        {"dialog.preferences.shortcuts.capture_title", "Change Keybinding"},
        {"dialog.preferences.shortcuts.capture_prompt", "Press the desired key combination, then press Enter."},
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
        {"dialog.render_settings.video.brightness_outer", "Outer Brightness"},
        {"dialog.render_settings.video.brightness_inner", "Inner Brightness"},
        {"dialog.render_settings.video.layout_square_scale", "Stage Display Scale"},
        {"dialog.render_settings.video.flow_speed", "Flow Speed"},
        {"dialog.render_settings.video.tap_flow_speed", "Tap Flow Speed"},
        {"dialog.render_settings.video.touch_flow_speed", "Touch Flow Speed"},
        {"dialog.render_settings.video.skin", "Skin"},
        {"dialog.render_settings.video.skin.standard", "Standard"},
        {"dialog.render_settings.video.skin.dx", "DX"},
        {"dialog.render_settings.video.skin.import", "Import..."},
        {"dialog.render_settings.video.scale_mode", "Background / PV Scale Mode"},
        {"dialog.render_settings.video.canvas_aspect", "Preview Canvas Aspect"},
        {"dialog.render_settings.gameplay.judge_effect", "Judge Effect Display"},
        {"dialog.render_settings.gameplay.judge_effect.slide", "slide"},
        {"dialog.render_settings.gameplay.judge_effect.tap", "tap"},
        {"dialog.render_settings.gameplay.judge_effect.touch", "touch"},
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
        {"dialog.video_export.option.hud_font", "HUD Font"},
        {"dialog.video_export.option.hud_font_settings", "Font Settings"},
        {"dialog.video_export.option.import_hud_font", "Import Font"},
        {"dialog.video_export.option.reset_hud_font", "Reset"},
        {"dialog.video_export.option.hud_font_default", "Default font"},
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
        {"dialog.batch_export.difficulty", "Difficulty"},
        {"dialog.batch_export.output_dir", "Output Folder"},
        {"dialog.batch_export.chart_folders", "Chart Folders"},
        {"dialog.batch_export.add_folders", "Add Folders"},
        {"dialog.batch_export.remove_selected", "Remove Selected"},
        {"dialog.batch_export.clear", "Clear"},
        {"dialog.batch_export.select_charts", "Select Chart Folders"},
        {"dialog.batch_export.error.no_difficulty", "No active difficulty is selected."},
        {"dialog.batch_export.error.no_preview", "Preview canvas is not initialized."},
        {"dialog.batch_export.error.no_chart_dirs", "Please add at least one chart folder."},
        {"dialog.batch_export.error.no_difficulties", "Please select at least one difficulty."},
        {"dialog.batch_export.error.no_output_dir", "Please choose an output folder."},
        {"dialog.batch_export.error.output_dir_create_failed", "Failed to create output folder."},
        {"dialog.batch_export.error.invalid_selection", "Some folders were skipped because required files are missing."},
        {"dialog.batch_export.error.invalid_folder", "The selected path is not a valid folder."},
        {"dialog.batch_export.error.missing_chart_file", "Missing majdata.txt (or maidata.txt)."},
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
        {"dialog.preferences.extensions_group", "Extensions"},
        {"dialog.preferences.extensions.open_folder", "Open Extensions Folder"},
        {"dialog.preferences.extensions.refresh", "Refresh Extensions"},
        {"dialog.preferences.extensions.open_logs", "Open Logs Folder"},
        {"dialog.preferences.extensions.enabled", "Enabled"},
        {"dialog.preferences.extensions.name", "Extension"},
        {"dialog.preferences.extensions.version", "Version"},
        {"dialog.preferences.extensions.contributions", "Contributions"},
        {"dialog.preferences.extensions.status", "Status"},
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
        {"status.normalize.already_normalized", "Format Chart: already normalized."},
        {"status.normalize.applied", "Format Chart applied: %1 measure line(s)."},
        {"status.transform.mirror_lr", "Mirror Left/Right applied."},
        {"status.transform.mirror_ud", "Mirror Up/Down applied."},
        {"status.transform.rotate_180", "Rotate 180 applied."},
        {"status.transform.rotate_ccw_45", "Rotate -45 applied."},
        {"status.transform.rotate_cw_45", "Rotate +45 applied."},

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

        {"editor.metadata", "谱面信息设置"},
        {"editor.welcome", "欢迎使用MiaCode！"},
        {"editor.des", "谱师"},
        {"editor.export", "导出"},

        {"metadata.information", "基础信息"},
        {"metadata.other_fields", "其他 &xx 字段"},
        {"metadata.field.title", "标题"},
        {"metadata.field.artist", "曲师"},
        {"metadata.field.first", "偏移"},
        {"metadata.field.des", "谱师"},
        {"metadata.field.cover", "曲绘"},
        {"metadata.empty_hint", "← 点击添加谱面难度"},
        {"metadata.latency_card.title", "延迟与偏移校准"},
        {"metadata.latency_card.open", "打开延迟设置 →"},

        {"sidebar.metadata", "谱面信息设置"},
        {"sidebar.add_difficulty", "添加难度"},
        {"sidebar.export", "导出"},

        {"tab.timeline", "时间轴"},
        {"tab.validation_errors", "校验错误"},

        {"dialog.preferences.title", "首选项"},
        {"dialog.preferences.interface_group", "外观"},
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
        {"dialog.welcome.chinese_input", "中文输入法"},
        {"dialog.welcome.chinese_input.disable", "关闭输入法"},
        {"dialog.welcome.chinese_input.enable", "开启输入法"},
        {"dialog.welcome.chinese_input.fullwidth", "转换全角字符"},
        {"dialog.welcome.chinese_input.hint",
         "如果您不使用注释功能，推荐选择“关闭输入法”；\n"
         "如果您使用注释功能，且希望减少类似1h【8:1】的错误输入，推荐选择“转换全角字符”。"},
        {"dialog.welcome.get_started", "开始使用"},
        {"dialog.preferences.editor_group", "编辑器"},
        {"dialog.preferences.editor_top_display", "顶部显示"},
        {"dialog.preferences.editor_top_display.latency", "偏移"},
        {"dialog.preferences.editor_top_display.designer", "谱师"},
        {"dialog.preferences.editor_font_size", "字号"},
        {"dialog.preferences.editor_line_spacing", "行距"},
        {"dialog.preferences.editor_half_width_input", "锁定半角符号输入"},
        {"dialog.preferences.editor_auto_completion", "自动补全括号"},
        {"dialog.preferences.editor_ime_input_disabled", "禁止中文输入法输入"},
        {"dialog.preferences.performance_group", "性能"},
        {"dialog.preferences.shortcuts_group", "快捷键"},
        {"dialog.preferences.shortcuts.edit", "修改快捷键"},
        {"dialog.preferences.shortcuts.reset", "还原快捷键"},
        {"dialog.preferences.shortcuts.title", "键盘快捷方式"},
        {"dialog.preferences.shortcuts.command", "命令"},
        {"dialog.preferences.shortcuts.keybinding", "键绑定"},
        {"dialog.preferences.shortcuts.change", "更改键绑定"},
        {"dialog.preferences.shortcuts.capture_title", "更改键绑定"},
        {"dialog.preferences.shortcuts.capture_prompt", "先按所需的组合键，再按 Enter 键。"},
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
        {"dialog.render_settings.video.brightness_outer", "亮度（外侧）"},
        {"dialog.render_settings.video.brightness_inner", "亮度（内侧）"},
        {"dialog.render_settings.video.layout_square_scale", "谱面显示范围"},
        {"dialog.render_settings.video.flow_speed", "流速"},
        {"dialog.render_settings.video.tap_flow_speed", "Tap流速"},
        {"dialog.render_settings.video.touch_flow_speed", "Touch流速"},
        {"dialog.render_settings.video.skin", "皮肤"},
        {"dialog.render_settings.video.skin.standard", "标准"},
        {"dialog.render_settings.video.skin.dx", "DX"},
        {"dialog.render_settings.video.skin.import", "导入..."},
        {"dialog.render_settings.video.scale_mode", "背景 / PV 缩放模式"},
        {"dialog.render_settings.video.canvas_aspect", "预览画布比例"},
        {"dialog.render_settings.gameplay.judge_effect", "判定效果显示"},
        {"dialog.render_settings.gameplay.judge_effect.slide", "slide"},
        {"dialog.render_settings.gameplay.judge_effect.tap", "tap"},
        {"dialog.render_settings.gameplay.judge_effect.touch", "touch"},
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
        {"dialog.video_export.option.hud_font", "HUD 字体"},
        {"dialog.video_export.option.hud_font_settings", "字体设置"},
        {"dialog.video_export.option.import_hud_font", "导入字体"},
        {"dialog.video_export.option.reset_hud_font", "还原"},
        {"dialog.video_export.option.hud_font_default", "默认字体"},
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
        {"dialog.batch_export.difficulty", "难度"},
        {"dialog.batch_export.output_dir", "导出文件夹"},
        {"dialog.batch_export.chart_folders", "谱面文件夹"},
        {"dialog.batch_export.add_folders", "添加文件夹"},
        {"dialog.batch_export.remove_selected", "移除选中"},
        {"dialog.batch_export.clear", "清空"},
        {"dialog.batch_export.select_charts", "选择谱面文件夹"},
        {"dialog.batch_export.error.no_difficulty", "当前没有激活难度。"},
        {"dialog.batch_export.error.no_preview", "预览画布尚未初始化。"},
        {"dialog.batch_export.error.no_chart_dirs", "请至少添加一个谱面文件夹。"},
        {"dialog.batch_export.error.no_difficulties", "请至少选择一个难度。"},
        {"dialog.batch_export.error.no_output_dir", "请选择导出文件夹。"},
        {"dialog.batch_export.error.output_dir_create_failed", "无法创建导出文件夹。"},
        {"dialog.batch_export.error.invalid_selection", "部分文件夹缺少必要文件，已跳过。"},
        {"dialog.batch_export.error.invalid_folder", "所选路径不是有效文件夹。"},
        {"dialog.batch_export.error.missing_chart_file", "缺少 net.txt（或 maidata.txt）。"},
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

        // 2026-07-07 audit backfill: these keys existed only in jaMap, so the
        // Chinese UI silently fell back to the call-site English string. Keep
        // zhMap and jaMap key sets identical — ui_text_locale_spec enforces it.
        {"about.platform", "发行环境"},
        {"about.build_type", "构建类型"},
        {"dialog.batch_export.error.export_failed", "导出失败。"},
        {"dialog.batch_export.error.invalid_first", "谱面信息的 &first 数值无效。"},
        {"dialog.batch_export.error.skin_missing", "未找到预览皮肤素材。"},
        {"dialog.normalize.title", "整理谱面"},
        {"dialog.normalize.failed", "无法整理当前谱面。"},
        {"dialog.preferences.extensions_group", "扩展"},
        {"dialog.preferences.extensions.open_folder", "打开扩展文件夹"},
        {"dialog.preferences.extensions.refresh", "刷新扩展"},
        {"dialog.preferences.extensions.open_logs", "打开日志位置"},
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
        {"status.normalize.already_normalized", "谱面整理：已经是整理后的格式。"},
        {"status.normalize.applied", "已应用谱面整理：%1 小节行。"},
        {"status.transform.mirror_lr", "已应用左右镜像。"},
        {"status.transform.mirror_ud", "已应用上下镜像。"},
        {"status.transform.rotate_180", "已应用旋转 180°。"},
        {"status.transform.rotate_ccw_45", "已应用逆时针旋转 45°。"},
        {"status.transform.rotate_cw_45", "已应用顺时针旋转 45°。"},

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
        {"cover.opacity", "不透明度"},
        {"cover.open", "打开"},
        {"cover.open_recent", "打开最近"},
        {"cover.play_pause_space", "播放 / 暂停（空格）"},
        {"cover.play_pause_visual_only", "播放 / 暂停（仅画面）"},
        {"cover.preset_name", "预设名称："},
        {"cover.pure_chart_frame", "纯谱面帧（无卡片）"},
        {"cover.rename_preset", "重命名预设"},
        {"cover.render_and_save_the_cover", "渲染并保存封面图片"},
        {"cover.render_level_as_text", "等级文本渲染"},
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
        {"action.discard", "破棄"},
        {"action.cancel", "取り消し"},
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
        {"action.audio_settings", "音の設定"},
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

        {"editor.metadata", "譜面情報設定"},
        {"editor.welcome", "MiaCode へようこそ！"},
        {"editor.des", "譜面作者"},
        {"editor.export", "出力"},

        {"metadata.information", "基本情報"},
        {"metadata.other_fields", "その他の &xx 欄"},
        {"metadata.field.title", "タイトル"},
        {"metadata.field.artist", "作曲者"},
        {"metadata.field.first", "開始ずれ"},
        {"metadata.field.des", "譜面作者"},
        {"metadata.field.cover", "ジャケット"},
        {"metadata.empty_hint", "← クリックして難易度を追加"},
        {"metadata.latency_card.title", "遅延と開始ずれの調整"},
        {"metadata.latency_card.open", "遅延設定を開く →"},

        {"sidebar.metadata", "譜面情報設定"},
        {"sidebar.add_difficulty", "難易度を追加"},
        {"sidebar.export", "出力"},

        {"tab.timeline", "時間軸"},
        {"tab.validation_errors", "検査エラー"},

        {"dialog.preferences.title", "設定"},
        {"dialog.preferences.interface_group", "外観"},
        {"dialog.preferences.language", "言語"},
        {"dialog.preferences.language.system", "システムに合わせる"},
        {"dialog.preferences.language.english", "English"},
        {"dialog.preferences.language.chinese", "简体中文"},
        {"dialog.preferences.language.japanese", "日本語"},
        {"dialog.preferences.theme", "テーマ"},
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
        {"dialog.welcome.theme", "色テーマ"},
        {"dialog.welcome.theme.light", "明るい"},
        {"dialog.welcome.theme.dark", "暗い"},
        {"dialog.welcome.chinese_input", "中国語入力"},
        {"dialog.welcome.chinese_input.disable", "入力を無効"},
        {"dialog.welcome.chinese_input.enable", "入力を有効"},
        {"dialog.welcome.chinese_input.fullwidth", "全角文字へ変換"},
        {"dialog.welcome.chinese_input.hint",
         "コメント機能を使わない場合は「入力を無効」がおすすめです。\n"
         "コメント機能を使い、1h【8:1】のような誤入力を減らしたい場合は「全角文字へ変換」がおすすめです。"},
        {"dialog.welcome.get_started", "始める"},
        {"dialog.preferences.editor_group", "編集"},
        {"dialog.preferences.editor_top_display", "上部表示"},
        {"dialog.preferences.editor_top_display.latency", "開始ずれ"},
        {"dialog.preferences.editor_top_display.designer", "譜面作者"},
        {"dialog.preferences.editor_font_size", "文字サイズ"},
        {"dialog.preferences.editor_line_spacing", "行間"},
        {"dialog.preferences.editor_half_width_input", "半角記号入力に固定"},
        {"dialog.preferences.editor_auto_completion", "括弧を自動補完"},
        {"dialog.preferences.editor_ime_input_disabled", "中国語入力を無効"},
        {"dialog.preferences.performance_group", "性能"},
        {"dialog.preferences.shortcuts_group", "ショートカット"},
        {"dialog.preferences.shortcuts.edit", "キー設定を編集"},
        {"dialog.preferences.shortcuts.reset", "キー設定を戻す"},
        {"dialog.preferences.shortcuts.title", "キー設定"},
        {"dialog.preferences.shortcuts.command", "命令"},
        {"dialog.preferences.shortcuts.keybinding", "キー"},
        {"dialog.preferences.shortcuts.change", "キーを変更"},
        {"dialog.preferences.shortcuts.capture_title", "キーを変更"},
        {"dialog.preferences.shortcuts.capture_prompt", "使うキーの組み合わせを押してから Enter を押してください。"},
        {"dialog.preferences.shortcuts.capture_prompt_hold", "押し続け用のキーを押してから Enter を押してください（Alt など単独の修飾キーも可）。"},
        {"dialog.preferences.shortcuts.reset_confirm_title", "キー設定を戻す"},
        {"dialog.preferences.shortcuts.reset_confirm_message", "変更できるキー設定をすべて初期値に戻しますか？"},
        {"dialog.preferences.extensions_group", "拡張"},
        {"dialog.preferences.extensions.open_folder", "拡張フォルダーを開く"},
        {"dialog.preferences.extensions.refresh", "拡張を更新"},
        {"dialog.preferences.extensions.open_logs", "ログの場所を開く"},
        {"dialog.preferences.extensions.enabled", "有効"},
        {"dialog.preferences.extensions.name", "拡張"},
        {"dialog.preferences.extensions.version", "版"},
        {"dialog.preferences.extensions.contributions", "追加内容"},
        {"dialog.preferences.extensions.status", "状態"},
        {"action.reset", "戻す"},
        {"dialog.preferences.restart_title", "再起動が必要です"},
        {"dialog.preferences.restart_message", "言語設定を保存しました。メニュー、字体、画面の文字に反映するには MiaCode を再起動してください。"},
        {"dialog.unsaved_changes.title", "未保存の変更"},
        {"dialog.unsaved_changes.message", "今の文書に未保存の変更があります。先に保存しますか？"},
        {"dialog.unsaved_field_changes.title", "未保存の欄があります"},
        {"dialog.unsaved_field_changes.message", "%1 に未保存の変更があります。切り替える前に保存しますか？"},
        {"dialog.unsaved_field_changes.field.metadata", "譜面情報"},

        {"dialog.audio_settings.title", "音の設定"},
        {"dialog.video_settings.title", "表示設定"},
        {"dialog.render_settings.audio_group", "音"},
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
        {"dialog.render_settings.button.close", "閉じる"},
        {"dialog.render_settings.button.set_software_default_audio", "本体の初期値として保存"},
        {"dialog.render_settings.button.restore_project_default", "本体の初期値を適用"},
        {"dialog.render_settings.button.mute_non_bgm", "BGM 以外を消音"},
        {"dialog.render_settings.button.restore_non_bgm", "BGM 以外の音量を戻す"},
        {"dialog.render_settings.option.enabled", "有効"},
        {"dialog.render_settings.option.disabled", "無効"},
        {"dialog.render_settings.audio.master", "全体音量"},
        {"dialog.render_settings.audio.bgm", "BGM 音量"},
        {"dialog.render_settings.audio.answer", "Answer 音量"},
        {"dialog.render_settings.audio.tap", "Tap 音量"},
        {"dialog.render_settings.audio.break", "Break 音量"},
        {"dialog.render_settings.audio.break_slide", "Break Slide 音量"},
        {"dialog.render_settings.audio.slide", "Slide 音量"},
        {"dialog.render_settings.audio.break_slide_tail_cheer_mute", "breakslide 終端の歓声音を消す"},
        {"dialog.render_settings.audio.ex", "EX 音量"},
        {"dialog.render_settings.audio.touch", "Touch 音量"},
        {"dialog.render_settings.audio.track", "Track 音量"},
        {"dialog.render_settings.audio.global", "Global 音量"},
        {"dialog.render_settings.audio.firework", "Firework 音量"},
        {"dialog.render_settings.audio.button.mute", "%1 を消音"},
        {"dialog.render_settings.audio.button.unmute", "%1 を戻す"},
        {"dialog.render_settings.music.intro_sound", "開始音"},
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
        {"dialog.render_settings.video.brightness_outer", "明るさ（外側）"},
        {"dialog.render_settings.video.brightness_inner", "明るさ（内側）"},
        {"dialog.render_settings.video.layout_square_scale", "譜面表示範囲"},
        {"dialog.render_settings.video.flow_speed", "流速"},
        {"dialog.render_settings.video.tap_flow_speed", "Tap 流速"},
        {"dialog.render_settings.video.touch_flow_speed", "Touch 流速"},
        {"dialog.render_settings.video.skin", "スキン"},
        {"dialog.render_settings.video.skin.standard", "標準"},
        {"dialog.render_settings.video.skin.dx", "DX"},
        {"dialog.render_settings.video.skin.import", "取り込み..."},
        {"dialog.render_settings.video.scale_mode", "背景 / PV 拡大方法"},
        {"dialog.render_settings.video.canvas_aspect", "プレビュー画面比"},
        {"dialog.render_settings.gameplay.judge_effect", "判定演出表示"},
        {"dialog.render_settings.gameplay.judge_effect.slide", "slide"},
        {"dialog.render_settings.gameplay.judge_effect.tap", "tap"},
        {"dialog.render_settings.gameplay.judge_effect.touch", "touch"},
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
        {"dialog.video_export.button.cancel", "取り消し"},
        {"dialog.video_export.button.close", "閉じる"},
        {"dialog.video_export.option.show_object_stats", "物量を表示"},
        {"dialog.video_export.option.show_chart_info", "左上に譜面情報を表示"},
        {"dialog.video_export.option.hud_font", "HUD 字体"},
        {"dialog.video_export.option.hud_font_settings", "字体設定"},
        {"dialog.video_export.option.import_hud_font", "字体を取り込む"},
        {"dialog.video_export.option.reset_hud_font", "戻す"},
        {"dialog.video_export.option.hud_font_default", "既定の字体"},
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
        {"dialog.batch_export.difficulty", "難易度"},
        {"dialog.batch_export.output_dir", "出力フォルダー"},
        {"dialog.batch_export.chart_folders", "譜面フォルダー"},
        {"dialog.batch_export.add_folders", "フォルダーを追加"},
        {"dialog.batch_export.remove_selected", "選択を削除"},
        {"dialog.batch_export.clear", "空にする"},
        {"dialog.batch_export.select_charts", "譜面フォルダーを選択"},
        {"dialog.batch_export.error.no_difficulty", "有効な難易度がありません。"},
        {"dialog.batch_export.error.no_preview", "プレビュー画面がまだ初期化されていません。"},
        {"dialog.batch_export.error.no_chart_dirs", "譜面フォルダーを 1 つ以上追加してください。"},
        {"dialog.batch_export.error.no_difficulties", "難易度を 1 つ以上選んでください。"},
        {"dialog.batch_export.error.no_output_dir", "出力フォルダーを選んでください。"},
        {"dialog.batch_export.error.output_dir_create_failed", "出力フォルダーを作成できません。"},
        {"dialog.batch_export.error.invalid_selection", "必要なファイルがないフォルダーは飛ばしました。"},
        {"dialog.batch_export.error.invalid_folder", "選んだ場所は有効なフォルダーではありません。"},
        {"dialog.batch_export.error.missing_chart_file", "net.txt（または maidata.txt）がありません。"},
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
        {"status.normalize.already_normalized", "譜面整形：すでに整形済みです。"},
        {"status.normalize.applied", "譜面整形を適用しました：%1 小節行。"},
        {"status.transform.mirror_lr", "左右反転を適用しました。"},
        {"status.transform.mirror_ud", "上下反転を適用しました。"},
        {"status.transform.rotate_180", "180° 回転を適用しました。"},
        {"status.transform.rotate_ccw_45", "-45° 回転を適用しました。"},
        {"status.transform.rotate_cw_45", "+45° 回転を適用しました。"},


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

namespace UiText {

LanguagePreference preferredLanguage()
{
    return preferredLanguageStorage();
}

void setPreferredLanguage(LanguagePreference preference)
{
    preferredLanguageStorage() = preference;
    saveStoredLanguagePreference(preference);
}

ThemePreference preferredTheme()
{
    return preferredThemeStorage();
}

void setPreferredTheme(ThemePreference preference)
{
    preferredThemeStorage() = preference;
    saveStoredThemePreference(preference);
}

bool isChineseUi()
{
    return resolvedLanguagePreference() == LanguagePreference::Chinese;
}

LanguagePreference resolvedLanguage()
{
    return resolvedLanguagePreference();
}

QString localized(const QString& en, const QString& zh, const QString& ja)
{
    switch (resolvedLanguagePreference()) {
    case LanguagePreference::Chinese:
        return zh.isEmpty() ? en : zh;
    case LanguagePreference::Japanese: {
        if (!ja.isEmpty()) {
            return ja;
        }
        const auto it = japaneseByChineseText().constFind(zh);
        if (it != japaneseByChineseText().constEnd()) {
            return it.value();
        }
        return en;
    }
    case LanguagePreference::English:
    case LanguagePreference::System:
    default:
        return en;
    }
}

QStringList translationKeyMismatches()
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

bool hasTranslationKey(const QString& key)
{
    return enMap().contains(key) && zhMap().contains(key) && jaMap().contains(key);
}

QString preferencesFilePath()
{
    return preferencesPath();
}

QString currentPreferencesSchema()
{
    return QString::fromLatin1(kPreferencesSchema);
}

QString storedPreferencesSchema()
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

QJsonObject loadPreferencesObject()
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

bool savePreferencesObject(const QJsonObject& root)
{
    return saveJsonObjectToFile(preferencesPath(), normalizedPreferencesRoot(root));
}

QString text(const QString& key)
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

}  // namespace UiText
