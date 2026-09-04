状态：实施检查清单草案

# Timeline Qt Quick + GPU 一致性检查清单

在任何 Timeline Quick/GPU 路径被提升到实验开关之外之前，请先使用这份清单逐项验证。

## 1. 语义一致性

- [ ] `L`、`R` 和 `C` 的含义与当前 QWidget Timeline 完全一致。
- [ ] playback-entry marker 仍会在 preview start/resume 时更新。
- [ ] playhead marker 仍然跟随 preview runtime `R`。
- [ ] cursor marker 仍然跟随 editor/timeline anchor `C`。
- [ ] 普通 editor cursor movement 仍然只更新 `C`，不会强制 preview seek。
- [ ] editor text edits 仍然先命中 fast Timeline refresh path。
- [ ] slow refresh 仍然拥有 parser note-marker publication 和 analysis scheduling 的职责。
- [ ] Timeline header click 仍然执行当前按时间导航的路径。
- [ ] Timeline body 中的 `Ctrl/Meta + click` 仍与 header-click 语义一致。
- [ ] Timeline drag scrub 仍然编辑 runtime `R`，而不是 editor `C`。
- [ ] Timeline wheel pan 仍然表现为 scrub/pan，而不是 zoom。
- [ ] Timeline `Alt+wheel` 仍然使用当前 preset zoom steps。
- [ ] Timeline `Space` 仍然切换 preview playback。
- [ ] Timeline `Left/Right` 仍然移动 Timeline viewport，而不是像 slider 一样 stepping preview。
- [ ] Follow-toggle 语义仍与当前路径一致。
- [ ] 暂停态 follow 仍然只绘制 decoration，不移动 editor caret。
- [ ] 播放态 follow 仍然将 editor caret/selection 绑定到 preview-follow span。

## 2. 跨区联动一致性

- [ ] Timeline 到 preview 的 seek commands 仍通过当前 `MainWindow` orchestration 流转。
- [ ] Preview playback start/resume 仍会更新 Timeline 的 `L`、`R` 和 upper-bound state。
- [ ] Preview pause/stop/end 仍然让 Timeline 进入与当前路径相同的暂停状态。
- [ ] Preview slider press/move/release 仍然先更新 preview，再通过 preview position 驱动 Timeline。
- [ ] Editor `Ctrl+LeftClick` 仍会在需要时停止 preview，并 seek 到 editor-resolved second。
- [ ] Preview 暂停时，editor cursor sync 仍然更新 Timeline `C`。
- [ ] Validation 与 Muri 的发布仍然留在 Timeline renderer 外部。
- [ ] Paused preview snapshot 的语义仍与当前路径一致。
- [ ] 显式 validation run 仍发布最新 validation snapshot，供 QML syntax/analysis rows 消费。
- [ ] QML Muri row activation 仍可跳转 editor、seek preview，并更新 Timeline markers。

## 3. Quick 底部标签宿主一致性

- [ ] 底部标签集群已经运行在 Quick 中，并由共享 state 将 Timeline、Validation 和 Muri 作为一个协调单元承载。
- [ ] 进入 chart mode 时，仍会强制 Timeline tab current。
- [ ] 切换到 metadata 或 welcome mode 时，仍会隐藏整个 bottom-tabs 集群。
- [ ] Validation tab 的可见性仍与 chart-page 状态保持正确联动。
- [ ] Validation/Muri rows 由 QML model 从 snapshot 重建，不依赖 tab 切换或尺寸变化时的 Widget relayout。
- [ ] Quick tab selection 与 Quick focus routing 通过 coordinator 的 state 路径保持可见行为。

## 4. 渲染一致性

- [ ] Grid line 位置与当前 Timeline 一致。
- [ ] Header line labels 的 line-start anchors、间距、折叠规则和 marker triangles 与当前路径一致。
- [ ] Waveform 形状与当前选中的 visible-range LOD 一致。
- [ ] Note icon selection 与当前 flags 组合一致。
- [ ] Hold 和 touch-hold 的视觉厚度与 cap 行为一致。
- [ ] Slide/wifi track visuals 仍保持当前的 track/head 分离。
- [ ] Note stacking order 与当前 Timeline 以及 preview-mirrored order 一致。
- [ ] Firework tail spans 出现在正确的行和持续时间内。
- [ ] Muri dots 出现在与当前相同的 note 位置上。
- [ ] Playhead、cursor 与 entry markers 的颜色和位置规则与当前一致。
- [ ] Zoom presets 和居中行为与当前 Timeline 一致。

## 5. GPU 策略 / 后端选择验证

- [ ] Preview 和 Timeline 从同一套 render policy 读取配置。
- [ ] 实际 GUI graphics backend 已记录到日志中。
- [ ] GUI 运行时后端报告与 CLI export/worker 后端报告已明确区分。
- [ ] Timeline 默认 inline path 在目标 Quick host 下工作正常。
- [ ] 若存在 Timeline separate-surface 路径，它只作为诊断/回退分支存在，并已按此语义验证。
- [ ] 启动策略日志包含 graphics API、render loop、native-sibling guard，以及按模块划分的 surface 决策。

## 6. 主题与配色验证

- [ ] Timeline 颜色 token 已由单一 owner 管理，例如 `TimelineThemeConfig`。
- [ ] 主题切换时 current tab 不会丢失。
- [ ] 主题切换时 active focus 不会丢失。
- [ ] 主题切换时 playback、follow、selection 和 markers 状态不变。
- [ ] 纯颜色层按照预期走廉价的 material/color revision 更新。
- [ ] 主题烘焙进纹理的缓存只会在必要时重建。
- [ ] Validation 和 Muri 的 Quick tabs 与 Timeline 一样，从同一份 palette/metrics revision stream 更新。

## 7. 焦点、快捷键与模态验证

- [ ] 点击 Timeline 后，Timeline Quick item 会真正获得 active focus。
- [ ] 当 editor text focus 应拥有输入时，Timeline keys 不会错误生效。
- [ ] Preview fullscreen 下的 `F11`、`Esc` 和 `Space` 仍正常工作。
- [ ] Preview slider 的 `Left/Right` held seek 仍正常工作。
- [ ] Timeline `Left/Right` 不会错误触发 preview slider seek。
- [ ] Quick tab navigation 不会在 Timeline tab content 应接管时偷走 Timeline-local keys。
- [ ] Validation/Muri issue-list navigation 不会破坏 preview 或 Timeline shortcuts。
- [ ] Editor undo/redo shortcuts 仍然继续归 editor 所有。
- [ ] 应用重新激活后的 text-focus restore 仍正常工作。
- [ ] Native dialogs 不会让 Quick bottom-tabs cluster 或 preview 落入错误焦点状态。
- [ ] Quick-shell host 的 QAction-owned app shortcuts 转发仍然正常。

## 8. 宿主架构约束

- [ ] 当前测试的实现路径明确标明了自己的 host mode。
- [ ] 实验性的 Quick Timeline 路径不会在未明确切换前悄悄替换 widget Timeline。
- [ ] 目标架构不依赖新的 `QWidget::createWindowContainer()` 风格 Timeline embedding 路径。
- [ ] Quick bottom-tabs cluster 在迁移期间能与 retained-native editor/metadata workspace 共存。
- [ ] Theme、DPI 和 font parity 已在真实宿主路径下验证。

## 9. 退出条件

- [ ] Widget Timeline 在 Quick 路径完全验收前仍保留为语义参考路线。
- [ ] 上述所有条目都已转绿，或者每一个例外都有明确审核过的说明。
- [ ] 没有把业务逻辑 owner 从 `MainWindow` / `TimelineQuickModel` 偷偷挪到临时的 QML state 中。
