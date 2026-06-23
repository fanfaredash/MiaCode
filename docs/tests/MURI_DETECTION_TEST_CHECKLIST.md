# 无理检测测试清单

依据：`MURI_DETECTION_SPEC.md`

目标：确认当前无理检测在运行时分析、静态参考、面板锚点、点击跳转、文案、叠键去重和多押折叠上的行为与规格一致。

## 0. 测试准备

- [ ] 已完成 `cmake --build build_codex --config Debug --target MiaCode muri_spec miacode_muri_dump`
- [ ] 已运行 `build_codex\\Debug\\muri_spec.exe`
- [ ] `muri_spec` 输出为 `Muri spec passed.`
- [ ] 可以在应用内打开无理面板并点击条目跳转

## 共享时间常量速查

- 判定时基：`180 TPS`
- 抬手延迟：`3 / 180 = 16.7 ms`
- 额外 pad down 延迟：`9 / 180 = 50.0 ms`
- tap critical：`3 / 180 = 16.7 ms`
- tap available：`27 / 180 = 150.0 ms`
- touch critical / available：`27 / 180 = 150.0 ms`
- slide critical：`42 / 180 = 233.3 ms`
- slide available：`24 h = 86400.0 s`
- slide leading：`15 / 180 = 83.3 ms`
- tap-on-slide 起始阈值：`1 / 180 = 5.6 ms`
- touch-on-slide 阈值：`24 / 180 = 133.3 ms`
- slide-too-fast 阈值：`1 / 180 = 5.6 ms`
- SlideHeadTap 无启动 tap 的 Warning 阈值：`50.0 ms`
- SlideHeadTap 晚窗口 Warning 阈值：`150.0 ms`

## 1. 基础类型覆盖

- [ ] `SlideTooFast` 能作为运行时诊断出现
- [ ] `SlideHeadTap` 能同时出现在运行时诊断和静态参考中
- [ ] `TapOnSlide` 能同时出现在运行时诊断和静态参考中
- [ ] `Overlap` 能同时出现在运行时诊断和静态参考中
- [ ] `MultiTouch` 只会出现在运行时诊断中

## 2. 锚点、行列号与点击跳转

使用样例：

```simai
(240){16}
8>3[4:1],,,,,
8,
E
```

- [ ] 面板只显示 1 条 `SlideHeadTap`
- [ ] 面板显示的行列号落在 `slide 8>3` 所在位置，而不是后续 `8`
- [ ] 点击该条目后，编辑器跳转到 slide 所在的精确列号，而不是只到正确行
- [ ] Timeline / 相关定位使用的是首个涉事物件的时间
- [ ] 运行时诊断与静态参考不会在面板中重复显示两条同类条目

## 3. 详情文案

- [ ] `SlideHeadTap` 文案会写出受影响目标为 `tap N` / `hold N` / `star N`
- [ ] `TapOnSlide` 文案会写出受影响目标为 `tap N` / `hold N` / `star N`
- [ ] 同头星目标显示为 `star N`，不会退化成裸 `tap`
- [ ] 带保护的 tap 目标显示为 `protected tap Nx`，不会退回 `EX` 一类旧称呼
- [ ] 带保护的 hold 目标显示为 `protected hold Nxh`
- [ ] 带保护的头星目标显示为 `protected star Nx`
- [ ] 中文文案与英文原始 detail 语义一致

## 4. 叠键去重

使用样例：

```simai
(240){16}
111,
E
```

- [ ] 运行时诊断中 `Overlap` 数量为 1
- [ ] 静态参考中 `Overlap` 数量为 1
- [ ] 面板最终可见 `Overlap` 数量为 1
- [ ] 不会因为运行时与静态参考各一条而在面板里重复显示两条

## 5. 叠键折叠后不再误报多押

使用样例：

```simai
(240){16}
118,
E
```

- [ ] 面板最终只显示 1 条 `Overlap`
- [ ] 不会出现 `MultiTouch`
- [ ] `muri_spec` 中该样例通过

## 6. 真实多押仍然保留

使用样例：

```simai
(240){16}
123,
E
```

- [ ] 不会出现 `Overlap`
- [ ] 会出现 1 条 `MultiTouch`
- [ ] 多押条目的锚点落在最早贡献动作的位置

## 7. 告警级别边界

- [ ] `SlideTooFast` 始终显示为 `Muri`
- [ ] `Overlap` 始终显示为 `Muri`
- [ ] `SlideHeadTap` 在 `0 < gap <= 50 ms` 且无启动时 tap 时可降为 `Warning`
- [ ] `SlideHeadTap` 在 `gap >= 150 ms` 时可降为 `Warning`
- [ ] `SlideHeadTap` 的受影响目标带保护时可降为 `Warning`
- [ ] `TapOnSlide` 在 `150 ms < gap <= staticTapOnSlideThresholdSeconds` 时可降为 `Warning`
- [ ] `TapOnSlide` 的受影响目标带保护时可降为 `Warning`
- [ ] 因“受影响目标带保护”而降级为 `Warning` 时，详情文案仍保留 `protected ...` 与 `x/xh` 记法
- [ ] `MultiTouch` 在涉及 touch 且非 touch hand count 不超过 2 时可降为 `Warning`

## 8. 静态阈值

- [ ] 静态 tap-on-slide 阈值默认值为 `200 ms`
- [ ] 运行时分析传入阈值后会被 clamp 到 `150..250 ms`
- [ ] 修改该阈值后，静态参考与运行时对应的 `TapOnSlide` / `SlideHeadTap` 告警级别仍保持一致预期

## 9. slide / wifi 长等待窗口

- [ ] slide / wifi 的 runtime available 窗口当前为 `24 h`
- [ ] 在 `endSecond + 24 h` 之前，未完成 slide / wifi 不会仅因超时而被强制 bad
- [ ] 超过 `endSecond + 24 h` 后，会被强制 bad
- [ ] 对 `(128.6){1}3v1[1:11]/7v5[1:11],` 这类超长 slide，若最终完成并不是被其他物件的最后一拍 pad-down 收尾，则不报 `SlideTooFast`
- [ ] 上述超长 slide 样例不会额外出现 slide `Good` 判定特效

## 10. 面板合并与排序

- [ ] 面板先以运行时诊断为主，再补静态参考
- [ ] 同 kind 且同锚点 `LxCy` 时，静态参考不会重复显示
- [ ] 列表按锚点秒数排序，而不是按实际发生秒数排序
- [ ] 同秒时按 `line / col / kind` 稳定排序

## 11. 头星专项回归

- [ ] 头星目标会参与 `SlideHeadTap`
- [ ] 头星目标会参与 `TapOnSlide`
- [ ] 头星不会额外制造假的 `MultiTouch`
- [ ] 可见面板中的头星文案为 `star N`

## 12. 手工回归建议

- [ ] 准备一张包含 slide head、slide tail、wifi、hold、touch 的综合谱面
- [ ] 逐条核对：
  - [ ] 中文标题是否与类型一致
  - [ ] 详情文案是否和原因一致
  - [ ] 点击跳转是否落到正确行列
  - [ ] 相同秒数的大量叠键不会把列表刷满
  - [ ] 去掉叠键后本不构成多押的情况不会残留 `MultiTouch`

## 13. 已知回归样例

- [ ] 锚点样例：`(240){16} / 8>3[4:1] -> 8`
- [ ] 同头星样例：`(240){16} / 8>4[4:1]/1>5[4:1] -> 1-5[8:1]`
- [ ] 头星撞尾样例：`(193){8} ... 2/6-3[8:1] -> 3-6[8:1]`
- [ ] `Prophesy One UTG` 风格样例在约 `170.5 ms` 时把 `SlideHeadTap` 降为 `Warning`
- [ ] `111` 只保留 1 条 `Overlap`
- [ ] `118` 不残留 `MultiTouch`
- [ ] `123` 仍保留 `MultiTouch`
- [ ] `(128.6){1}3v1[1:11]/7v5[1:11],` 不显示 `SlideTooFast`

## 14. 建议记录模板

- [ ] 样例名称
- [ ] 谱面文本
- [ ] 预期结果
- [ ] 实际结果
- [ ] 是否稳定复现
- [ ] 是否已有 `muri_spec` 覆盖
