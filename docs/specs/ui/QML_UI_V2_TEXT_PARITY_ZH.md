# QML v2 中文文案 vs v1 全量比对表

本文档记录一次针对 `src/app/ui/UiText.cpp::qmlOnlyEntries()`（QML v2 重构中新增、v1 主表
`zhMap()` 没有对应词条的 185 条字符串）与 v1 中文原文的全量比对结果。中文会话下
`UiText::textForQmlSource` 会原样返回 QML 源串，所以 v2 重构引入的中文措辞偏差从未被
自动改回——这张表就是找出这些偏差、判断哪些值得改、哪些不该动的记录。

## 已处理（本轮，7 条）

以下 7 条已确认为真实偏差并改回 v1 原文（QML 源串改回 v1 逐字文案，`qmlOnlyEntries()`
里对应的兜底条目已删除，改由 `qmlSourceReverseIndex()` 反查 v1 主表命中）：

- **丢失"自动备份原文件"承诺（4 条，严重度：高/中）**：
  `media_tools.insert_silence_at_the_start`、`media_tools.insert_a_black_screen_at`、
  `media_tools.convert_track_mp3_to_44100_2`、`media_tools.compress_the_background_video_under`。
  这四个媒体工具入口卡片的描述文案在 v2 重构中丢了"，并自动备份原文件。"半句。
  实测（`src/app/runtime/media/MediaTools.cpp`）确认备份行为本身没有被移除，只是文案
  不再告知用户——前两个（插入静音/黑幕）用户全程看不到任何提及备份的文案，后两个
  （转换采样率/压缩视频）点击后的确认框仍会提，只是入口卡片这一步先漏了。
- **纯空格差异（3 条，严重度：低）**：`dialog.render_settings.video.tap_flow_speed`、
  `dialog.render_settings.video.touch_flow_speed`、`video_export.layout_size`。
  v2 在英文缩写和中文字之间多加了一个空格（`Tap 流速` vs v1 `Tap流速`），已改回无空格。

## 本轮不动、原因记录（其余 178 条）

比对方法本身是自动相似度打分（见下方原始说明），准确率不足以直接采信，所以除上面 7 条
外，本轮全部不动，原因分几类：

1. **"语义变了"里剩下的 1 条（`media_tools.batch_pv_description`）**：v1 多了"及其直接
   子文件夹""仅认 bg.mp4 或 pv.mp4"两个限定，v2 入口卡片简化成了"扫描一个目录，批量压缩
   其中的背景视频。"。已核实点开后的 `PvBatchCompressionDialog.qml`
   （`src/app/qml_ui/media/PvBatchCompressionDialog.qml`）**没有**补充说明这两个限定的
   文案（该对话框只有目录选择器、队列列表和进度摘要，没有规则说明文本）——但这属于
   "入口卡片文案是否应该带业务规则细节"的产品判断，不属于本轮"丢了本该在的一句话"这类
   可以照抄修复的问题，因此保留调查结果，不修改。
2. **"判断边界"（2 条）**：`dialog.unsaved_field_changes.message` 的问句在 v2 里被显式的
   保存/放弃/取消三个按钮取代，可能是更准确的表达而非语义丢失；
   `preview.fullscreen.exit_tooltip` 丢了"（Esc）"提示但快捷键本身仍生效。两条都需要
   人工产品判断，不是机械可改的偏差。
3. **符号/图标现代化陷阱（`开始试听`▶、`添加`▾ 等）**：v1 硬编码了装饰符号，QML v2
   大概率已经改用真实图标控件表达同样的含义，照抄符号回去是倒退，不改。
4. **占位符结构差异**（`静音` vs v1 `静音 %1`；`关闭 %1` vs v1 `关闭` 等）：动这些可能
   打断 QML 侧的 `.arg()` 调用链，风险归属于代码改动而非纯文案，不改。
5. **同形词一对多**（`亮度`/`时间轴`/`关闭`/`谱面` 等在 v1 对应不止一个 key）：自动匹配
   无法判定唯一正确的 v1 对应词条，人工也没有足够上下文一次性裁决，不改。
6. **主题遮罩标签**（工具栏/状态栏/面板/编辑器标题/输入控件/代码编辑器 各深浅色，共 10
   条）：v1 对应的是"深色"/"浅色"这类可复用的模板片段做字符串拼接，不是这些标签的
   1:1 直接映射，不改。
7. **约 65 条"中"置信度和全部"低（弱相关候选，未采信）"/"低"条目**：这些是自动相似度
   打分结果，比对脚本自身说明里已经写明"短泛用词在 1114 条里到处出现，自动挑的
   top-1 经常挑到语义不对但字面分高的 key""未逐条验证全部 185 条，尤其中置信度条目
   请勿直接采信"。没有人工逐条复核前不能当作真实偏差处理，本轮不采信、不改。

---

## 原始比对表（185 条，未改动）

来源：`src/app/ui/UiText.cpp` 的 `qmlOnlyEntries()`（4282-4467 行，本文档写作时的行号；
上面 7 条已处理的条目在当前代码里已被删除）逐条与主表 `zhMap()`（v1，1617-2799 行，1114
个 key，24 个重复 key 取最后一次赋值）做中英文双语相似度自动匹配；对高置信度桶（约 30
条）和全部"语义变了"候选做了人工抽查、改正。

方法与局限：SequenceMatcher 对去空白中文 + 归一化英文分别打分，combined = 0.5*zh_ratio +
0.5*en_ratio + 包含关系加分，取分数最高的候选。已知问题：短泛用词（关闭/亮度/时间轴/深色/
浅色……）在 1114 条里到处出现，自动挑的 top-1 经常挑到语义不对但字面分高的 key；已人工
修正找到的错配，但未逐条验证全部 185 条，尤其 0.75-0.85 分段的"中"置信度条目请勿直接
采信。

| QML 源串 | 疑似 v1 key | v1 中文原文 | 差异类型 | 置信度 | 备注 |
|---|---|---|---|---|---|
| 在 track.mp3 开头插入一段静音。 | `media_tools.insert_silence_at_the_start` | 在 track.mp3 开头插入一段静音，并自动备份原文件。 | 语义变了 | 高 | **已处理**：launcher description drops the backup clause; no other UI step mentions backup before this action runs (PrependBlankDialog confirm text also has no backup mention) |
| 在背景视频开头插入一段黑屏。 | `media_tools.insert_a_black_screen_at` | 在背景视频开头插入一段黑幕，并自动备份原文件。 | 语义变了 | 高 | **已处理**：same pattern as track.mp3 silence; also 黑屏/黑幕 word swap |
| 把 track.mp3 转换为 44100 Hz。 | `media_tools.convert_track_mp3_to_44100_2` | 将 track.mp3 转换为 44100Hz，并自动备份原文件。 | 语义变了 | 高 | **已处理**：launcher card drops backup clause, but requestConfirmation shown before running (media_tools.convert_track_mp3_to_44100) still mentions backup — partial mitigation, first-seen text is still wrong |
| 把背景视频压缩到 20 MiB 以内。 | `media_tools.compress_the_background_video_under` | 将背景视频压缩到 20M 以内，并自动备份原文件。 | 语义变了 | 高 | **已处理**：same mitigation pattern as convert-44100 (requestConfirmation media_tools.compress_1_under_20_mib still mentions backup) |
| 扫描一个目录，批量压缩其中的背景视频。 | `media_tools.batch_pv_description` | 扫描所选文件夹及其直接子文件夹中的 bg.mp4 或 pv.mp4，并将视频压缩到 20 MiB 以内。 | 语义变了 | 中 | 未改：drops "含直接子文件夹" + "仅 bg.mp4/pv.mp4 命名" 的限定；已核实 `PvBatchCompressionDialog.qml` 点开后也没有补充说明这两个限定 |
| 「%1」有未保存的更改。 | `dialog.unsaved_field_changes.message` | %1 有未保存的更改。切换前是否保存？ | 判断边界 | 中 | v1 问句"切换前是否保存？"在 v2 用 ChoiceDialog 的 保存/放弃/取消 三个显式按钮代替，问句变得多余——像是 v2 更准确而非语义丢失，但需人工确认 |
| 退出全屏预览 | `preview.fullscreen.exit_tooltip` | 退出全屏预览（Esc） | 判断边界 | 中 | v1 tooltip 里带"（Esc）"快捷键提示，v2 tooltip 文案本身丢了这个提示；功能不受影响（Esc 仍然生效），只是提示文案少了这半句，严重度远低于备份那类 |
| 启用 clock_count | `video_export.enable_clock_count_1` | 启用 clock_count (%1) | 拿不准 | 中 | v1 标签带动态 (%1) 占位符；v2 复选框文案是静态的，没有 .arg() 调用，看不出 %1 原来显示什么、是否真的丢信息 |
| 切换时间轴 | `tab.timeline (弱)` | 时间轴 | 拿不准 | 低 | v1 "时间轴" is a tab/section label, not a toggle-visibility action; QML label is for a panel-toggle button — unclear 1:1 correspondence, v1 may not have had this as a discrete toggle |
| 播放速度 | `action.preview_speed_down/up (弱)` | 播放速度 ↓ / 播放速度 ↑ | 拿不准 | 低 | v1 had two separate inc/dec button labels (each with a hardcoded arrow glyph); QML tooltip is for what looks like a single unified speed control — possible UI restructuring, not a simple wording change |
| 时间轴缩放 | `tab.timeline (弱)` | 时间轴 | 拿不准 | 低 | same uncertainty as 切换时间轴 — likely a new granular zoom-preset control, not a reused v1 label |
| Layout 整图大小 | `video_export.layout_size` | Layout整图大小 | 空格差异 | 高 | **已处理**：score=0.95 |
| Tap 流速 | `dialog.render_settings.video.tap_flow_speed` | Tap流速 | 空格差异 | 高 | **已处理**：score=1.30 |
| Touch 流速 | `dialog.render_settings.video.touch_flow_speed` | Touch流速 | 空格差异 | 高 | **已处理**：score=1.30 |
| HUD 字体区域 | `dialog.video_export.option.hud_font` | HUD 字体 | 用词不同 | 高 | score=1.10 |
| 上一个 | `metadata.find_previous` | 查找上一个 | 用词不同 | 高 | score=1.06 |
| 关闭 %1 | `action.close` | 关闭 | 用词不同 | 高 | score=1.05 |
| 内圈亮度 | `dialog.render_settings.video.brightness_inner` | 亮度（内侧） | 用词不同 | 高 | corrected: original auto-pick landed on generic cover.brightness="亮度"; the real analog is the export-page inner/outer brightness pair |
| 右侧 | `dialog.preferences.background.position.right` | 右 | 用词不同 | 高 | score=1.13 |
| 外圈亮度 | `dialog.render_settings.video.brightness_outer` | 亮度（外侧） | 用词不同 | 高 | corrected, see 内圈亮度 |
| 导入 HUD 字体 | `dialog.video_export.option.hud_font` | HUD 字体 | 用词不同 | 高 | score=1.06 |
| 左侧 | `dialog.preferences.background.position.left` | 左 | 用词不同 | 高 | score=1.13 |
| 开始试听 | `latency.start_audition` | ▶ 开始试听 | 用词不同 | 高 | score=1.24 \| v1 label has a hardcoded "▶" glyph prefix; QML likely renders play icon as a real Icon element instead — dropping the glyph from text is probably correct modernization, not a bug |
| 更多 | `action.transform.more` | 更多... | 用词不同 | 高 | score=1.09 \| v1 suffix is "..." (three ASCII dots) vs QML plain "更多" with no ellipsis — trivial, likely fine either way |
| 标题字体预览 | `card_font.title` | 标题字体 | 用词不同 | 高 | score=1.06 |
| 检测 | `media_tools.detect` | 自动检测 | 用词不同 | 高 | score=1.13 |
| 正文字体预览 | `card_font.body` | 正文字体 | 用词不同 | 高 | score=1.05 |
| 添加 | `cover.add_layer` | 添加 ▾ | 用词不同 | 高 | score=1.20 \| v1 label has a hardcoded "▾" dropdown-caret glyph suffix; same modernization pattern as 开始试听 |
| 片头标题字体 | `card_font.title` | 标题字体 | 用词不同 | 高 | score=1.08 |
| 片头正文字体 | `card_font.body` | 正文字体 | 用词不同 | 高 | score=1.07 |
| 自定义 | `cover.custom_image` | 自定义图片 | 用词不同 | 高 | score=1.01 |
| 视图设置 | `toolbar.settings_placeholder` | 设置 | 用词不同 | 高 | score=1.01 |
| 解码器 | `latency.audio_decoder` | 音频解码器 | 用词不同 | 高 | score=1.02 |
| 输出文件夹 | `dialog.batch_export.output_dir` | 导出文件夹 | 用词不同 | 高 | score=1.05 |
| 还原 HUD 字体 | `dialog.video_export.option.hud_font` | HUD 字体 | 用词不同 | 高 | score=1.08 |
| 采样率 | `media_tools.sample_rate` | 采样率转换 | 用词不同 | 高 | score=1.18 |
| 难度卡字体 | `cover.difficulty_card` | 难度卡 | 用词不同 | 高 | score=1.09 |
| 静音 | `dialog.render_settings.audio.button.mute` | 静音 %1 | 用词不同 | 高 | score=1.03 |
|  · 谱师：%1 | `net.designer` | 谱师 | 用词不同 | 中 | score=0.88 |
| %1 个错误，%2 个警告 | `editor.validation_summary.tooltip_with_muri` | %1 个错误，%2 个警告，%3 条无理 | 用词不同 | 中 | score=0.89 |
| %1% | `dialog.batch_export.progress.current_item` | %1\n%2 | 用词不同 | 中 | score=0.98 |
| BPM | `menu.bpm_latency` | BPM && 延迟检测 | 用词不同 | 中 | score=0.76 |
| PV 帧率 | `cover.frame` | 帧 | 用词不同 | 中 | score=0.78 |
| 下一个 | `metadata.find_next` | 查找下一个 | 用词不同 | 中 | score=0.98 |
| 书签 | `editor.new_bookmark` | 新书签 | 用词不同 | 中 | score=0.93 |
| 关于 MiaCode | `action.about` | 关于 | 用词不同 | 中 | score=0.76 |
| 关闭 (Ctrl+W) | `action.close` | 关闭 | 用词不同 | 中 | score=0.76 |
| 关闭「%1」 | `action.close` | 关闭 | 用词不同 | 中 | score=0.97 |
| 关闭文档 | `action.close` | 关闭 | 用词不同 | 中 | score=0.90 |
| 其他字段 | `metadata.other_fields` | 其他 &xx 字段 | 用词不同 | 中 | score=0.81 |
| 初始偏移 | `metadata.field.first` | 偏移 | 用词不同 | 中 | score=0.93 |
| 删除当前难度 | `dialog.batch_export.difficulty` | 难度 | 用词不同 | 中 | score=0.84 |
| 导入片头难度卡字体 | `cover.difficulty_card` | 难度卡 | 用词不同 | 中 | score=0.86 |
| 展开难度 | `dialog.batch_export.difficulty` | 难度 | 用词不同 | 中 | score=0.79 |
| 工具 | `menu.tools` | 工具(&T) | 用词不同 | 中 | score=0.97 |
| 延迟校准 | `metadata.latency_card.title` | 延迟与偏移校准 | 用词不同 | 中 | score=0.79 |
| 开始压缩 | `dialog.video_export.range.start` | 开始 | 用词不同 | 中 | score=0.86 |
| 恢复备份 | `media_tools.restore_backup` | 还原备份 | 用词不同 | 中 | score=0.90 |
| 恢复备份 (%1) | `media_tools.restore_backup` | 还原备份 | 用词不同 | 中 | score=0.78 |
| 恢复默认 | `dialog.preferences.background.overlay.reset_defaults` | 还原到默认 | 用词不同 | 中 | score=0.87 |
| 所有文件 (*) | `net.upload_assets` | 文件 | 用词不同 | 中 | score=0.88 |
| 所有文件 (*.*) | `track_metadata.mp3_audio_mp3_all_files` | MP3 音频 (*.mp3);;所有文件 (*.*) | 用词不同 | 中 | score=0.86 |
| 打开播放速度预设 | `action.preview_speed_down` | 播放速度 ↓ | 用词不同 | 中 | score=0.80 |
| 打开时间轴缩放预设 | `tab.timeline` | 时间轴 | 用词不同 | 中 | score=0.79 |
| 批量压缩 PV | `media_tools.batch_pv_title` | 批量压缩视频 | 用词不同 | 中 | score=0.77 |
| 折叠难度 | `dialog.batch_export.difficulty` | 难度 | 用词不同 | 中 | score=0.77 |
| 撤销 | `action.undo` | 撤回 | 用词不同 | 中 | score=0.90 |
| 整理语法 | `window.syntax` | 语法 | 用词不同 | 中 | score=0.95 |
| 无理检测 | `validation.muri.alert.muri` | 无理 | 用词不同 | 中 | score=0.87 |
| 时间轴亮度 | `tab.timeline` | 时间轴 | 用词不同 | 中 | score=0.97 |
| 时间轴帧率 | `tab.timeline` | 时间轴 | 用词不同 | 中 | score=0.97 |
| 显示谱面信息 | `dialog.video_export.option.show_chart_info` | 显示左上角谱面信息 | 用词不同 | 中 | score=0.95 |
| 查找与替换 | `metadata.replace` | 替换 | 用词不同 | 中 | score=0.89 |
| 正在取消… | `action.cancel` | 取消 | 用词不同 | 中 | score=0.96 |
| 移除 | `dialog.batch_export.remove_selected` | 移除选中 | 用词不同 | 中 | score=0.92 |
| 第 %1 行：%2 | `editor.l_1` | 第 %1 行 | 用词不同 | 中 | score=0.79 |
| 等级文本渲染 | `cover.render_level_as_text` | 等级包含文字/字母 | 用词不同 | 中 | score=0.85 |
| 细分 | `latency.subdivision` | 分音: | 用词不同 | 中 | score=0.85 |
| 统一谱师 | `net.designer` | 谱师 | 用词不同 | 中 | score=0.92 |
| 背景缩放 | `dialog.preferences.background_group` | 背景 | 用词不同 | 中 | score=0.99 |
| 启用应用背景 | `dialog.preferences.background_group` | 背景 | 用词不同 | 中 | score=0.81 |
| 未选择背景图片 | `cover.choose_background_image` | 选择背景图片 | 用词不同 | 中 | score=0.96 |
| 背景覆盖层 | `dialog.preferences.background_group` | 背景 | 用词不同 | 中 | score=0.93 |
| 缩放模式 | `dialog.preferences.background.scale` | 缩放 | 用词不同 | 中 | score=0.97 |
| 工具栏（深色） | `dialog.preferences.theme.dark` | 深色 | 用词不同 | 中 | score=0.77 |
| 工具栏（浅色） | `dialog.preferences.theme.light` | 浅色 | 用词不同 | 中 | score=0.80 |
| 状态栏（深色） | `dialog.preferences.extensions.status` | 状态 | 用词不同 | 中 | score=0.81 |
| 状态栏（浅色） | `dialog.preferences.extensions.status` | 状态 | 用词不同 | 中 | score=0.80 |
| 面板（深色） | `dialog.preferences.theme.dark` | 深色 | 用词不同 | 中 | score=0.84 |
| 面板（浅色） | `dialog.preferences.theme.light` | 浅色 | 用词不同 | 中 | score=0.86 |
| 编辑器标题（深色） | `dialog.welcome.preview.editor` | 编辑器 | 用词不同 | 中 | score=0.80 |
| 编辑器标题（浅色） | `dialog.welcome.preview.editor` | 编辑器 | 用词不同 | 中 | score=0.79 |
| 代码编辑器（深色） | `dialog.preferences.background.overlay.code_editor` | 代码编辑区 | 用词不同 | 中 | score=0.84 |
| 代码编辑器（浅色） | `dialog.preferences.background.overlay.code_editor` | 代码编辑区 | 用词不同 | 中 | score=0.83 |
| 自动 | `latency.auto_detect` | 自动检测 | 用词不同 | 中 | score=0.78 |
| 视频解码 | `dialog.render_settings.video_group` | 视频 | 用词不同 | 中 | score=0.90 |
| 视频路径 | `dialog.render_settings.video_group` | 视频 | 用词不同 | 中 | score=0.97 |
| 选择当前行 | `net.select` | 选择 | 用词不同 | 中 | score=0.83 |
| 选择统一谱师 | `net.designer` | 谱师 | 用词不同 | 中 | score=0.77 |
| 选择要扫描的目录 | `media_tools.batch_pv_choose_folder` | 选择要扫描视频的文件夹 | 用词不同 | 中 | score=0.87 |
| 重置片头难度卡字体 | `cover.difficulty_card` | 难度卡 | 用词不同 | 中 | score=0.86 |
| 错误 | `tab.validation_errors` | 校验错误 | 用词不同 | 中 | score=0.89 |
| 音视频处理 | `media_tools.audio_video_processing` | 音频/视频处理 | 用词不同 | 中 | score=0.88 |
| %1，第 %2 行 | `document.1_line_2_double_click` | %1\n第 %2 行 · 双击重命名 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.67: document.1_line_2_double_click |
| Muri | `validation.muri.alert.muri` | 无理 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.65: validation.muri.alert.muri |
| PV 前置黑屏 | `media_tools.black_screen` | 黑幕 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.59: media_tools.black_screen |
| Simai 文件 (*.txt *.simai) | `net.upload_assets` | 文件 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.58: net.upload_assets |
| 书签 %1：%2 | `dialog.batch_export.progress.current_item` | %1\n%2 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.71: dialog.batch_export.progress.current_item |
| 书签名称 | `editor.new_bookmark` | 新书签 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.61: editor.new_bookmark |
| 从左侧打开元数据或难度 | `dialog.batch_export.difficulty` | 难度 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.63: dialog.batch_export.difficulty |
| 保存 simai 文件 | `action.save` | 保存 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.69: action.save |
| 修正 HUD 文本布局 | `cover.layout` | 布局 ▾ | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.56: cover.layout |
| 偏好设置 | `action.preferences` | 首选项... | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.65: action.preferences |
| 元数据 | `editor.metadata` | 谱面信息设置 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.65: editor.metadata |
| 全部恢复默认 | `dialog.preferences.background.overlay.reset_defaults` | 还原到默认 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.63: dialog.preferences.background.overlay.reset_defaults |
| 切换侧栏 | `metadata.show_in_sidebar` | 在侧边栏显示 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.51: metadata.show_in_sidebar |
| 创建书签 | `editor.new_bookmark` | 新书签 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.66: editor.new_bookmark |
| 区分大小写 | `cover.size` | 大小 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.58: cover.size |
| 半角输入转换 | `dialog.preferences.editor_half_width_input` | 锁定半角符号输入 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.63: dialog.preferences.editor_half_width_input |
| 取消静音 | `action.cancel` | 取消 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.65: action.cancel |
| 将规范化整份谱面正文。 | `dialog.normalize.failed` | 无法整理当前谱面。 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.53: dialog.normalize.failed |
| 展开书签 | `editor.new_bookmark` | 新书签 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.64: editor.new_bookmark |
| 常规渲染 | `dialog.preferences.performance.video_decode` | PV渲染 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.53: dialog.preferences.performance.video_decode |
| 当前难度及其正文将从文档中删除。 | `dialog.batch_export.difficulty` | 难度 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.54: dialog.batch_export.difficulty |
| 恢复本地默认 | `dialog.preferences.background.overlay.reset_defaults` | 还原到默认 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.60: dialog.preferences.background.overlay.reset_defaults |
| 打开 simai 文件 | `action.open` | 打开 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.69: action.open |
| 打开波形和小节线亮度设置 | `cover.brightness` | 亮度 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.62: cover.brightness |
| 打开预览渲染模式菜单 | `preview.canvas.menu_description` | 打开预览画布菜单 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.67: preview.canvas.menu_description |
| 折叠书签 | `editor.new_bookmark` | 新书签 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.62: editor.new_bookmark |
| 按下新的快捷键，Esc 取消。 | `action.cancel` | 取消 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.58: action.cancel |
| 整谱规范化 | `dialog.normalize.title` | 整理谱面 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.53: dialog.normalize.title |
| 无理判定半径 | `validation.muri.alert.muri` | 无理 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.71: validation.muri.alert.muri |
| 显示所有已打开的编辑器 | `dialog.welcome.preview.editor` | 编辑器 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.74: dialog.welcome.preview.editor |
| 暂无备份 | `action.restore_backup.empty` | 没有可用备份 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.68: action.restore_backup.empty |
| 暂无最近文档 | `cover.no_recent_files` | （无最近文件） | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.66: cover.no_recent_files |
| 检查谱面 | `menu.format_chart` | 谱面整理 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.56: menu.format_chart |
| 正在分析… | `net.canceling` | 正在取消... | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.50: net.canceling |
| 点击一行以录制新的快捷键。 | `dialog.preferences.shortcuts_group` | 快捷键 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.52: dialog.preferences.shortcuts_group |
| 画布帧率 | `cover.frame` | 帧 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.73: cover.frame |
| 硬件解码 | `dialog.preferences.performance.video_decode.hardware` | 硬件渲染 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.72: dialog.preferences.performance.video_decode.hardware |
| 禁用输入法 | `dialog.welcome.chinese_input.disable` | 禁止输入法 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.70: dialog.welcome.chinese_input.disable |
| 输入控件（深色） | `dialog.preferences.theme.dark` | 深色 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.67: dialog.preferences.theme.dark |
| 输入控件（浅色） | `dialog.preferences.theme.light` | 浅色 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.70: dialog.preferences.theme.light |
| 艺术家 | `metadata.field.artist` | 曲师 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.65: metadata.field.artist |
| 行 %1，列 %2 | `metadata.ln_1_col_1` | 1行 1列 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.63: metadata.ln_1_col_1 |
| 计数拍 | `media_tools.beats` | 拍数 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.61: media_tools.beats |
| 设为本地默认 | `cover.reset_to_default` | 重置为默认布局… | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.57: cover.reset_to_default |
| 语言与主题的更改将在重启后生效。 | `dialog.preferences.language` | 语言 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.54: dialog.preferences.language |
| 跟随当前谱面代码位置 | `dialog.preferences.background.position` | 位置 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.65: dialog.preferences.background.position |
| 跳转到此行 | `validation.jump_to_source` | 跳转到源 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.66: validation.jump_to_source |
| 软件解码 | `dialog.preferences.performance.video_decode.software` | 软件渲染 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.72: dialog.preferences.performance.video_decode.software |
| 静音 Break 星星尾判音 | `dialog.render_settings.audio.break` | Break 音量 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.52: dialog.render_settings.audio.break |
| 音效音量 | `dialog.render_settings.music.intro_sound_volume` | 片头音量 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.57: dialog.render_settings.music.intro_sound_volume |
| 音轨前置静音 | `media_tools.prepend_track_silence` | 音频开头静音处理 | 纯新增 | 低（弱相关候选，未采信） | best candidate score only 0.51: media_tools.prepend_track_silence |
| %1 尚未更新到 QML 界面。 | `net.upload_failed_count_1` | %1 个谱面上传失败。 | 纯新增 | 低 | score=0.38, no plausible v1 counterpart found |
| %1x | `editor.l_1` | 第 %1 行 | 纯新增 | 低 | score=0.49, no plausible v1 counterpart found |
| A / B / C / D / E 各区半径不同，暂不可调。 | `export_page.the_video_export_panel_is` | 视频导出面板暂不可用。 | 纯新增 | 低 | score=0.30, no plausible v1 counterpart found |
| Enter 跳转；Ctrl+Shift+B 创建；Delete 删除；F2 重命名；右键打开书签菜单 | `metadata.rename` | 重命名 | 纯新增 | 低 | score=0.42, no plausible v1 counterpart found |
| 书签与行号：第 %1 行无书签 | `editor.l_1` | 第 %1 行 | 纯新增 | 低 | score=0.44, no plausible v1 counterpart found |
| 书签与行号：第 %1 行有书签 | `editor.l_1` | 第 %1 行 | 纯新增 | 低 | score=0.44, no plausible v1 counterpart found |
| 保存只写入这个难度，其他难度在文件里保持原样；放弃把这个难度还原到上次保存时的内容。 | `dialog.batch_export.difficulty` | 难度 | 纯新增 | 低 | score=0.42, no plausible v1 counterpart found |
| 入口会保留，功能完成后将在此处提供。 | `cover.render_and_save_the_cover` | 渲染并保存封面图片 | 纯新增 | 低 | score=0.25, no plausible v1 counterpart found |
| 全词 | `net.select_all` | 全选 | 纯新增 | 低 | score=0.39, no plausible v1 counterpart found |
| 切换侧栏 (Ctrl+B) | `cover.zoom_canvas_in_ctrl` | 放大画布视图（Ctrl++） | 纯新增 | 低 | score=0.42, no plausible v1 counterpart found |
| 切换底部面板 | `dialog.preferences.extensions.devtools` | DevTools 面板 | 纯新增 | 低 | score=0.40, no plausible v1 counterpart found |
| 在 %1 开头插入 %2 拍（BPM %3）静音，约 %4 秒。 | `media_tools.prepended_2_s_of_3` | 已为 %1 开头添加 %2 秒%3（原文件已备份为 %4）。 | 纯新增 | 低 | score=0.40, no plausible v1 counterpart found |
| 在 %1 开头插入 %2 拍（BPM %3）黑屏，约 %4 秒。 | `media_tools.prepended_2_s_of_3` | 已为 %1 开头添加 %2 秒%3（原文件已备份为 %4）。 | 纯新增 | 低 | score=0.39, no plausible v1 counterpart found |
| 字段源码 | `metadata.other_fields` | 其他 &xx 字段 | 纯新增 | 低 | score=0.40, no plausible v1 counterpart found |
| 对齐到 384 分网格 | `document.snap_approximately_to_384_grid` | 约分至384分音 | 纯新增 | 低 | score=0.42, no plausible v1 counterpart found |
| 将规范化选中的第 %1 - %2 行。 | `dialog.batch_export.progress.current_item` | %1\n%2 | 纯新增 | 低 | score=0.46, no plausible v1 counterpart found |
| 平滑星星消去动画 | `dialog.render_settings.video.smooth_brightness` | 平滑亮度 | 纯新增 | 低 | score=0.35, no plausible v1 counterpart found |
| 当前存在多个谱师名义，请选择要统一使用的值。 | `net.designer` | 谱师 | 纯新增 | 低 | score=0.49, no plausible v1 counterpart found |
| 录制中… | `dialog.render_settings.gameplay.tap_judge_text_distance.middle` | 中 | 纯新增 | 低 | score=0.42, no plausible v1 counterpart found |
| 总时长 %1 s | `net.retrying_1` | 重试：%1 | 纯新增 | 低 | score=0.40, no plausible v1 counterpart found |
| 暂未更新支持 | `export_page.no_difficulty_is_available_to` | 暂无可导出的难度。 | 纯新增 | 低 | score=0.33, no plausible v1 counterpart found |
| 最大化 | `cover.size` | 大小 | 纯新增 | 低 | score=0.45, no plausible v1 counterpart found |
| 最小化 | `cover.size` | 大小 | 纯新增 | 低 | score=0.45, no plausible v1 counterpart found |
| 未发现 Muri 问题 | `validation.no_muri_issues_detected` | 未检测到无理。 | 纯新增 | 低 | score=0.43, no plausible v1 counterpart found |
| 未发现验证问题 | `tab.validation_errors` | 校验错误 | 纯新增 | 低 | score=0.37, no plausible v1 counterpart found |
| 每行一个 &字段=值 | `metadata.other_fields` | 其他 &xx 字段 | 纯新增 | 低 | score=0.39, no plausible v1 counterpart found |
| 界面 | `dialog.preferences.restart_message` | 语言设置已保存。请重启 MiaCode 以应用菜单、字体和界面文本。 | 纯新增 | 低 | score=0.45, no plausible v1 counterpart found |
| 表单 | `metadata.information` | 基础信息 | 纯新增 | 低 | score=0.42, no plausible v1 counterpart found |
| 警报 | `validation.muri.alert.warning` | 警告 | 纯新增 | 低 | score=0.40, no plausible v1 counterpart found |
| 谱面 | `(无)` | (无对应词条) | 纯新增 | 低 | top auto-pick cover.chart_frame="谱面帧" is a different concept (cover/layout canvas frame); QML usage is a sidebar tab title/tooltip with no v1 zhMap equivalent found |
| 重新检查 | `card_font.reset` | 重置 | 纯新增 | 低 | score=0.42, no plausible v1 counterpart found |
