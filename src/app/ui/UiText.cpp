#include "UiText.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>

namespace {

constexpr auto kPreferencesSchema = "miacode_preferences_v3";
constexpr auto kUiSectionKey = "ui";
constexpr auto kAppSectionKey = "app";
constexpr auto kPreviewSectionKey = "preview";
constexpr auto kLanguageKey = "language";
constexpr auto kThemeKey = "theme";

QString preferencesPath()
{
    const QDir appDir(QCoreApplication::applicationDirPath());
    return appDir.filePath(".miacode_preferences.json");
}

QString legacyPreferencesFilePath()
{
    QString configRoot = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (configRoot.isEmpty()) {
        return QString();
    }
    const QDir configDir(configRoot);
    return configDir.filePath("preferences.json");
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
        ui.insert(kThemeKey, "system");
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
    if (raw.contains("bgm_volume")
        || raw.contains("sfx_volume")
        || raw.contains("answer_volume")
        || raw.contains("judge_volume")
        || raw.contains("break_slide_volume")
        || raw.contains("firework_volume")) {
        QJsonObject audio = preview.value("audio").toObject();
        if (raw.contains("bgm_volume")) {
            audio.insert("bgm_volume", raw.value("bgm_volume").toDouble());
        }
        if (raw.contains("answer_volume")) {
            audio.insert("answer_volume", raw.value("answer_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("judge_volume")) {
            audio.insert("judge_volume", raw.value("judge_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("slide_volume")) {
            audio.insert("slide_volume", raw.value("slide_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("break_volume")) {
            audio.insert("break_volume", raw.value("break_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("break_slide_volume")) {
            audio.insert("break_slide_volume", raw.value("break_slide_volume").toDouble(raw.value("slide_volume").toDouble(raw.value("sfx_volume").toDouble())));
        }
        if (raw.contains("ex_volume")) {
            audio.insert("ex_volume", raw.value("ex_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("touch_volume")) {
            audio.insert("touch_volume", raw.value("touch_volume").toDouble(raw.value("sfx_volume").toDouble()));
        }
        if (raw.contains("touchhold_volume")) {
            audio.insert("touchhold_volume", raw.value("touchhold_volume").toDouble(raw.value("sfx_volume").toDouble()));
            if (!raw.contains("touch_volume")) {
                audio.insert("touch_volume", raw.value("touchhold_volume").toDouble(raw.value("sfx_volume").toDouble()));
            }
        }
        if (raw.contains("firework_volume")) {
            audio.insert("firework_volume", raw.value("firework_volume").toDouble(raw.value("sfx_volume").toDouble()));
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

bool languageListPrefersChinese(const QStringList& languages)
{
    for (const QString& language : languages) {
        const QString token = normalizedLanguageToken(language);
        if (token.startsWith("zh")) {
            return true;
        }
        if (token.startsWith("en")) {
            return false;
        }
    }
    return false;
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
    if (languageListPrefersChinese(uiLanguages)) {
        return UiText::LanguagePreference::Chinese;
    }

    const QString localeName = normalizedLanguageToken(QLocale::system().name());
    if (localeName.startsWith("zh")) {
        return UiText::LanguagePreference::Chinese;
    }

    return UiText::LanguagePreference::English;
}

const QHash<QString, QString>& zhMap()
{
    static const QHash<QString, QString> map{
        {"menu.file", "文件(&F)"},
        {"menu.tools", "工具(&T)"},
        {"menu.transform", "变换(&T)"},
        {"menu.help", "帮助(&H)"},

        {"action.new", "新建"},
        {"action.open", "打开"},
        {"action.save", "保存"},
        {"action.ok", "确定"},
        {"action.discard", "放弃"},
        {"action.cancel", "取消"},
        {"action.close", "关闭"},
        {"action.yes", "是"},
        {"action.no", "否"},
        {"action.save_as", "另存为"},
        {"action.preferences", "首选项..."},
        {"action.about", "关于"},
        {"action.cut", "剪切"},
        {"action.copy", "复制"},
        {"action.paste", "粘贴"},
        {"action.undo", "撤回"},
        {"action.redo", "重做"},
        {"action.validate", "校验 Simai"},
        {"action.stop_preview", "停止预览"},
        {"action.pause_preview", "播放/暂停预览"},
        {"action.preview_speed_down", "播放速度 ↓"},
        {"action.preview_speed_up", "播放速度 ↑"},
        {"action.audio_settings", "音频设置..."},
        {"action.video_settings", "视频设置..."},
        {"toolbar.export", "导出"},
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

        {"editor.metadata", "谱面信息设置"},
        {"editor.welcome", "欢迎使用MiaCode！"},
        {"editor.des", "谱师"},

        {"metadata.information", "基础信息"},
        {"metadata.other_fields", "其他 &xx 字段"},
        {"metadata.field.title", "标题"},
        {"metadata.field.artist", "曲师"},
        {"metadata.field.first", "偏移"},
        {"metadata.field.des", "谱师"},
        {"metadata.empty_hint", "← 点击添加谱面难度"},

        {"sidebar.metadata", "谱面信息设置"},
        {"sidebar.add_difficulty", "添加难度"},

        {"tab.timeline", "时间轴"},
        {"tab.validation_errors", "校验错误"},

        {"dialog.preferences.title", "首选项"},
        {"dialog.preferences.interface_group", "界面"},
        {"dialog.preferences.language", "语言"},
        {"dialog.preferences.language.system", "跟随系统"},
        {"dialog.preferences.language.english", "English"},
        {"dialog.preferences.language.chinese", "简体中文"},
        {"dialog.preferences.theme", "主题"},
        {"dialog.preferences.theme.system", "跟随系统"},
        {"dialog.preferences.theme.light", "浅色"},
        {"dialog.preferences.theme.dark", "深色"},
        {"dialog.preferences.editor_group", "编辑器"},
        {"dialog.preferences.editor_font_size", "文本框字号"},
        {"dialog.preferences.editor_line_spacing", "行距"},
        {"dialog.preferences.restart_title", "需要重启"},
        {"dialog.preferences.restart_message", "语言设置已保存。请重启 MiaCode 以应用菜单、字体和界面文本。"},
        {"dialog.unsaved_changes.title", "未保存的更改"},
        {"dialog.unsaved_changes.message", "当前文档有未保存的更改。是否先保存？"},
        {"dialog.unsaved_field_changes.title", "未保存的字段更改"},
        {"dialog.unsaved_field_changes.message", "%1 有未保存的更改。切换前是否保存？"},
        {"dialog.unsaved_field_changes.field.metadata", "谱面信息"},

        {"dialog.audio_settings.title", "音频设置"},
        {"dialog.video_settings.title", "视频设置"},
        {"dialog.render_settings.audio_group", "音频"},
        {"dialog.render_settings.video_group", "视频"},
        {"dialog.render_settings.preview_group", "预览"},
        {"dialog.render_settings.button.close", "关闭"},
        {"dialog.render_settings.button.restore_project_default", "恢复默认"},
        {"dialog.render_settings.audio.bgm", "BGM 音量"},
        {"dialog.render_settings.audio.answer", "Answer 音量"},
        {"dialog.render_settings.audio.judge", "Judge 音量"},
        {"dialog.render_settings.audio.break", "Break 音量"},
        {"dialog.render_settings.audio.slide", "Slide 音量"},
        {"dialog.render_settings.audio.ex", "EX 音量"},
        {"dialog.render_settings.audio.touch", "Touch 音量"},
        {"dialog.render_settings.audio.firework", "Firework 音量"},
        {"dialog.render_settings.audio.break_slide", "Break Slide 音量"},
        {"dialog.render_settings.video.brightness", "背景/PV 亮度"},
        {"dialog.render_settings.video.scale.fill", "填充（必要时裁切）"},
        {"dialog.render_settings.video.scale.fit", "适应（完整显示）"},
        {"dialog.render_settings.video.canvas_aspect.square", "1:1（正方形）"},
        {"dialog.render_settings.video.canvas_aspect.4_3", "4:3"},
        {"dialog.render_settings.video.canvas_aspect.16_9", "16:9"},
        {"dialog.render_settings.video.auto_restore_square", "导出后自动恢复 1:1"},
        {"dialog.render_settings.video.smooth_brightness", "平滑亮度"},
        {"dialog.render_settings.video.brightness_outer", "亮度（外侧）"},
        {"dialog.render_settings.video.brightness_inner", "亮度（内侧）"},
        {"dialog.render_settings.video.layout_square_scale", "判定线大小"},
        {"dialog.render_settings.video.flow_speed", "流速"},
        {"dialog.render_settings.video.scale_mode", "背景 / PV 缩放模式"},
        {"dialog.render_settings.video.canvas_aspect", "预览画布比例"},
        {"dialog.render_settings.preview.debug", "显示预览调试信息"},
        {"dialog.render_settings.preview.canvas_frame_rate", "预览刷新率"},
        {"dialog.render_settings.preview.canvas_frame_rate.60", "60 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.120", "120 FPS"},
        {"dialog.render_settings.preview.canvas_frame_rate.display", "屏幕最大刷新率"},
        {"dialog.render_settings.preview.follow_mode", "跟随模式"},
        {"dialog.render_settings.preview.follow_mode.every_comma", "全部逗号"},
        {"dialog.render_settings.preview.follow_mode.nonempty_comma", "仅非空逗号"},
        {"dialog.render_settings.preview.follow_mode.line_only", "仅按行"},
        {"dialog.render_settings.preview.show_object_stats", "预览/导出显示物件统计"},
        {"dialog.render_settings.preview.show_validation_summary", "显示头部错误/警告摘要"},
        {"dialog.render_settings.preview.show_object_stats_preview", "预览显示物件统计"},
        {"dialog.render_settings.preview.show_object_stats_export", "导出显示物件统计"},

        {"dialog.video_export.title", "导出视频"},
        {"dialog.video_export.output", "输出"},
        {"dialog.video_export.browse", "浏览..."},
        {"dialog.video_export.resolution", "分辨率"},
        {"dialog.video_export.fps", "帧率"},
        {"dialog.video_export.performance", "性能"},
        {"dialog.video_export.performance.balanced", "平衡"},
        {"dialog.video_export.performance.speed", "速度优先"},
        {"dialog.video_export.section.options", "选项"},
        {"dialog.video_export.section.range", "导出区间"},
        {"dialog.video_export.range.start", "开始"},
        {"dialog.video_export.range.end", "结束"},
        {"dialog.video_export.range.set_left", "← 设定"},
        {"dialog.video_export.range.set_end", "设定结束"},
        {"dialog.video_export.preview.stop", "停止"},
        {"dialog.video_export.preview.play", "播放"},
        {"dialog.video_export.preview.pause", "暂停"},
        {"dialog.video_export.button.export", "导出"},
        {"dialog.video_export.button.cancel", "取消"},
        {"dialog.video_export.option.show_timestamp", "显示左下角时间戳"},
        {"dialog.video_export.option.smooth_brightness", "平滑亮度"},
        {"dialog.video_export.option.brightness_outer", "亮度（外侧）"},
        {"dialog.video_export.option.brightness_inner", "亮度（内侧）"},
        {"dialog.video_export.option.layout_size", "判定线大小"},
        {"dialog.video_export.option.flow_speed", "流速"},
        {"dialog.video_export.option.scale_mode", "背景 / PV 缩放模式"},
        {"dialog.video_export.option.scale.fill", "填充（必要时裁切）"},
        {"dialog.video_export.option.scale.fit", "适应（完整显示）"},
        {"dialog.video_export.error.preview_unavailable", "预览画布未初始化。"},
        {"dialog.video_export.progress.preparing", "正在准备导出..."},
        {"dialog.video_export.status.canceled", "导出已取消。"},
        {"dialog.video_export.status.completed", "导出完成。"},
        {"dialog.video_export.message.canceled", "导出已取消。"},
        {"dialog.video_export.message.completed", "导出完成。"},
        {"dialog.video_export.error.failed", "导出失败。"},
        {"dialog.video_export.error.worker_crash", "导出子进程已崩溃。"},
        {"dialog.video_export.error.worker_exit", "导出子进程异常退出。"},
        {"dialog.video_export.error.failed_title", "导出失败"},

        {"status.audio_restored_default", "已恢复默认音量设置"},
        {"status.touch_trail_enabled", "Touch 轨迹已开启"},
        {"status.touch_trail_disabled", "Touch 轨迹已关闭"},
        {"status.judge_marker_enabled", "判定标记已开启"},
        {"status.judge_marker_disabled", "判定标记已隐藏"},
        {"status.editor_text_display_updated", "文本框显示已更新。"},
        {"status.preferences_updated", "首选项已更新。"},
        {"status.preferences_saved", "首选项已保存，重启后生效。"},
        {"status.syntax.select_difficulty", "请先选择一个难度文本。"},
        {"status.syntax.passed", "语法检查通过。"},
        {"dialog.syntax_ok.message", "未发现语法错误/警告。"},
        {"status.syntax.failed_counts", "语法检查未通过：%1 个错误，%2 个警告。"},
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

QString preferencesFilePath()
{
    return preferencesPath();
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
    if (!isChineseUi()) {
        return QString();
    }
    const auto it = zhMap().constFind(key);
    if (it == zhMap().constEnd()) {
        return QString();
    }
    return it.value();
}

}  // namespace UiText
