# i18n 收敛第二阶段方案：统一到键值路径 + Muri 详情多语言（2026-07-07）

> 本文档只是**方案 + 索引**，不含代码改动。承接
> `docs/audit/I18N_AND_UI_COMPONENT_AUDIT_ZH.md`（第一阶段：已把 7 套散落机制收敛到
> `UiText::localized(en, zh, ja={})` + zh 为键的日语词典，commit `82038a98`）。
>
> 用户拍板两点：
> 1. **取词只保留一条路径 `UiText::text(key)`**（去掉内联 `UiText::localized`）。
> 2. **Muri 详情文本也要多语言**，且**不要 if-else 结构**——加语言不该更复杂。
> 3. key 用**点分语义键、按功能分组**（与现有 363 个键一致）。
>
> 附录 A 是**全部内联字符串的索引**（433 条，含建议 key + en/zh/ja + 出处），方便后续
> 逐表迁移和查找。生成脚本见 §4。

---

## 1. 现状与目标

### 1.1 现状（两条取词路径）

- **键值路径** `UiText::text(key)`：`zhMap`（364 键）+ `jaMap`（412→现已对齐）在
  `UiText.cpp`。**没有 enMap**——英文靠调用点 fallback（`uiText(key, "English")`）兜底。
- **内联路径** `UiText::localized(en, zh, ja={})`：feature 代码里直接写 (en, zh) 对，
  日语从 zh 为键的中央词典 `UiTextJaDictionary.cpp`（359 条）查。

### 1.2 现状的问题（为什么要统一）

1. **英文散落**：英文只存在于调用点 fallback / 内联第一参数，无法集中审计，同一含义可能
   有不同英文。
2. **zh 作词典键会串味**：内联路径的日语以**中文串**为键。当两个不同英文共用同一中文串时，
   日语被合并成一个，语义错乱。实例（附录里可见）：
   - `Close`（关闭查找/对话框）与自动补全的 `Off` 都写成中文「关闭」→ 词典 `关闭→オフ`
     → **日语下 "Close" 被错误显示成「オフ」**。
   - `Size` 有 `大小`/`尺寸` 两种中文，`Brightness` 有 `亮度`/`背景亮度` 两种……key 化后
     各自独立，不再互相覆盖。
3. **两套心智负担**：加一个新串要先决定走 key 还是走内联，评审也要看两处。

### 1.3 目标设计（单一路径 + 三张平行表）

```
UiText::text(key)  // 唯一取词入口
   ├── enMap[key]   ← 新增；英文集中入表，调用点不再传 fallback
   ├── zhMap[key]   ← 简体中文＝基准语言
   └── jaMap[key]   ← 日语；从中文翻译
```

- `text(key)` 按解析语言取对应表；缺失时回退顺序 **resolved → en → key 本身**（key 本身只
  作为「漏翻」的兜底，不应正常触发；spec 保证三表键集一致）。
- **删除** `UiText::localized(...)` 与 `UiTextJaDictionary.cpp`（zh→ja 词典）——日语改为
  `jaMap[key]`，以语义 key 为键，§1.2.2 的串味问题随之消失。
- 所有调用点变成 `UiText::text("feature.xxx")`，**不再带 fallback 参数**（含参数的 `%1`
  仍照常 `.arg()`）。

---

## 2. Part A — 内联串迁移到键值路径

### 2.1 key 命名约定

点分语义键，第一段是**功能域**，与现有键并列。功能域取自所属文件/对话框（见附录分组），
建议前缀集合：

| 前缀 | 覆盖 | 条数 |
|---|---|---|
| `cover.` | `tools/cover_export/*`（封面工作台） | 132 |
| `net.` | `tools/net/*`（批量下载，含日志串） | 66 |
| `media_tools.` | 采样率/压缩/静音/黑幕对话框 | 49 |
| `document.` | 书签/难度/整理等文档流 | 32 |
| `metadata.` | 谱面信息页（FrameBootstrap） | 29 |
| `video_export.` | 视频导出对话框内联（非键值部分） | 21 |
| `latency.` | 延迟检测页 | 18 |
| `track_metadata.` | MP3 元数据读取 | 16 |
| `menu.` / `export_page.` / `preferences.` / `validation.` / `editor.` / `export.` / `window.` / `shell.` / `timeline.` / `ui.` / `dialogs.` | 其余 | 各 1–15 |

> 附录里每条已给出**建议 key**（`slug(英文)` 派生，重名自动加序号）。这些是**起点**，评审时
> 可把机器 slug 改成更贴切的语义名（如 `cover.could_not_read_the_layout` →
> `cover.error.layout_read_failed`）。迁移脚本以「附录表」为准，不再从代码现推。

### 2.2 迁移步骤（可脚本化，建议分功能域小批提交）

1. **建 enMap**：
   - 内联串的英文 = 附录 `en` 列。
   - 现有 363 个 key 的英文 = 从各调用点 fallback 抽取（`uiText(key,"…")`、
     `UiDialogs::text(key,"…")`、`VideoExportDialogInternal.h uiText`）。
   - 三表键集必须一致（spec 守）。
2. **按功能域**，对该域附录条目：把 (key→en)、(key→zh)、(key→ja) 三条分别写进
   `enMap`/`zhMap`/`jaMap`，然后把代码里对应的 `UiText::localized("en","zh")`（及
   `l10n`/`localizedText`/`trText` 转发）替换成 `UiText::text("feature.key")`。
   - `%1` 占位符原样保留，调用处 `.arg()` 不变。
   - 迁移脚本沿用 §4 的定位正则（已在一阶段验证可覆盖全部 4 种写法）。
3. **删除**内联设施：`UiText::localized` 三态实现 + 声明、`UiTextJaDictionary.cpp` 及其
   CMake/词典函数 `japaneseByChineseText()`；`l10n`/`localizedText`/`trText` helper 直接删（调用点已改）。
4. **改 spec**：`ui_text_locale_spec` 从「zh/ja 键集一致 + 内联 zh 有词典条目」改为
   「**en/zh/ja 三表键集完全一致**」+「源码中 `UiText::text("literal")` 的字面量 key 都在表里
   （抓拼错 key）」。不再扫 `localized`。
5. 每个功能域一批：Release 构建 + `ctest` 绿 → 提交。

### 2.3 迁移会**顺带修掉**的串味 bug（附录中同 zh 多 en 的条目）

脚本生成时对「同一 zh、不同 en」自动加了序号 key（如 `cover.brightness` vs
`cover.brightness_2`、`cover.close` vs …）。这些在旧词典里本来会互相覆盖，key 化后各自独立，
日语不再串味。评审时重点看这些 `_2/_3` 后缀项，确认英文语境后给正确日语。

### 2.4 不在本阶段（保持一阶段结论）

- net 的**日志诊断串**（`trText` 里大量 `Queue …`/`Resource result …`）可先只入 en/zh、
  ja 留空回退英文（日志英文可接受）；附录已标出（`ja` 空列 62 条，绝大多数是 net 日志）。
- `UiText` 与偏好持久化（`loadPreferencesObject`）的拆分仍不做。

---

## 3. Part B — Muri 详情文本改结构化（去 if-else）

### 3.1 现状

- **产生端**（语言中立，core）：`src/tools/muri/MuriDiagnosticLabels.cpp` 的
  `slideHeadTapDetailText`/`tapOnSlideDetailText` 等把已算好的字段
  （causeConfig / affectedTarget / gapMs …）**拼成英文散文**塞进 `MuriDiagnostic.detail`。
- **消费端**（UI，3 份拷贝）：`MainWindow.ValidationRuntime.cpp` / `ValidationFlow.cpp` /
  `TimelineQuickStateBridge.cpp` 的 `localizeMuriDetail()`——约 230 行 if-else，**反解析英文
  句子**再用中文重拼。日语无支持（回退英文）。加第三语言＝再抄一份反解析，指数级复杂。

### 3.2 目标：结构化诊断 + 模板渲染

产生端**不再拼英文**，改为在 `MuriDiagnostic` 上挂**结构化字段**：

```cpp
enum class MuriDetailKind {
    None,
    SlideHeadStartEarlyJudgeTap,   // start will/may early-judge a following tap
    SlideHeadJumpStartEarlyJudgeTap,
    SlideHeadStartEarlyJudge,      // start will/may early-judge <target>
    SlideHeadJumpStartEarlyJudge,
    TapOnSlideCollide,             // trajectory will/may collide with <target>
    EarlyJudgedBy,                 // <a> was early-judged by <b>
    ResolvedOutsideWindow,         // <target> resolved outside critical window (+gap)
    MultiTouchFormedBy,
    FormedOverlapSamePosition,
    FormedOverlapAtSamePosition,
    SlideHeadTriggerEarlyJudged,
    PadTriggerEarlyJudged,
    SimpleNoteMissedWindowOverlap,
    SlideRuntimeOutsideWindow, WifiRuntimeOutsideWindow,
    SlideClearedEarly, WifiClearedEarly,
    StaticReference,               // Δ ms
    RuntimeHandMultiTouch,
};
struct MuriDetailArgs {            // 只放已算好的原子字段，不含成句英文
    QString left, right, gapText, deltaText, handCount, actions;
    MuriAlertLevel alert = MuriAlertLevel::Muri;  // will vs may
};
```

- `MuriDiagnostic` 增 `MuriDetailKind detailKind` + `MuriDetailArgs detailArgs`（`detail`
  英文串保留一段过渡期，供 dump/spec，最终可由渲染器统一产出）。
- **单一渲染器** `renderMuriDetail(kind, args, SimaiNativeValidationLocale)`（放
  `common/MuriTypes` 或新 `MuriDetailText`），内部是一张
  **`kind × 语言 → 模板`** 表，用 `args` 填 `%1/%2/%3`。3 处 UI 调用点全部换成它，
  `localizeMuriDetail`/`localizeMuriEntityText` 的 230×3 行删除。
- 模板表（en/zh/ja 各一份，中文＝基准）示例：
  - `SlideHeadStartEarlyJudge` + Muri：`en "%1 start will early-judge %2, gap %3."`
    / `zh "%1 启动，会提前判定 %2，间隔 %3。"` / `ja "%1 が始動し %2 を早入力判定します（間隔 %3）。"`
- 实体子串（`protected …`、note 配置 token 如 `tap 5`/`star 3x`）：token 本身
  （`tap`/`star`/`slide`…）是产品术语保持不变；只有 `protected` 前缀等**词**需要按语言
  映射——同样进模板/小词表，不再 `if chineseUi`。

### 3.3 影响面 & 迁移步骤

1. `MuriDiagnostic`（`MuriAnalyzerModel.h` 或 `common/MuriTypes.h`）加 `detailKind`+`detailArgs`。
2. `MuriDiagnosticLabels.cpp` 的 `*DetailText` 改成填 `detailArgs`+定 `detailKind`（字段本就
   算好了，只是不再拼英文）。其它产生 `.detail=` 的点（`MuriSimpleNoteJudge.cpp`、
   `MuriSlideWifiJudge.cpp`、overlap/static/hand 几处）同样改为设 kind+args。
3. 新增 `renderMuriDetail()` + 三语模板表。
4. 3 处 UI 调用点用渲染器；删 `localizeMuriDetail`/`localizeMuriEntityText`（3 份）及残留
   `const bool chineseUi = UiText::isChineseUi()`（改用 `uiValidationLocale()`）。
5. `MuriDump.cpp` 用英文模板渲染保持输出稳定；`MuriSpec.cpp` 断言从「英文 detail 字符串」改为
   「detailKind + 关键 args」或「英文渲染结果」——**这是唯一需要小心的对照点**，改断言时逐条比对。
6. Release + `muri_spec` + 全量 `ctest` 绿。

> 收益：加任意语言 = 在模板表加一列，产生端和 UI 都不动；英文散文反解析这个脆弱点彻底消失
> （分析器措辞改动不再静默让中文失联）。

---

## 4. 扫描/迁移脚本（现成）

一阶段留下的定位正则可直接复用（覆盖 4 种写法）：

- `UiText::localized(QStringLiteral("EN"), QStringLiteral("ZH")[, QStringLiteral("JA")])`
- `l10n(QStringLiteral("EN"), QStringLiteral("ZH"))`（video_export / cover_export helper）
- `localizedText(QStringLiteral("ZH"), QStringLiteral("EN"))`（成员，export_page / latency）
- `trText("ZH", "EN")`（char*，net）

附录 A 的生成脚本落在维护者本地（`scratchpad/build_inline_index.py`）：扫上述四种 → 去重 →
按文件推功能域 → 从 `UiTextJaDictionary.cpp` 取 ja → `slug(en)` 出建议 key → 按域分组出表。
迁移时以附录表为准（key 已定、ja 已附），脚本按 (en,zh) 精确匹配替换调用点为 `text(key)`。

解析器报错（`SimaiNativeParser.Driver.cpp` 的 zh/ja Exact/Prefix 表）已是「英文原文为键」的
表结构，**不在本次迁移**（它天然是键值式，只是键恰好是英文消息原文）；Muri 结构化完成后可另评
是否统一到同一渲染风格。

---

## 5. 建议执行顺序

1. **Part B（Muri 结构化）** 先做：范围自足、有 `muri_spec` 兜底，且能立刻验证「结构化+模板」
   模式，为 Part A 的三表模式打样。
2. **Part A** 按功能域小批推进：先 `enMap` 落地 + spec 切三表一致，再一个域一个域迁移
   （建议序：`cover` → `media_tools` → `net`(UI 部分) → `document`/`metadata` → 其余）。
3. 每批构建 + ctest 绿再提交；全部完成后删 `localized()` + 词典 + helper。

---

## 附录 A — 全部内联字符串索引（433 条）

> 建议 key 为脚本 `slug(en)` 派生的**起点**；同一 zh 多 en 的加了 `_2/_3` 序号（迁移时重点核
> 对语境给正确 ja）。`ja` 空列＝当前无日语（多为 net 日志，回退英文可接受）。`source` 为出处文件。


### `cover` (132)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `cover.add_a_chart_frame_a` | Add a chart frame (A) | 添加谱面帧（快捷键 A） | 譜面フレームを追加（ショートカット A） | CoverLayerListPanel.cpp |
| `cover.add_chart_frame` | Add chart frame | 添加谱面帧 | 譜面フレームを追加 | CoverStudioPanel.cpp |
| `cover.add_difficulty_card` | Add difficulty card | 添加难度卡 | 難易度カードを追加 | CoverStudioPanel.cpp |
| `cover.add_frame` | ＋ Add frame | ＋ 添加谱面帧 | ＋ 譜面フレームを追加 | CoverLayerListPanel.cpp |
| `cover.apply_preset` | Apply preset | 应用预设 | プリセットを適用 | CoverStudioWindow.cpp |
| `cover.backdrop_brightness` | Backdrop brightness | 背景亮度（底图明暗） | 背景の明るさ（下地の明暗） | CoverStudioPanel.cpp |
| `cover.background` | Background | 背景 | 背景 | CoverStudioPanel.cpp, VideoExportDialog.cpp |
| `cover.background_brightness` | Background brightness | 背景亮度 | 背景の明るさ | CoverStudioPanel.cpp |
| `cover.background_transparency` | Background transparency | 背景透明度 | 背景の透明度 | CoverStudioPanel.cpp |
| `cover.blur_background` | Blur background | 背景虚化 | 背景をぼかす | CoverStudioPanel.cpp, VideoExportDialog.cpp |
| `cover.brightness` | Brightness | 亮度 | 明るさ | CoverInspectorPanel.cpp |
| `cover.brightness_2` | Brightness | 背景亮度 | 背景の明るさ | CoverStudioPanel.cpp |
| `cover.bring_to_front` | Bring to front | 置顶 | 最前面へ | CoverLayerListPanel.cpp |
| `cover.browse` | Browse… | 浏览… | 参照… | CoverStudioPanel.cpp, VideoExportDialog.cpp |
| `cover.canvas` | Canvas | 画板 | キャンバス | CoverStudioPanel.cpp |
| `cover.card_chart_frame` | Card + chart frame | 卡片 + 谱面帧 | カード + 譜面フレーム | CoverStudioWindow.cpp |
| `cover.card_drop_shadow` | Card drop shadow | 难度卡阴影 | カードのドロップシャドウ | CoverStudioPanel.cpp, VideoExportDialog.cpp |
| `cover.centered_card_default` | Centered card (default) | 卡片居中（默认） | カード中央寄せ（既定） | CoverStudioWindow.cpp |
| `cover.chart_frame` | Chart frame | 谱面帧 | 譜面フレーム | CoverInspectorPanel.cpp, CoverLayerListModel.cpp, CoverStudioPanel.cpp |
| `cover.chart_frame_background_brightness` | Chart-frame background brightness | 谱面帧背景亮度 | 譜面フレームの背景の明るさ | CoverInspectorPanel.cpp |
| `cover.chart_frame_background_transparency` | Chart-frame background transparency | 谱面帧背景透明度 | 譜面フレームの背景の透明度 | CoverInspectorPanel.cpp |
| `cover.chart_frame_inner_background` | Chart-frame inner background | 谱面帧内圈背景 | 譜面フレームの内側背景 | CoverInspectorPanel.cpp, CoverStudioPanel.cpp |
| `cover.chart_frame_options` | Chart frame options | 谱面帧选项 | 譜面フレームのオプション | CoverInspectorPanel.cpp |
| `cover.chart_jacket` | Chart jacket (曲绘) | 曲绘 | ジャケット | CoverStudioPanel.cpp, VideoExportDialog.cpp |
| `cover.chart_type` | Chart type | 谱面类型 | 譜面タイプ | CoverStudioPanel.cpp, VideoExportDialog.cpp |
| `cover.choose_background_image` | Choose background image | 选择背景图片 | 背景画像を選択 | CoverStudioPanel.cpp, VideoExportDialog.IntroControls.cpp |
| `cover.clear_recent` | Clear recent | 清除最近 | 最近の履歴を消去 | CoverStudioWindow.cpp |
| `cover.close` | Close | 关闭 | オフ | CoverStudioWindow.cpp, NetBatchDownloadDialog.cpp |
| `cover.close_without_exporting_esc` | Close without exporting (Esc) | 关闭而不导出（Esc） | 出力せずに閉じる（Esc） | CoverStudioWindow.cpp |
| `cover.could_not_read_the_layout` | Could not read the layout file. | 无法读取布局文件。 | レイアウトファイルを読み込めません。 | CoverStudioPanel.cpp |
| `cover.could_not_render_the_chart` | Could not render the chart frame. | 无法渲染谱面帧。 | 譜面フレームをレンダリングできません。 | CoverStudioPanel.cpp |
| `cover.could_not_write_the_layout` | Could not write the layout file. | 无法写入布局文件。 | レイアウトファイルを書き込めません。 | CoverStudioPanel.cpp |
| `cover.cover_export_completed` | Cover export completed. | 封面导出完成。 | カバーの出力が完了しました。 | CoverStudioWindow.cpp |
| `cover.cover_export_failed_1` | Cover export failed:\n%1 | 封面导出失败：\n%1 | カバーの出力に失敗しました：\n%1 | CoverStudioWindow.cpp |
| `cover.cover_layout_miacover` | Cover layout (*.miacover) | 封面布局 (*.miacover) | カバーレイアウト (*.miacover) | CoverStudioPanel.cpp |
| `cover.cover_layout_miacover_legacy_json` | Cover layout (*.miacover);;Legacy JSON (*.json) | 封面布局 (*.miacover);;旧版 JSON (*.json) | カバーレイアウト (*.miacover);;旧版 JSON (*.json) | CoverStudioPanel.cpp |
| `cover.custom_background_image_path` | Custom background image path | 自定义背景图片路径 | カスタム背景画像のパス | CoverStudioPanel.cpp, VideoExportDialog.cpp |
| `cover.custom_image` | Custom image | 自定义图片 | カスタム画像 | CoverStudioPanel.cpp, VideoExportDialog.cpp |
| `cover.delete_preset` | Delete preset | 删除预设 | プリセットを削除 | CoverStudioWindow.cpp |
| `cover.delete_the_selected_layer_delete` | Delete the selected layer (Delete) | 删除当前图层（Delete） | 選択中のレイヤーを削除（Delete） | CoverLayerListPanel.cpp |
| `cover.delete_this_preset` | Delete this preset? | 删除这个预设？ | このプリセットを削除しますか？ | CoverStudioWindow.cpp |
| `cover.difficulty_card` | Difficulty card | 难度卡 | 難易度カード | CoverInspectorPanel.cpp, CoverLayerListModel.cpp, VideoExportDialog.cpp |
| `cover.difficulty_card_2` | Difficulty card | 难度卡片 | 難易度カード | CoverLayerListModel.cpp |
| `cover.difficulty_card_options` | Difficulty card options | 难度卡选项 | 難易度カードのオプション | CoverStudioPanel.cpp |
| `cover.dual_chart_frame_collage` | Dual chart-frame collage | 双谱面帧拼贴 | 譜面フレーム 2 枚のコラージュ | CoverStudioWindow.cpp |
| `cover.export` | Export | 导出 | 出力 | CoverStudioPanel.cpp, CoverStudioWindow.cpp |
| `cover.export_cover` | Export Cover | 导出封面 | カバーを出力 | CoverStudioPanel.cpp, CoverStudioWindow.cpp, ExportCoverDialog.cpp |
| `cover.failed_to_start_the_composer` | Failed to start the composer:\n%1 | 合成器启动失败：\n%1 | コンポーザーの起動に失敗しました：\n%1 | CoverStudioPanel.cpp |
| `cover.file_not_found` | File not found | 文件不存在 | ファイルが存在しません | CoverStudioWindow.cpp |
| `cover.frame` | Frame | 帧 | フレーム | CoverFramePickerPanel.cpp |
| `cover.frame_2` | Frame  | 帧时间  | フレーム時間  | CoverLayerListModel.cpp |
| `cover.frame_time` | Frame time | 帧时间 | フレーム時間 | CoverInspectorPanel.cpp |
| `cover.frame_time_2` | Frame time | 谱面时间 | フレーム時間 | CoverStudioPanel.cpp |
| `cover.frame_time_for_the_selected` | Frame time for the selected chart frame | 当前谱面帧的时间 | 選択中の譜面フレームの時間 | CoverFramePickerPanel.cpp |
| `cover.frame_time_for_the_selected_2` | Frame time for the selected chart frame | 当前谱面帧的帧时间 | 選択中の譜面フレームのフレーム時間 | CoverInspectorPanel.cpp |
| `cover.hidden` |  · Hidden |  · 已隐藏 |  · 非表示 | CoverLayerListPanel.cpp |
| `cover.hide` | Hide | 隐藏 | 非表示 | CoverLayerListPanel.cpp |
| `cover.hide_layer_v` | Hide layer (V) | 隐藏图层（快捷键 V） | レイヤーを非表示（ショートカット V） | CoverLayerListPanel.cpp |
| `cover.horizontal_position` | Horizontal position | 水平位置 | 水平位置 | CoverInspectorPanel.cpp |
| `cover.images_png_jpg_jpeg_bmp` | Images (*.png *.jpg *.jpeg *.bmp *.webp) | 图片 (*.png *.jpg *.jpeg *.bmp *.webp) | 画像 (*.png *.jpg *.jpeg *.bmp *.webp) | CoverStudioPanel.cpp, VideoExportDialog.IntroControls.cpp |
| `cover.import_cover_layout` | Import cover layout | 导入封面布局 | カバーレイアウトを読み込む | CoverStudioPanel.cpp |
| `cover.import_layout` | Import layout… | 导入布局… | レイアウトを読み込む… | CoverStudioPanel.cpp |
| `cover.import_layout_2` | Import layout | 导入布局 | レイアウトを読み込む | CoverStudioPanel.cpp |
| `cover.import_layout_file` | Import layout file… | 导入布局文件… | レイアウトファイルを読み込む… | CoverStudioWindow.cpp |
| `cover.inner_bg` | Inner bg | 内圈背景 | 内側の背景 | CoverInspectorPanel.cpp |
| `cover.jacket` | Jacket | 曲绘 | ジャケット | CoverInspectorPanel.cpp |
| `cover.keep_size_ellipsis` | Keep size, ellipsis (…) | 保持字号，省略号(…)截断 | 文字サイズを保持し省略記号(…)で切り詰め | CoverStudioPanel.cpp |
| `cover.layer` | Layer | 图层 | レイヤー | CoverInspectorPanel.cpp |
| `cover.layer_2` | Layer ·  | 图层 ·  | レイヤー ·  | CoverInspectorPanel.cpp |
| `cover.layer_opacity` | Layer opacity | 图层不透明度 | レイヤーの不透明度 | CoverInspectorPanel.cpp |
| `cover.layer_size` | Layer size | 图层大小 | レイヤーサイズ | CoverInspectorPanel.cpp |
| `cover.layers` | Layers | 图层 | レイヤー | CoverLayerListPanel.cpp |
| `cover.layout` | Layout ▾ | 布局 ▾ | レイアウト ▾ | CoverStudioWindow.cpp |
| `cover.lock` | Lock | 锁定 | ロック | CoverInspectorPanel.cpp, CoverLayerListPanel.cpp |
| `cover.lock_geometry_l` | Lock geometry (L) | 锁定位置和大小（快捷键 L） | 位置とサイズをロック（ショートカット L） | CoverLayerListPanel.cpp |
| `cover.lock_position_and_size_l` | Lock position and size (L) | 锁定位置与大小，防止拖动（快捷键 L） | 位置とサイズをロックしてドラッグを防止（ショートカット L） | CoverInspectorPanel.cpp |
| `cover.long_text` | Long text | 文字超长 | 文字が長すぎます | CoverStudioPanel.cpp |
| `cover.manage_presets` | Manage presets... | 管理预设... | プリセットを管理... | CoverStudioWindow.cpp |
| `cover.manage_presets_2` | Manage presets | 管理预设 | プリセットを管理 | CoverStudioWindow.cpp |
| `cover.move_down` | Move down | 下移 | 下へ移動 | CoverLayerListPanel.cpp |
| `cover.move_up` | Move up | 上移 | 上へ移動 | CoverLayerListPanel.cpp |
| `cover.no_recent_files` | (No recent files) | （无最近文件） | （最近のファイルなし） | CoverStudioWindow.cpp |
| `cover.opacity` | Opacity | 不透明度 | 不透明度 | CoverInspectorPanel.cpp |
| `cover.open` | Open | 打开 | 開く | CoverStudioWindow.cpp |
| `cover.open_recent` | Open recent | 打开最近 | 最近のファイルを開く | CoverStudioWindow.cpp |
| `cover.play_pause_space` | Play / pause (Space) | 播放 / 暂停（空格） | 再生 / 一時停止（スペース） | CoverFramePickerPanel.cpp, CoverInspectorPanel.cpp |
| `cover.play_pause_visual_only` | Play / pause (visual only) | 播放 / 暂停（仅画面） | 再生 / 一時停止（映像のみ） | CoverStudioPanel.cpp |
| `cover.preset_name` | Preset name: | 预设名称： | プリセット名： | CoverStudioWindow.cpp |
| `cover.pure_chart_frame` | Pure chart frame | 纯谱面帧（无卡片） | 譜面フレームのみ（カードなし） | CoverStudioWindow.cpp |
| `cover.rename_preset` | Rename preset | 重命名预设 | プリセットの名前を変更 | CoverStudioWindow.cpp |
| `cover.render_and_save_the_cover` | Render and save the cover image | 渲染并保存封面图片 | カバー画像をレンダリングして保存 | CoverStudioWindow.cpp |
| `cover.render_level_as_text` | Render level as text | 等级文本渲染 | レベルをテキストで表示 | CoverStudioPanel.cpp, VideoExportDialog.cpp |
| `cover.reset_canvas_zoom` | Reset canvas zoom | 还原画布缩放 | キャンバスの拡大率をリセット | CoverStudioWindow.cpp |
| `cover.reset_canvas_zoom_ctrl_0` | Reset canvas zoom (Ctrl+0) | 还原画布缩放（Ctrl+0） | キャンバスの拡大率をリセット（Ctrl+0） | CoverStudioWindow.cpp |
| `cover.reset_discards_all_current_layers` | Reset discards all current layers and positions. Continue? | 重置将丢弃当前所有图层与位置，继续？ | リセットすると現在のすべてのレイヤーと位置が破棄されます。続行しますか？ | CoverStudioWindow.cpp |
| `cover.reset_layout` | Reset layout | 重置布局 | レイアウトをリセット | CoverStudioPanel.cpp, CoverStudioWindow.cpp |
| `cover.reset_save_import_recent_layouts` | Reset / save / import / recent layouts | 重置 / 保存 / 导入 / 最近布局 | リセット / 保存 / 読み込み / 最近のレイアウト | CoverStudioWindow.cpp |
| `cover.reset_to_default` | Reset to default… | 重置为默认布局… | 既定のレイアウトにリセット… | CoverStudioWindow.cpp |
| `cover.save_cover_layout` | Save cover layout | 保存封面布局 | カバーレイアウトを保存 | CoverStudioPanel.cpp |
| `cover.save_current_as_preset` | Save current as preset... | 保存当前为预设... | 現在の設定をプリセットとして保存... | CoverStudioWindow.cpp |
| `cover.save_layout` | Save layout… | 保存布局… | レイアウトを保存… | CoverStudioPanel.cpp |
| `cover.save_layout_2` | Save layout | 保存布局 | レイアウトを保存 | CoverStudioPanel.cpp |
| `cover.save_layout_to_file` | Save layout to file… | 保存布局到文件… | レイアウトをファイルに保存… | CoverStudioWindow.cpp |
| `cover.save_preset` | Save preset | 保存预设 | プリセットを保存 | CoverStudioWindow.cpp |
| `cover.select_a_chart_frame_layer` | Select a chart-frame layer to edit its time | 选择谱面帧图层以编辑帧时间 | 譜面フレームレイヤーを選んでフレーム時間を編集 | CoverFramePickerPanel.cpp |
| `cover.send_to_back` | Send to back | 置底 | 最背面へ | CoverLayerListPanel.cpp |
| `cover.show` | Show | 显示 | 表示 | CoverLayerListPanel.cpp |
| `cover.show_layer_v` | Show layer (V) | 显示图层（快捷键 V） | レイヤーを表示（ショートカット V） | CoverLayerListPanel.cpp |
| `cover.show_or_hide_this_layer` | Show or hide this layer (V) | 显示或隐藏当前图层（快捷键 V） | このレイヤーの表示/非表示（ショートカット V） | CoverInspectorPanel.cpp |
| `cover.shrink_to_fit` | Shrink to fit | 缩小字体以放入全部 | 縮小して全体を収める | CoverStudioPanel.cpp |
| `cover.size` | Size | 大小 | サイズ | CoverInspectorPanel.cpp |
| `cover.size_2` | Size | 尺寸 | サイズ | CoverStudioPanel.cpp |
| `cover.step_back` | Step back (←) | 后退一步（←） | 1 コマ戻る（←） | CoverFramePickerPanel.cpp |
| `cover.step_forward` | Step forward (→) | 前进一步（→） | 1 コマ進む（→） | CoverFramePickerPanel.cpp |
| `cover.the_chart_frame_could_not` | The chart frame could not be rendered; the cover will not include it. | 谱面帧无法渲染，封面将不包含它。 | 譜面フレームをレンダリングできないため、カバーには含まれません。 | CoverStudioPanel.cpp |
| `cover.the_custom_background_image_was` | The custom background image was not found; using the chart jacket instead. | 自定义背景图片未找到，已回退为曲绘背景。 | カスタム背景画像が見つからないため、譜面ジャケットに戻しました。 | CoverStudioPanel.cpp |
| `cover.the_layout_file_is_not` | The layout file is not valid JSON. | 布局文件不是有效的 JSON。 | レイアウトファイルが有効な JSON ではありません。 | CoverStudioPanel.cpp |
| `cover.this_difficulty_has_no_chart` | This difficulty has no chart notes to render. | 当前难度没有可渲染的谱面音符。 | この難易度にはレンダリングできる譜面ノーツがありません。 | CoverStudioPanel.cpp |
| `cover.this_file_is_not_a` | This file is not a MiaCode cover layout. | 该文件不是 MiaCode 封面布局文件。 | このファイルは MiaCode のカバーレイアウトファイルではありません。 | CoverStudioPanel.cpp |
| `cover.this_preset_needs_a_renderable` | This preset needs a renderable chart frame | 该预设需要可渲染的谱面帧 | このプリセットにはレンダリング可能な譜面フレームが必要です | CoverStudioWindow.cpp |
| `cover.transparency` | Transparency | 透明度 | 透明度 | CoverInspectorPanel.cpp |
| `cover.transparent` | Transparent | 透明 | 透明 | CoverInspectorPanel.cpp, CoverStudioPanel.cpp |
| `cover.unlock` | Unlock | 解锁 | ロック解除 | CoverLayerListPanel.cpp |
| `cover.unlock_geometry_l` | Unlock geometry (L) | 解锁位置和大小（快捷键 L） | 位置とサイズのロックを解除（ショートカット L） | CoverLayerListPanel.cpp |
| `cover.vertical_position` | Vertical position | 垂直位置 | 垂直位置 | CoverInspectorPanel.cpp |
| `cover.visible` | Visible | 显示 | 表示 | CoverInspectorPanel.cpp |
| `cover.x` | X | 水平位置 | 水平位置 | CoverInspectorPanel.cpp |
| `cover.y` | Y | 垂直位置 | 垂直位置 | CoverInspectorPanel.cpp |
| `cover.zoom_canvas_in` | Zoom canvas in | 放大画布视图 | キャンバスを拡大 | CoverStudioWindow.cpp |
| `cover.zoom_canvas_in_ctrl` | Zoom canvas in (Ctrl++) | 放大画布视图（Ctrl++） | キャンバスを拡大（Ctrl++） | CoverStudioWindow.cpp |
| `cover.zoom_canvas_out` | Zoom canvas out | 缩小画布视图 | キャンバスを縮小 | CoverStudioWindow.cpp |
| `cover.zoom_canvas_out_ctrl` | Zoom canvas out (Ctrl+-) | 缩小画布视图（Ctrl+-） | キャンバスを縮小（Ctrl+-） | CoverStudioWindow.cpp |

### `dialogs` (1)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `dialogs.open_folder` | Open Folder | 打开文件夹 | フォルダーを開く | MainWindow.Dialogs.cpp |

### `document` (32)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `document.1_comment_bookmarks` | %1 · comment bookmarks | %1 · 注释书签 | %1 · コメントブックマーク | MainWindow.DocumentUi.cpp |
| `document.1_line_2_double_click` | %1\nLine %2 · double-click to rename | %1\n第 %2 行 · 双击重命名 | %1\n%2 行目 · ダブルクリックで名前変更 | MainWindow.DocumentUi.cpp |
| `document.all_difficulties_share_this_designer` | All difficulties share this designer | 所有难度采用相同名义 | すべての難易度で同じ作者名を使用 | MainWindow.DocumentDesignerFlow.cpp |
| `document.bpm_1_offset_2_s` | BPM %1  ·  Offset %2 s | BPM %1　·　偏移 %2 s | BPM %1　·　オフセット %2 s | MainWindow.DocumentUi.cpp |
| `document.clear_all` | Clear all | 直接清除 | すべて消去 | MainWindow.DocumentDesignerFlow.cpp |
| `document.delete_1` | Delete %1? | 确定删除 %1 吗？该难度的等级、谱师与谱面内容将一并移除。 | %1 を削除しますか？この難易度のレベル・作者・譜面内容もすべて削除されます。 | MainWindow.DocumentUi.cpp |
| `document.delete_difficulty` | Delete Difficulty | 删除难度 | 難易度を削除 | MainWindow.DocumentUi.cpp |
| `document.deleted_1` | Deleted %1. | 已删除 %1。 | %1 を削除しました。 | MainWindow.DocumentUi.cpp |
| `document.deleted_1_changes_are_still` | Deleted %1. Changes are still unsaved. | 已删除 %1，更改尚未保存。 | %1 を削除しました。変更はまだ保存されていません。 | MainWindow.DocumentUi.cpp |
| `document.designer_management` | Designer management | 谱师名义管理 | 譜面作者名の管理 | MainWindow.DocumentDesignerFlow.cpp |
| `document.designers` | Designers | 谱师 | 譜面作者 | MainWindow.DocumentDesignerFlow.cpp |
| `document.file_already_exists` | File Already Exists | 文件已存在 | ファイルが既に存在します | MainWindow.DocumentFileFlow.cpp |
| `document.latency_settings` | Latency Settings | 延迟设置 | 遅延設定 | MainWindow.DocumentUi.cpp |
| `document.ln_1_col_2` | Ln %1, Col %2 | %1行 %2列 | %1 行 %2 列 | MainWindow.DocumentUi.cpp |
| `document.ln_9999_col_9999` | Ln 9999, Col 9999 | 9999行 9999列 | 9999 行 9999 列 | MainWindow.DocumentUi.cpp, MainWindow.FrameBootstrap.cpp |
| `document.maidata_txt_already_exists_in` | maidata.txt already exists in the selected folder. Overwrite it? | 所选文件夹下已存在 maidata.txt，是否覆盖？ | 選択したフォルダーに既に maidata.txt があります。上書きしますか？ | MainWindow.DocumentFileFlow.cpp |
| `document.no_chart_yet_records_des` | No chart yet — records &des_%1 only. | 该难度暂无谱面，仅记录 &des_%1。 | 譜面がまだないため、&des_%1 のみ記録します。 | MainWindow.DocumentDesignerFlow.cpp |
| `document.open_the_export_page_video` | Open the Export page: video / cover / batch / ZIP | 打开导出页：导出视频 / 导出封面 / 批量导出 / 打包ZIP | 出力ページを開く：動画 / カバー / 一括 / ZIP | MainWindow.DocumentUi.cpp, MainWindow.FrameBootstrapFinalize.cpp |
| `document.open_toolbox_muri_check_format` | Open toolbox: Muri Check / Format Chart / Official Chart Mirror | 打开工具箱：无理检测 / 谱面整理 / 官谱镜像站 | ツールボックスを開く：無理チェック / 譜面整形 / 公式譜面ミラー | MainWindow.DocumentUi.cpp |
| `document.pick_the_canonical_designer` | Pick the canonical designer | 选择统一的谱师名 | 統一する作者名を選択 | MainWindow.DocumentDesignerFlow.cpp |
| `document.select_chart_folder` | Select Chart Folder | 选择谱面文件夹 | 譜面フォルダーを選択 | MainWindow.DocumentFileFlow.cpp |
| `document.selection_full_chart` | Selection: full chart | 选中范围：全文 | 選択範囲：譜面全体 | MainWindow.DocumentTransforms.cpp |
| `document.selection_l_1c_2_l` | Selection: L%1C%2 ~ L%3C%4 | 选中范围：%1行%2列 ~ %3行%4列 | 選択範囲：%1 行 %2 列 ~ %3 行 %4 列 | MainWindow.DocumentTransforms.cpp |
| `document.snap_approximately_to_384_grid` | Snap approximately to 384 grid | 统一近似至384分音 | 384 分音におおよそスナップ | MainWindow.DocumentTransforms.cpp |
| `document.subdivision_1` | Subdivision +1 | 分音提升一档 | 分音を 1 段上げる | MainWindow.DocumentTransforms.cpp, MainWindow.BootstrapAndMenus.cpp |
| `document.subdivision_1_2` | Subdivision -1 | 分音降低一档 | 分音を 1 段下げる | MainWindow.DocumentTransforms.cpp, MainWindow.BootstrapAndMenus.cpp |
| `document.subdivision_1_2` | Subdivision +1/2 | 分音提升半档 | 分音を半段上げる | MainWindow.DocumentTransforms.cpp, MainWindow.BootstrapAndMenus.cpp |
| `document.subdivision_1_2_2` | Subdivision -1/2 | 分音降低半档 | 分音を半段下げる | MainWindow.DocumentTransforms.cpp, MainWindow.BootstrapAndMenus.cpp |
| `document.toolbox` | Toolbox | 工具箱 | ツールボックス | MainWindow.DocumentUi.cpp |
| `document.treat_selection_start_as_measure` | Treat selection start as measure boundary | 选区起点视作小节线开始 | 選択の始点を小節線の開始とみなす | MainWindow.DocumentTransforms.cpp |
| `document.untitled_bookmark` | Untitled Bookmark | 未命名书签 | 無名のブックマーク | MainWindow.DocumentUi.cpp, MainWindow.EditorDisplay.cpp |
| `document.when_checked_des_and_every` | When checked, &des and every &des_N stay identical. | 勾选后，&des 与每个难度的 &des_N 会保持一致。 | チェックすると &des と各難易度の &des_N が一致します。 | MainWindow.DocumentDesignerFlow.cpp |

### `editor` (5)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `editor.delete_bookmark` | Delete Bookmark | 删除书签 | ブックマークを削除 | MainWindow.EditorDisplay.cpp, MainWindow.FrameBootstrap.cpp |
| `editor.delete_bookmark_1_this_will` | Delete bookmark "%1"? This will delete the chart comment on that line. | 确定删除书签“%1”吗？这会删除该行的谱面注释。 | ブックマーク「%1」を削除しますか？その行の譜面コメントも削除されます。 | MainWindow.EditorDisplay.cpp |
| `editor.l_1` | L%1 | 第 %1 行 | %1 行目 | MainWindow.EditorDisplay.cpp |
| `editor.new_bookmark` | New Bookmark | 新书签 | 新規ブックマーク | MainWindow.EditorDisplay.cpp |
| `editor.target_line_already_has_a` | Target line already has a comment; bookmark move canceled. | 目标行已经有注释，已取消移动书签。 | 移動先の行には既にコメントがあるため、ブックマークの移動をキャンセルしました。 | MainWindow.EditorDisplay.cpp |

### `export` (7)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `export.export_as_zip` | Export as ZIP | 导出为ZIP | ZIP で出力 | MainWindow.PackZip.cpp |
| `export.exported_to_1_2_file` | Exported to:\n%1\n\n%2 file(s) included:\n%3 | 已导出到：\n%1\n\n包含 %2 个文件：\n%3 | 出力先：\n%1\n\n%2 個のファイルを含みます：\n%3 | MainWindow.PackZip.cpp |
| `export.packaging_1_2_3` | Packaging %1/%2\n%3 | 正在打包 %1/%2\n%3 | パッケージ化中 %1/%2\n%3 | MainWindow.PackZip.cpp |
| `export.packaging_canceled` | Packaging canceled. | 已取消打包。 | パッケージ化をキャンセルしました。 | MainWindow.PackZip.cpp |
| `export.packaging_failed_1` | Packaging failed.\n\n%1 | 打包失败。\n\n%1 | パッケージ化に失敗しました。\n\n%1 | MainWindow.PackZip.cpp |
| `export.preparing_package` | Preparing package... | 正在准备打包… | パッケージ化を準備中… | MainWindow.PackZip.cpp |
| `export.the_chart_is_empty_there` | The chart is empty; there is nothing to package. | 谱面为空，没有可打包的内容。 | 譜面が空で、パッケージ化する内容がありません。 | MainWindow.PackZip.cpp, ExportLauncherPage.cpp |

### `export_page` (11)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `export_page.batch_export` | Batch Export | 批量导出 | 一括出力 | ExportLauncherPage.cpp |
| `export_page.export_cover` | Export Cover | 封面导出 | カバー出力 | ExportLauncherPage.cpp |
| `export_page.export_video` | Export Video | 视频导出 | 動画出力 | ExportLauncherPage.cpp |
| `export_page.no_difficulty_has_chart_content` | No difficulty has chart content yet, so there is nothing to export. | 暂无包含谱面内容的难度，无法导出。 | 譜面内容を含む難易度がまだないため、出力できません。 | ExportLauncherPage.cpp |
| `export_page.no_difficulty_is_available_to` | No difficulty is available to export. | 暂无可导出的难度。 | 出力できる難易度がありません。 | ExportLauncherPage.cpp |
| `export_page.open_composer` | Open Composer… ↗ | 打开合成器… ↗ | コンポーザーを開く… ↗ | ExportLauncherPage.cpp |
| `export_page.open_queue` | Open Queue… ↗ | 打开队列… ↗ | キューを開く… ↗ | ExportLauncherPage.cpp |
| `export_page.pack_as_zip` | Pack as ZIP | 打包 ZIP | ZIP パッケージ化 | ExportLauncherPage.cpp |
| `export_page.pack_now` | Pack Now | 立即打包 | 今すぐパッケージ化 | ExportLauncherPage.cpp |
| `export_page.the_selected_difficulty_has_no` | The selected difficulty has no chart content to export. | 当前难度暂无谱面内容，无法导出视频。 | 選択中の難易度には出力できる譜面内容がありません。 | ExportLauncherPage.cpp |
| `export_page.the_video_export_panel_is` | The video export panel is unavailable right now. | 视频导出面板暂不可用。 | 動画出力パネルは現在利用できません。 | ExportLauncherPage.cpp |

### `latency` (18)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `latency.auto_detect` | Auto-detect | 自动检测 | 自動検出 | LatencyDetectionPage.cpp |
| `latency.back_to_chart_info` | ← Back to Chart Info | ← 返回谱面信息 | ← 譜面情報に戻る | LatencyDetectionPage.cpp |
| `latency.bpm_not_detected` | BPM not detected | 未检测到 BPM | BPM を検出できません | LatencyDetectionPage.cpp |
| `latency.chart_parameters` | Chart Parameters | 谱面参数 | 譜面パラメーター | LatencyDetectionPage.cpp |
| `latency.detected_1` | Detected: %1 | 检测结果: %1 | 検出結果: %1 | LatencyDetectionPage.cpp |
| `latency.detected_1_s` | Detected: %1 s | 检测结果: %1 秒 | 検出結果: %1 秒 | LatencyDetectionPage.cpp |
| `latency.offset` | Offset | 偏移 | オフセット | LatencyDetectionPage.cpp |
| `latency.open_audio_video_tools_sample` | Open audio/video tools: sample-rate convert / compress video / prepend silence / prepend black. | 打开音频/视频处理工具：采样率转换 / 视频压缩 / 开头静音 / 开头黑幕。 | オーディオ/動画ツールを開く：サンプルレート変換 / 動画圧縮 / 先頭無音 / 先頭黒画面。 | LatencyDetectionPage.cpp |
| `latency.pause` | ⏸ Pause | ⏸ 暂停 | ⏸ 一時停止 | LatencyDetectionPage.cpp |
| `latency.requires_a_loaded_track_audio` | Requires a loaded track audio file | 需要先加载歌曲音频 | 先に曲のオーディオファイルを読み込む必要があります | LatencyDetectionPage.cpp |
| `latency.reset_volume` | Reset volume | 重置音量 | 音量をリセット | LatencyDetectionPage.cpp |
| `latency.rhythm_calibration_audition` | Rhythm Calibration Audition | 节奏校准试听 | リズム校正の試聴 | LatencyDetectionPage.cpp |
| `latency.s` |  s |  秒 |  秒 | LatencyDetectionPage.cpp |
| `latency.set_or_detect_bpm_first` | Set or detect BPM first | 先设置/检测 BPM | 先に BPM を設定/検出してください | LatencyDetectionPage.cpp |
| `latency.sfx_volume` | SFX Volume | SFX 音量 | SFX 音量 | LatencyDetectionPage.cpp |
| `latency.start_audition` | ▶ Start Audition | ▶ 开始试听 | ▶ 試聴を開始 | LatencyDetectionPage.cpp |
| `latency.subdivision` | Subdivision: | 分音: | 分音: | LatencyDetectionPage.cpp |
| `latency.track_audio_missing` | Track audio missing | 缺少歌曲音频 | 曲のオーディオがありません | LatencyDetectionPage.cpp |

### `media_tools` (49)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `media_tools.1_was_not_found_next` | %1 was not found next to the current chart. | 当前谱面目录缺少 %1。 | 現在の譜面フォルダーに %1 がありません。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.a_black_screen` | a black screen | 黑幕 | 黒画面 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.audio_video_processing` | Audio/Video Processing | 音频/视频处理 | オーディオ/動画処理 | MainWindow.Dialogs.MediaTools.cpp, MainWindow.FrameBootstrap.cpp, LatencyDetectionPage.cpp |
| `media_tools.background_mp4_video` | background .mp4 video | 背景视频 .mp4 | 背景動画 .mp4 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.backup_restored` | Backup restored. | 已还原备份。 | バックアップを復元しました。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.beats` | Beats | 拍数 | 拍数 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.black_screen` | black screen | 黑幕 | 黒画面 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.cancel` | Cancel | 取消 | キャンセル | MainWindow.Dialogs.MediaTools.cpp, MainWindow.PackZip.cpp, CoverStudioPanel.cpp, CoverStudioWindow.cpp, NetBatchDownloadDialog.cpp |
| `media_tools.canceled` | Canceled. | 已取消。 | キャンセルしました。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.compress_1_under_20_mib` | Compress %1 under 20 MiB and create/replace backup %2? | 将压缩 %1 到 20M 内，并生成/覆盖备份 %2。是否继续？ | %1 を 20 MiB 以内に圧縮し、バックアップ %2 を作成/上書きします。続行しますか？ | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.compress_the_background_video_under` | Compress the background video under 20 MiB (the original is backed up). | 将背景视频压缩到 20M 以内，并自动备份原文件。 | 背景動画を 20 MiB 以内に圧縮し、元ファイルを自動でバックアップします。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.compress_video` | Compress Video | 视频压缩 | 動画圧縮 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.compressed_1_under_20_mib` | Compressed %1 under 20 MiB. | 已压缩 %1 到 20M 内。 | %1 を 20 MiB 以内に圧縮しました。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.compressed_1_under_20_mib_2` | Compressed %1 under 20 MiB (original backed up as %2). | 已压缩 %1 到 20M 内（原文件已备份为 %2）。 | %1 を 20 MiB 以内に圧縮しました（元ファイルは %2 にバックアップ）。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.compressing_video` | Compressing video... | 正在压缩视频... | 動画を圧縮中... | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.convert_track_mp3_to_44100` | Convert track.mp3 to 44100 Hz and create/replace backup track_bak.mp3? | 将 track.mp3 处理为 44100Hz，并生成/覆盖备份 track_bak.mp3。是否继续？ | track.mp3 を 44100Hz に変換し、バックアップ track_bak.mp3 を作成/上書きします。続行しますか？ | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.convert_track_mp3_to_44100_2` | Convert track.mp3 to 44100 Hz (the original is backed up). | 将 track.mp3 转换为 44100Hz，并自动备份原文件。 | track.mp3 を 44100Hz に変換し、元ファイルを自動でバックアップします。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.converted_track_mp3_to_44100` | Converted track.mp3 to 44100 Hz. | 已将 track.mp3 处理为 44100Hz。 | track.mp3 を 44100Hz に変換しました。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.converted_track_mp3_to_44100_2` | Converted track.mp3 to 44100 Hz (original backed up as track_bak.mp3). | 已将 track.mp3 处理为 44100Hz（原文件已备份为 track_bak.mp3）。 | track.mp3 を 44100Hz に変換しました（元ファイルは track_bak.mp3 にバックアップ）。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.detect` | Detect | 自动检测 | 自動検出 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.failed_to_restore_backup_to` | Failed to restore backup to: %1\n\nThe file may be open in preview, a media player, File Explorer preview pane, or another program. | 还原备份失败：%1\n\n文件可能正在被预览、播放器、资源管理器预览窗格或其他程序占用。 | バックアップの復元に失敗しました：%1\n\nこのファイルはプレビュー、メディアプレーヤー、エクスプローラーのプレビューウィンドウ、または別のプログラムで開かれている可能性があります。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.failed_to_stage_original_file` | Failed to stage original file for replacement: %1\n\nThe file may still be open in preview, a media player, File Explorer preview pane, or another program. Stop preview and close programs using it, then try again. | 无法替换原文件：%1\n\n文件可能仍被预览、播放器、资源管理器预览窗格或其他程序占用。请停止预览并关闭占用该文件的程序后重试。 | 元ファイルを差し替えできません：%1\n\nこのファイルはまだプレビュー、メディアプレーヤー、エクスプローラーのプレビューウィンドウ、または別のプログラムで開かれている可能性があります。プレビューを停止し、使用中のプログラムを閉じてから再試行してください。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.failed_to_write_file_1` | Failed to write file: %1\n\nThe file may be open in preview, a media player, File Explorer preview pane, or another program. | 无法写入文件：%1\n\n文件可能正在被预览、播放器、资源管理器预览窗格或其他程序占用。 | ファイルを書き込めません：%1\n\nこのファイルはプレビュー、メディアプレーヤー、エクスプローラーのプレビューウィンドウ、または別のプログラムで開かれている可能性があります。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.ffmpeg_was_not_found_place` | ffmpeg was not found. Place ffmpeg next to the app or set MIACODE_FFMPEG_PATH. | 未找到 ffmpeg。请将 ffmpeg 放到程序目录，或设置 MIACODE_FFMPEG_PATH。 | ffmpeg が見つかりません。ffmpeg をアプリと同じ場所に置くか、MIACODE_FFMPEG_PATH を設定してください。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.insert_a_black_screen_at` | Insert a black screen at the start of the background video (the original is backed up). | 在背景视频开头插入一段黑幕，并自动备份原文件。 | 背景動画の先頭に黒画面を挿入し、元ファイルを自動でバックアップします。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.insert_silence_at_the_start` | Insert silence at the start of track.mp3 (the original is backed up). | 在 track.mp3 开头插入一段静音，并自动备份原文件。 | track.mp3 の先頭に無音を挿入し、元ファイルを自動でバックアップします。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.no_background_mp4_video_was` | No background .mp4 video was found next to the current chart. | 当前谱面目录缺少背景视频 .mp4。 | 現在の譜面フォルダーに背景動画 .mp4 がありません。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.open_or_save_a_chart` | Open or save a chart file first. | 请先打开或保存一个谱面文件。 | 先に譜面ファイルを開くか保存してください。 | MainWindow.Dialogs.MediaTools.cpp, MainWindow.Dialogs.TrackMetadata.cpp |
| `media_tools.prepend_pv_black_screen` | Prepend PV Black Screen | 视频开头黑幕处理 | 動画の先頭に黒画面を追加 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.prepend_track_silence` | Prepend Track Silence | 音频开头静音处理 | オーディオの先頭に無音を追加 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.prepended_2_s_of_3` | Prepended %2 s of %3 to %1 (original backed up as %4). | 已为 %1 开头添加 %2 秒%3（原文件已备份为 %4）。 | %1 の先頭に %2 秒の%3を追加しました（元ファイルは %4 にバックアップ）。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.prepended_2_seconds_of_blank` | Prepended %2 seconds of blank media to %1. | 已为 %1 开头添加 %2 秒空白。 | %1 の先頭に %2 秒の空白を追加しました。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.prepends_1_to_2_3` | Prepends %1 to %2: %3 quarter-notes at %4 BPM (~%5 s). | 将在 %2 开头增加一段%1，时长为 BPM %4 下的 %3 个 4 分音（约 %5 秒）。 | %2 の先頭に%1を追加します。長さは BPM %4 で %3 個の 4 分音（約 %5 秒）です。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.processing_audio` | Processing audio... | 正在处理音频... | オーディオを処理中... | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.processing_pv_mp4` | Processing pv.mp4... | 正在处理 pv.mp4... | pv.mp4 を処理中... | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.processing_track_mp3` | Processing track.mp3... | 正在处理 track.mp3... | track.mp3 を処理中... | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.restore_backup` | Restore Backup | 还原备份 | バックアップを復元 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.sample_rate` | Sample Rate | 采样率转换 | サンプルレート変換 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.sample_rate_conversion_canceled` | Sample-rate conversion canceled. | 已取消采样率转换。 | サンプルレート変換をキャンセルしました。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.silence` | silence | 空白 | 空白 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.the_background_video` | the background video | 背景视频 | 背景動画 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.the_current_video_is_already` | The current video is already under 20 MiB; compression is not needed. | 当前视频已经小于 20 MiB，无需压缩。 | 現在の動画は既に 20 MiB 未満です。圧縮は不要です。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.the_current_video_is_already_2` | The current video is already under 20 MiB (%1); compression is not needed. | 当前视频已经小于 20 MiB（%1），无需压缩。 | 現在の動画は既に 20 MiB 未満です（%1）。圧縮は不要です。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.track_mp3_failed` | track.mp3 Failed | track.mp3 处理失败 | track.mp3 の処理に失敗しました | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.track_mp3_processing_canceled` | track.mp3 processing canceled. | 已取消 track.mp3 处理。 | track.mp3 の処理をキャンセルしました。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.track_mp3_was_not_found` | track.mp3 was not found next to the current chart. | 当前谱面目录缺少 track.mp3。 | 現在の譜面フォルダーに track.mp3 がありません。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.video_compression_canceled` | Video compression canceled. | 已取消视频压缩。 | 動画の圧縮をキャンセルしました。 | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.video_failed` | Video Failed | 视频处理失败 | 動画の処理に失敗しました | MainWindow.Dialogs.MediaTools.cpp |
| `media_tools.video_processing_canceled` | Video processing canceled. | 已取消视频处理。 | 動画の処理をキャンセルしました。 | MainWindow.Dialogs.MediaTools.cpp |

### `menu` (15)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `menu.bpm_latency` | BPM && Latency | BPM && 延迟检测 | BPM && 遅延検出 | MainWindow.BootstrapAndMenus.cpp |
| `menu.clear_elements` | Clear Elements | 一键清空 | すべて消去 | MainWindow.BootstrapAndMenus.cpp |
| `menu.compress_video` | Compress Video... | 视频压缩 | 動画圧縮 | MainWindow.BootstrapAndMenus.cpp |
| `menu.export_as_zip` | Export as ZIP... | 导出为ZIP | ZIP で出力 | MainWindow.BootstrapAndMenus.cpp |
| `menu.find_replace` | Find/Replace | 查找/替换 | 検索/置換 | MainWindow.BootstrapAndMenus.cpp |
| `menu.format_chart` | Format Chart | 谱面整理 | 譜面を整形 | MainWindow.BootstrapAndMenus.cpp |
| `menu.net_batch_download` | Net Batch Download... | Net 批量下载... | Net 一括ダウンロード... | MainWindow.BootstrapAndMenus.cpp |
| `menu.official_chart_mirror` | Official Chart Mirror | 官谱镜像站 | 公式譜面ミラー | MainWindow.BootstrapAndMenus.cpp, MainWindow.FrameBootstrap.cpp |
| `menu.prepend_pv_black_screen` | Prepend PV Black Screen... | 视频开头黑幕处理 | 動画の先頭に黒画面を追加 | MainWindow.BootstrapAndMenus.cpp |
| `menu.prepend_track_silence` | Prepend Track Silence... | 音频开头静音处理 | オーディオの先頭に無音を追加 | MainWindow.BootstrapAndMenus.cpp |
| `menu.preview_mode_chart_review` | Preview Mode: Chart Review | 预览模式：谱面确认 | プレビューモード：譜面確認 | MainWindow.BootstrapAndMenus.cpp |
| `menu.preview_mode_muri_check` | Preview Mode: Muri Check | 预览模式：无理检测 | プレビューモード：無理チェック | MainWindow.BootstrapAndMenus.cpp |
| `menu.sample_rate` | Sample Rate... | 采样率转换 | サンプルレート変換 | MainWindow.BootstrapAndMenus.cpp |
| `menu.swap_side_panels` | Swap Side Panels | 左右面板互换 | 左右パネルを入れ替え | MainWindow.BootstrapAndMenus.cpp |
| `menu.tap_on_slide_threshold` | Tap-On-Slide Threshold... | 撞尾阈值... | 末尾衝突しきい値... | MainWindow.BootstrapAndMenus.cpp |

### `metadata` (29)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `metadata.choose_an_mp3_and_pull` | Choose an MP3 and pull the title from its ID3 tag. | 选择一个 MP3，从它的 ID3 标签里读取标题。 | MP3 を選び、その ID3 タグからタイトルを読み込みます。 | MainWindow.FrameBootstrap.cpp |
| `metadata.choose_an_mp3_and_pull_2` | Choose an MP3 and pull the artist from its ID3 tag. | 选择一个 MP3，从它的 ID3 标签里读取曲师。 | MP3 を選び、その ID3 タグからアーティストを読み込みます。 | MainWindow.FrameBootstrap.cpp |
| `metadata.choose_an_mp3_and_write` | Choose an MP3 and write its embedded cover artwork as bg.jpg next to the chart. | 选择一个 MP3，把它内嵌的封面图写到当前谱面目录的 bg.jpg。 | MP3 を選び、その埋め込みカバー画像を現在の譜面フォルダーの bg.jpg に書き出します。 | MainWindow.FrameBootstrap.cpp |
| `metadata.click_to_open_the_muri` | Click to open the Muri tab | 点击跳转到「无理」选项卡 | クリックして「無理」タブを開く | MainWindow.FrameBootstrap.cpp |
| `metadata.click_to_open_the_syntax` | Click to open the Syntax tab | 点击跳转到「语法」选项卡 | クリックして「構文」タブを開く | MainWindow.FrameBootstrap.cpp |
| `metadata.close` | Close | 关闭查找栏 | 検索バーを閉じる | MainWindow.FrameBootstrap.cpp |
| `metadata.copy_area` | Copy area | 复制区 | コピー範囲 | MainWindow.FrameBootstrap.cpp |
| `metadata.copy_area_2` | Copy Area | 复制区 | コピー範囲 | MainWindow.FrameBootstrap.cpp |
| `metadata.delete` | Delete | 删除 | 削除 | MainWindow.FrameBootstrap.cpp, CoverLayerListPanel.cpp, CoverStudioWindow.cpp |
| `metadata.delete_1` | Delete %1 | 删除 %1 | %1 を削除 | MainWindow.FrameBootstrap.cpp |
| `metadata.edit_e` | Edit(&E) | 编辑(&E) | 編集(&E) | MainWindow.FrameBootstrap.cpp |
| `metadata.find` | Find | 查找 | 検索 | MainWindow.FrameBootstrap.cpp |
| `metadata.find_next` | Find Next | 查找下一个 | 次を検索 | MainWindow.FrameBootstrap.cpp |
| `metadata.find_previous` | Find Previous | 查找上一个 | 前を検索 | MainWindow.FrameBootstrap.cpp |
| `metadata.full_copy_area` | Full Copy Area | 完整复制区 | コピー範囲全体 | MainWindow.FrameBootstrap.cpp |
| `metadata.insert_bookmark` | Insert Bookmark | 插入书签 | ブックマークを挿入 | MainWindow.FrameBootstrap.cpp |
| `metadata.jump_to_timeline_position` | Jump to Timeline Position | 跳到时间轴位置 | タイムライン位置へジャンプ | MainWindow.FrameBootstrap.cpp |
| `metadata.ln_1_col_1` | Ln 1, Col 1 | 1行 1列 | 1 行 1 列 | MainWindow.FrameBootstrap.cpp |
| `metadata.manage_per_difficulty_designers` | Manage per-difficulty designers | 管理多个难度名义 | 難易度ごとの作者を管理 | MainWindow.FrameBootstrap.cpp |
| `metadata.muri` | Muri | 无理 | 無理 | MainWindow.FrameBootstrap.cpp, MainWindow.BottomTabsHost.cpp, QuickShellController.cpp |
| `metadata.open_the_latency_settings_page` | Open the Latency Settings page: adjust BPM/Offset and audition for calibration. | 打开延迟设置页：调整 BPM/Offset，并通过试听校准。 | 遅延設定ページを開く：BPM/Offset を調整し、試聴で校正します。 | MainWindow.FrameBootstrap.cpp |
| `metadata.preview_p` | Preview(&P) | 预览(&P) | プレビュー(&P) | MainWindow.FrameBootstrap.cpp |
| `metadata.read_from_mp3` | Read from MP3 | 从 MP3 读取 | MP3 から読み込む | MainWindow.FrameBootstrap.cpp |
| `metadata.rename` | Rename | 重命名 | 名前を変更 | MainWindow.FrameBootstrap.cpp, CoverStudioWindow.cpp |
| `metadata.rename_bookmark` | Rename Bookmark | 重命名书签 | ブックマークの名前を変更 | MainWindow.FrameBootstrap.cpp |
| `metadata.replace` | Replace | 替换 | 置換 | MainWindow.FrameBootstrap.cpp |
| `metadata.replace_all` | Replace All | 全部替换 | すべて置換 | MainWindow.FrameBootstrap.cpp |
| `metadata.show_in_sidebar` | Show in Sidebar | 在侧边栏显示 | サイドバーに表示 | MainWindow.FrameBootstrap.cpp |
| `metadata.syntax` | Syntax | 语法 | 構文 | MainWindow.FrameBootstrap.cpp, MainWindow.BottomTabsHost.cpp, QuickShellController.cpp |

### `net` (66)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `net.also_create_zip_after_success` | Also create ZIP after success | 成功后额外生成 ZIP |  | NetBatchDownloadDialog.cpp |
| `net.artist` | Artist | 曲师 | アーティスト | NetBatchDownloadDialog.cpp |
| `net.background_download_thread_started` | Background download thread started. | 后台下载线程已启动。 |  | NetBatchDownloadWorker.cpp |
| `net.browse` | Browse... | 浏览... | 参照... | NetBatchDownloadDialog.cpp, VideoExportDialog.cpp |
| `net.cancel_download` | Cancel Download | 取消下载 |  | NetBatchDownloadDialog.cpp |
| `net.canceling` | Canceling... | 正在取消... |  | NetBatchDownloadDialog.cpp |
| `net.chart_complete_1_2` | Chart complete: %1 -> %2 | 谱面完成：%1 -> %2 |  | NetBatchDownloadWorker.cpp |
| `net.chart_speed_summary_1_total` | Chart speed summary: %1 total=%2 bytes network=%3ms avg=%4 slowest=%5/%6ms | 谱面速度汇总：%1 total=%2 bytes network=%3ms avg=%4 slowest=%5/%6ms |  | NetBatchDownloadWorker.cpp |
| `net.choose_output_directory` | Choose Output Directory | 选择输出目录 |  | NetBatchDownloadDialog.cpp |
| `net.clear_selection` | Clear Selection | 取消全选 |  | NetBatchDownloadDialog.cpp |
| `net.could_not_create_chart_folder` | Could not create chart folder. | 无法创建谱面文件夹。 |  | NetBatchDownloadWorker.cpp |
| `net.designer` | Designer | 谱师 | 譜面作者 | NetBatchDownloadDialog.cpp |
| `net.done_folder` | Done (folder) | 完成（文件夹） |  | NetBatchDownloadWorker.cpp |
| `net.done_folder_zip` | Done (folder + ZIP) | 完成（文件夹 + ZIP） |  | NetBatchDownloadWorker.cpp |
| `net.download_canceled` | Download canceled. | 下载已取消。 |  | NetBatchDownloadDialog.cpp |
| `net.download_complete_1_succeeded_2` | Download complete: %1 succeeded, %2 failed. | 下载完成：成功 %1，失败 %2。 |  | NetBatchDownloadDialog.cpp |
| `net.download_resource_chart_1_resource` | Download resource: chart=%1 resource=%2 attempt=%3 | 下载资源：chart=%1 resource=%2 attempt=%3 |  | NetBatchDownloadWorker.cpp |
| `net.download_selected` | Download Selected | 下载选中 |  | NetBatchDownloadDialog.cpp |
| `net.downloading` | Downloading... | 下载中... |  | NetBatchDownloadWorker.cpp |
| `net.downloading_1` | Downloading: %1 | 正在下载：%1 |  | NetBatchDownloadWorker.cpp |
| `net.downloading_1_2` | Downloading: %1 | 下载中：%1 |  | NetBatchDownloadWorker.cpp |
| `net.end` | End | 结束 |  | NetBatchDownloadDialog.cpp |
| `net.enter_a_user_id_or` | Enter a user ID or tag, choose a date range, then query. | 输入用户 ID 或 Tag，再选择日期范围查询。 |  | NetBatchDownloadDialog.cpp |
| `net.failed_1` | Failed: %1 | 失败：%1 |  | NetBatchDownloadWorker.cpp |
| `net.found_1_chart_s_from` | Found %1 chart(s) from %2 returned chart(s). | 找到 %1 个谱面（查询返回 %2 个）。 |  | NetBatchDownloadDialog.cpp |
| `net.fuzzy_case_insensitive_match` | Fuzzy case-insensitive match | 模糊大小写匹配 |  | NetBatchDownloadDialog.cpp |
| `net.hide_log` | Hide Log | 隐藏日志 |  | NetBatchDownloadDialog.cpp |
| `net.levels` | Levels | 等级 |  | NetBatchDownloadDialog.cpp |
| `net.net_batch_download` | Net Batch Download | Net 批量下载 |  | NetBatchDownloadDialog.cpp |
| `net.no_charts_are_selected` | No charts are selected. | 没有选中的谱面。 |  | NetBatchDownloadDialog.cpp |
| `net.not_selected` | Not selected | 未选中 |  | NetBatchDownloadWorker.cpp |
| `net.output_directory` | Output Directory | 输出目录 |  | NetBatchDownloadDialog.cpp |
| `net.package_failed_1` | Package failed: %1 | 打包失败：%1 |  | NetBatchDownloadWorker.cpp |
| `net.packaging_zip` | Packaging ZIP... | 正在打包 ZIP... |  | NetBatchDownloadWorker.cpp |
| `net.paused` | Paused | 已暂停 |  | NetBatchDownloadWorker.cpp |
| `net.pending` | Pending | 待下载 |  | NetBatchDownloadDialog.cpp |
| `net.please_choose_a_valid_output` | Please choose a valid output directory. | 请选择有效的输出目录。 |  | NetBatchDownloadDialog.cpp |
| `net.please_enter_a_user_id` | Please enter a user ID, tag, or song title. | 请输入用户 ID、Tag 或歌曲名。 |  | NetBatchDownloadDialog.cpp |
| `net.query` | Query | 查询 |  | NetBatchDownloadDialog.cpp |
| `net.query_and_download_diagnostics_will` | Query and download diagnostics will appear here. | 查询和下载诊断日志会显示在这里。 |  | NetBatchDownloadDialog.cpp |
| `net.query_complete_1_ms_api` | Query complete (%1 ms): API returned %2, date filter kept %3, local ID/tag/title filter kept %4. | 查询完成（%1 ms）：接口返回 %2，日期筛选后 %3，本地 ID/Tag/歌曲名筛选后 %4。 |  | NetBatchDownloadDialog.cpp |
| `net.query_failed` | Query failed. | 查询失败。 |  | NetBatchDownloadDialog.cpp |
| `net.query_failed_1_ms_2` | Query failed (%1 ms): %2 | 查询失败（%1 ms）：%2 |  | NetBatchDownloadDialog.cpp |
| `net.querying_net` | Querying Net... | 正在查询 Net... |  | NetBatchDownloadDialog.cpp |
| `net.queue_canceled_1_succeeded_2` | Queue canceled: %1 succeeded, %2 failed. | 队列取消：成功 %1，失败 %2。 |  | NetBatchDownloadWorker.cpp |
| `net.queue_complete_1_succeeded_2` | Queue complete: %1 succeeded, %2 failed, network total %3 bytes, average %4. | 队列完成：成功 %1，失败 %2，网络总量 %3 bytes，平均 %4。 |  | NetBatchDownloadWorker.cpp |
| `net.queue_paused_net_cloudflare_blocked` | Queue paused: Net/Cloudflare blocked a request. | 队列已暂停：Net/Cloudflare 阻断了请求。 |  | NetBatchDownloadDialog.cpp |
| `net.queue_paused_net_cloudflare_blocked_2` | Queue paused: Net/Cloudflare blocked a request. | 队列暂停：Net/Cloudflare 阻断了请求。 |  | NetBatchDownloadWorker.cpp |
| `net.resource_download_failed` | Resource download failed. | 资源下载失败。 |  | NetBatchDownloadWorker.cpp |
| `net.resource_result_1_http_2` | Resource result: %1 HTTP=%2 bytes=%3 elapsed=%4ms speed=%5 | 资源结果：%1 HTTP=%2 bytes=%3 elapsed=%4ms speed=%5 |  | NetBatchDownloadWorker.cpp |
| `net.retrying_1` | Retrying: %1 | 重试：%1 |  | NetBatchDownloadWorker.cpp |
| `net.select` | Select | 选择 |  | NetBatchDownloadDialog.cpp |
| `net.select_all` | Select All | 全选 |  | NetBatchDownloadDialog.cpp |
| `net.show_log` | Show Log | 查看日志 |  | NetBatchDownloadDialog.cpp |
| `net.show_log_2` | Show Log * | 查看日志 * |  | NetBatchDownloadDialog.cpp |
| `net.skip_existing_file_1_2` | Skip existing file: %1 (%2 bytes) | 跳过已有文件：%1（%2 bytes） |  | NetBatchDownloadWorker.cpp |
| `net.song_title` | Song Title | 歌曲名 |  | NetBatchDownloadDialog.cpp |
| `net.start` | Start | 开始 |  | NetBatchDownloadDialog.cpp |
| `net.start_chart_1_2` | Start chart: %1 [%2] | 开始谱面：%1 [%2] |  | NetBatchDownloadWorker.cpp |
| `net.start_download_queue_selected_1` | Start download queue: selected=%1, output=%2, extra ZIP=%3 | 开始下载队列：选中 %1，输出 %2，额外 ZIP=%3 |  | NetBatchDownloadDialog.cpp |
| `net.start_query_user_1_tag` | Start query: user=%1, tag=%2, title=%3, dates=%4..%5, fuzzy case=%6 | 开始查询：用户=%1，tag=%2，歌曲名=%3，日期=%4..%5，模糊大小写=%6 |  | NetBatchDownloadDialog.cpp |
| `net.status` | Status | 状态 |  | NetBatchDownloadDialog.cpp |
| `net.title` | Title | 标题 | タイトル | NetBatchDownloadDialog.cpp |
| `net.uploaded` | Uploaded | 上传时间 |  | NetBatchDownloadDialog.cpp |
| `net.user_id` | User ID | 用户 ID |  | NetBatchDownloadDialog.cpp |
| `net.zip_package_1_2_3` | ZIP package: %1 -> %2 (%3 ms) | ZIP 打包：%1 -> %2（%3 ms） |  | NetBatchDownloadWorker.cpp |

### `preferences` (11)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `preferences.auto_closes_brackets_suggests_durations` | Auto-closes brackets, suggests durations/BPMs inside them, and offers [8:1]-style hold tokens after typing 'h'. | 自动补全括号、给出括号/时值建议，并在输入 h 时提示 [8:1] 等 hold 时值。 | 括弧を自動補完し、括弧/音価の候補を表示します。h を入力すると [8:1] のような hold 音価を提示します。 | MainWindow.PreferencesDialog.cpp |
| `preferences.auto_completion` | Auto-completion | 自动补全 | 自動補完 | MainWindow.PreferencesDialog.cpp |
| `preferences.chinese_input` | Chinese input | 中文输入 | 中国語入力 | MainWindow.PreferencesDialog.cpp |
| `preferences.conflicts_with_1` | Conflicts with "%1" | 与「%1」重复 | 「%1」と重複しています | MainWindow.PreferencesDialog.cpp |
| `preferences.disable_ime` | Disable IME | 禁止中文输入法 | IME を無効化 | MainWindow.PreferencesDialog.cpp |
| `preferences.filter_full_width_chars` | Filter full-width chars | 仅过滤全角字符 | 全角文字のみ除外 | MainWindow.PreferencesDialog.cpp |
| `preferences.hides_muri_from_the_editor` | Hides muri from the editor header and timeline dots. Saved in the current chart folder's .miacode data. | 开启后不在编辑器标题栏和时间轴小点中提示无理。设置保存到当前谱面文件夹的 .miacode。 | 有効にするとエディタのタイトルバーとタイムラインの点に無理を表示しません。設定は現在の譜面フォルダーの .miacode に保存されます。 | MainWindow.PreferencesDialog.cpp |
| `preferences.ignore_muri_issue_prompts` | Ignore muri issue prompts | 忽略无理报错提示 | 無理の警告表示を無視 | MainWindow.PreferencesDialog.cpp |
| `preferences.off` | Off | 关闭 | オフ | MainWindow.PreferencesDialog.cpp |
| `preferences.on` | On | 开启 | オン | MainWindow.PreferencesDialog.cpp |
| `preferences.the_field_next_to_lv` | The field next to Lv in the chart header: the &first offset or this difficulty's &des_N designer. | 谱面编辑页顶部 Lv 旁边显示的字段：偏移（&first）或当前难度的谱师（&des_N）。 | 譜面編集ページ上部の Lv の横に表示するフィールド：オフセット（&first）または現在の難易度の作者（&des_N）。 | MainWindow.PreferencesDialog.cpp |

### `shell` (3)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `shell.follow_code` | Follow Code | 代码跟随 | コード追従 | QuickShellController.cpp |
| `shell.timeline_sync` | Timeline Sync | 时轴同步 | タイムライン同期 | QuickShellController.cpp |
| `shell.view_lock` | View Lock | 光标居中 | ビューロック | QuickShellController.cpp |

### `timeline` (1)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `timeline.playback_speed` | Playback Speed | 当前倍速 | 再生速度 | MainWindow.TimelinePlayback.cpp |

### `track_metadata` (16)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `track_metadata.artist` | artist | 曲师 | アーティスト | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.bg_jpg_already_exists_overwrite` | bg.jpg already exists. Overwrite? | bg.jpg 已经存在，是否覆盖？ | bg.jpg は既に存在します。上書きしますか？ | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.extract_cover_to_bg_jpg` | Extract Cover to bg.jpg | 提取封面为 bg.jpg | カバーを bg.jpg に抽出 | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.failed_to_decode_embedded_cover` | Failed to decode embedded cover (MIME=%1). | 内嵌封面解码失败（MIME=%1）。 | 埋め込みカバーのデコードに失敗しました（MIME=%1）。 | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.failed_to_write_bg_jpg` | Failed to write bg.jpg. | 写入 bg.jpg 失败。 | bg.jpg の書き込みに失敗しました。 | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.loaded_artist_from_mp3` | Loaded artist from MP3. | 已从 MP3 读取曲师。 | MP3 からアーティストを読み込みました。 | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.loaded_title_from_mp3` | Loaded title from MP3. | 已从 MP3 读取标题。 | MP3 からタイトルを読み込みました。 | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.mp3_audio_mp3_all_files` | MP3 audio (*.mp3);;All files (*.*) | MP3 音频 (*.mp3);;所有文件 (*.*) | MP3 オーディオ (*.mp3);;すべてのファイル (*.*) | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.no_id3v2_tag_was_found` | No ID3v2 tag was found in the selected MP3. | 没能在所选 MP3 中读取到 ID3v2 标签。 | 選択した MP3 から ID3v2 タグを読み取れませんでした。 | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.overwrote_bg_jpg_with_embedded` | Overwrote bg.jpg with embedded cover from the selected MP3. | 已覆盖 bg.jpg（来源：所选 MP3 内嵌封面）。 | bg.jpg を上書きしました（選択した MP3 の埋め込みカバーから）。 | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.read_artist_from_mp3` | Read Artist from MP3 | 从 MP3 读取曲师 | MP3 からアーティストを読み込む | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.read_title_from_mp3` | Read Title from MP3 | 从 MP3 读取标题 | MP3 からタイトルを読み込む | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.the_selected_mp3_has_no` | The selected MP3 has no embedded cover artwork. | 所选 MP3 中没有内嵌的封面图。 | 選択した MP3 に埋め込みカバー画像がありません。 | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.the_selected_mp3_s_id3` | The selected MP3's ID3 tag carries no %1. | 所选 MP3 的 ID3 标签里没有%1信息。 | 選択した MP3 の ID3 タグに%1情報がありません。 | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.title` | title | 标题 | タイトル | MainWindow.Dialogs.TrackMetadata.cpp |
| `track_metadata.wrote_bg_jpg_from_the` | Wrote bg.jpg from the selected MP3's embedded cover. | 已生成 bg.jpg（来源：所选 MP3 内嵌封面）。 | bg.jpg を生成しました（選択した MP3 の埋め込みカバーから）。 | MainWindow.Dialogs.TrackMetadata.cpp |

### `ui` (1)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `ui.click_to_type_a_value` | Click to type a value | 点击可输入数值 | クリックして数値を入力 | EditableValueLabel.cpp |

### `validation` (11)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `validation.adjust_the_static_tap_on` | Adjust the static Tap-On-Slide reference threshold. | 调整静态“撞尾无理”参考检查阈值。 | 静的な「末尾衝突無理」の参照チェックしきい値を調整します。 | MainWindow.ValidationRender.cpp |
| `validation.click_an_icon_to_jump` | Click an icon to jump to its tab | 点击图标可跳转到对应选项卡 | アイコンをクリックして対応するタブへ移動 | MainWindow.ValidationFlow.cpp |
| `validation.copy_info` | Copy Info | 复制信息 | 情報をコピー | MainWindow.ValidationRuntime.cpp |
| `validation.ignore_this_issue_type` | Ignore This Issue Type | 忽视该类型提示 | この種類の警告を無視 | MainWindow.ValidationRuntime.cpp |
| `validation.issue_info_copied` | Issue info copied. | 已复制信息。 | 情報をコピーしました。 | MainWindow.ValidationRuntime.cpp |
| `validation.jump_to_source` | Jump to Source | 跳转到源 | ソースへジャンプ | MainWindow.ValidationRuntime.cpp |
| `validation.no_muri_issues_detected` | No muri issues detected. | 未检测到无理。 | 無理は検出されませんでした。 | MainWindow.ValidationRuntime.cpp |
| `validation.no_syntax_errors_detected` | No syntax errors detected. | 未检测到语法错误。 | 構文エラーは検出されませんでした。 | MainWindow.ValidationRuntime.cpp |
| `validation.stop_ignoring_this_issue_type` | Stop Ignoring This Issue Type | 取消忽视该类型提示 | この種類の警告表示を再開 | MainWindow.ValidationRuntime.cpp |
| `validation.tap_on_slide_threshold` | Tap-On-Slide Threshold | 撞尾阈值 | 末尾衝突しきい値 | MainWindow.ValidationRender.cpp |
| `validation.tap_on_slide_threshold_set` | Tap-On-Slide threshold set to %1 ms. | 撞尾阈值已更新为 %1 ms。 | 末尾衝突しきい値を %1 ms に更新しました。 | MainWindow.ValidationRender.cpp |

### `video_export` (21)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `video_export.add_intro` | Add intro | 添加片头 | イントロを追加 | BatchVideoExportDialog.cpp, VideoExportDialog.cpp |
| `video_export.cancel_export` | Cancel Export | 取消导出 | 出力を取り消す | VideoExportDialog.ExportFlow.cpp |
| `video_export.current_export_range_1_2` | Current export range: [%1, %2], %3 s total. | 当前导出区间：[%1, %2]，共 %3 秒。 | 現在の出力範囲：[%1, %2]、合計 %3 秒。 | VideoExportDialog.cpp |
| `video_export.enable_clock_count_1` | Enable clock_count (%1) | 启用 clock_count (%1) | clock_count を有効化 (%1) | VideoExportDialog.cpp |
| `video_export.export_range` | Export Range | 导出区间 | 出力範囲 | VideoExportDialog.cpp |
| `video_export.export_range_is_empty` | Export range is empty. | 导出区间为空。 | 出力範囲が空です。 | VideoExportDialog.ExportFlow.cpp |
| `video_export.export_start_is_out_of` | Export start is out of range. | 导出起始时间超出范围。 | 出力開始時間が範囲外です。 | VideoExportDialog.ExportFlow.cpp |
| `video_export.export_video` | Export Video | 导出视频 | 動画を出力 | VideoExportDialog.ExportFlow.cpp |
| `video_export.gameplay` | Gameplay | 游戏 | ゲーム | VideoExportDialog.cpp |
| `video_export.intro` | Intro | 片头 | イントロ | VideoExportDialog.cpp |
| `video_export.layout_size` | Layout Size | Layout整图大小 | Layout 全体サイズ | VideoExportDialog.cpp |
| `video_export.output` | Output | 输出 | 出力 | VideoExportDialog.cpp |
| `video_export.output_directory_does_not_exist` | Output directory does not exist. | 输出目录不存在。 | 出力フォルダーが存在しません。 | VideoExportDialog.ExportFlow.cpp |
| `video_export.please_choose_an_output_path` | Please choose an output path. | 请先选择输出路径。 | 先に出力先を選択してください。 | VideoExportDialog.ExportFlow.cpp |
| `video_export.prepend_the_maimai_track_start` | Prepend the maimai track-start intro to each export. | 在每个视频开头加入 maimai 风格片头（批量导出整谱）。 | 各動画の先頭に maimai 風のイントロを追加します（全譜面の一括出力）。 | BatchVideoExportDialog.cpp |
| `video_export.resolution_is_invalid` | Resolution is invalid. | 分辨率无效。 | 解像度が無効です。 | VideoExportDialog.ExportFlow.cpp |
| `video_export.show_bottom_left_timestamp` | Show bottom-left timestamp | 显示左下角时间戳 | 左下にタイムスタンプを表示 | VideoExportDialog.cpp |
| `video_export.skin` | Skin | 皮肤 | スキン | VideoExportDialog.cpp |
| `video_export.smooth_brightness` | Smooth brightness | 平滑亮度 | 明るさを滑らかに | VideoExportDialog.cpp |
| `video_export.start_export` | Start Export | 开始导出 | 出力を開始 | VideoExportDialog.ExportFlow.cpp |
| `video_export.video` | Video | 视频 | 動画 | VideoExportDialog.cpp |

### `window` (4)

| proposed key | en | zh | ja | source |
|---|---|---|---|---|
| `window.collapse_left_sidebar` | Collapse left sidebar | 折叠左侧字段栏 | 左のフィールド欄を折りたたむ | MainWindow.WindowShell.cpp |
| `window.expand_left_sidebar` | Expand left sidebar | 展开左侧字段栏 | 左のフィールド欄を展開 | MainWindow.WindowShell.cpp |
| `window.replaced_1_occurrence_s` | Replaced %1 occurrence(s). | 已替换 %1 处。 | %1 か所を置換しました。 | MainWindow.WindowInteraction.cpp |
| `window.timeline` | Timeline | 时间轴 | タイムライン | MainWindow.BottomTabsHost.cpp, QuickShellController.cpp |
