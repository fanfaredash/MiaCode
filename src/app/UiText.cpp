#include "UiText.h"

#include <QHash>
#include <QLocale>

namespace {

bool useChineseUi()
{
    const QByteArray env = qgetenv("MIACODE_LANG").trimmed().toLower();
    if (!env.isEmpty()) {
        return env == "zh" || env == "zh-cn" || env == "zh_cn" || env == "cn";
    }
    const QString localeName = QLocale::system().name().toLower();
    return localeName.startsWith("zh");
}

const QHash<QString, QString>& zhMap()
{
    static const QHash<QString, QString> map{
        {"menu.file", "文件(&F)"},
        {"menu.tools", "工具(&T)"},
        {"menu.transform", "变换(&R)"},
        {"menu.help", "帮助(&H)"},

        {"action.new", "新建"},
        {"action.open", "打开"},
        {"action.save", "保存"},
        {"action.save_as", "另存为"},
        {"action.about", "关于"},
        {"action.validate", "校验 Simai"},
        {"action.stop_preview", "停止预览"},
        {"action.pause_preview", "播放/暂停预览"},
        {"action.render_settings", "渲染设置..."},
        {"action.transform.mirror_lr", "左右镜像"},
        {"action.transform.mirror_ud", "上下镜像"},
        {"action.transform.rotate_180", "旋转 180°"},
        {"action.transform.rotate_ccw_45", "逆时针旋转 45°"},
        {"action.transform.rotate_cw_45", "顺时针旋转 45°"},

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

        {"dialog.render_settings.title", "渲染设置"},
        {"dialog.render_settings.audio_group", "音频"},
        {"dialog.render_settings.video_group", "视频"},
        {"dialog.render_settings.button.close", "关闭"},
        {"dialog.render_settings.button.restore_project_default", "恢复默认"},
        {"dialog.render_settings.audio.bgm", "BGM 音量"},
        {"dialog.render_settings.audio.answer", "Answer 音量"},
        {"dialog.render_settings.audio.slide", "Slide 音量"},
        {"dialog.render_settings.audio.break", "Break 音量"},
        {"dialog.render_settings.audio.ex", "EX 音量"},
        {"dialog.render_settings.audio.touch", "Touch 音量"},
        {"dialog.render_settings.audio.touchhold", "TouchHold 音量"},
        {"dialog.render_settings.video.brightness", "背景/PV 亮度"},
        {"dialog.render_settings.video.debug", "显示预览调试信息"},

        {"status.audio_restored_default", "已恢复默认音量设置"},
        {"status.touch_trail_enabled", "Touch 轨迹已开启"},
        {"status.touch_trail_disabled", "Touch 轨迹已关闭"},
        {"status.judge_marker_enabled", "判定标记已开启"},
        {"status.judge_marker_disabled", "判定标记已隐藏"},
    };
    return map;
}

}  // namespace

namespace UiText {

bool isChineseUi()
{
    static const bool isZh = useChineseUi();
    return isZh;
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
