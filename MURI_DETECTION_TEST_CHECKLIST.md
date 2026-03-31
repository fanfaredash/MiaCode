# 无理检测测试清单

依据：`MURI_DETECTION_SPEC.md`

目标：确认当前无理检测在运行时分析、静态参考、面板锚点、叠键去重和多押折叠上的行为与规格一致。

## 0. 测试准备

- [ ] 已完成 `cmake --build build_codex --config Debug --target MiaCode muri_spec`
- [ ] 已运行 `build_codex\\Debug\\muri_spec.exe`
- [ ] `muri_spec` 输出为 `Muri spec passed.`
- [ ] 可以在应用内打开无理面板并点击条目跳转

## 1. 基础类型覆盖

- [ ] `SlideTooFast` 能作为运行时诊断出现
- [ ] `SlideHeadTap` 能同时出现在运行时诊断和静态参考中
- [ ] `TapOnSlide` 能同时出现在运行时诊断和静态参考中
- [ ] `Overlap` 能同时出现在运行时诊断和静态参考中
- [ ] `MultiTouch` 只会出现在运行时诊断中

## 2. 锚点与行列号

使用样例：

```simai
(240){16}
8>3[4:1],,,,,
8,
E
```

- [ ] 面板只显示 1 条 `SlideHeadTap`
- [ ] 面板显示的行列号落在 `slide 8>3` 所在位置，而不是后续 `8`
- [ ] 点击该条目后，编辑器跳转到 slide 所在行列
- [ ] Timeline / 相关定位使用的是首个涉事物件的时间
- [ ] 运行时诊断与静态参考不会在面板中重复显示两条同类条目

## 3. 叠键去重

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

## 4. 叠键折叠后不再误报多押

使用样例：

```simai
(240){16}
118,
E
```

- [ ] 面板最终只显示 1 条 `Overlap`
- [ ] 不会出现 `MultiTouch`
- [ ] `muri_spec` 中该样例通过

## 5. 真实多押仍然保留

使用样例：

```simai
(240){16}
123,
E
```

- [ ] 不会出现 `Overlap`
- [ ] 会出现 1 条 `MultiTouch`
- [ ] 多押条目的锚点落在最早贡献动作的位置

## 6. 告警级别边界

- [ ] `SlideTooFast` 始终显示为 `Muri`
- [ ] `Overlap` 始终显示为 `Muri`
- [ ] `SlideHeadTap` 在 `0 < gap <= 50 ms` 且无更强 head 条件时可降为 `Warning`
- [ ] `TapOnSlide` 在 `150 ms < gap <= staticTapOnSlideThresholdSeconds` 时可降为 `Warning`
- [ ] `MultiTouch` 在涉及 touch 且非 touch hand count 不超过 2 时可降为 `Warning`

## 7. 静态阈值

- [ ] 静态 tap-on-slide 阈值默认值为 `200 ms`
- [ ] 运行时分析传入阈值后会被 clamp 到 `150..250 ms`
- [ ] 修改该阈值后，静态参考与运行时对应的 `TapOnSlide` / `SlideHeadTap` 告警级别仍保持一致预期

## 8. 面板合并与排序

- [ ] 面板先以运行时诊断为主，再补静态参考
- [ ] 同 kind 且同锚点 `LxCy` 时，静态参考不会重复显示
- [ ] 列表按锚点秒数排序，而不是按实际发生秒数排序
- [ ] 同秒时按 `line / col / kind` 稳定排序

## 9. 手工回归建议

- [ ] 准备一张包含 slide head、slide tail、wifi、hold、touch 的综合谱面
- [ ] 逐条核对：
  - [ ] 中文标题是否与类型一致
  - [ ] 详情文案是否和原因一致
  - [ ] 点击跳转是否落到正确行列
  - [ ] 相同秒数的大量叠键不会把列表刷满
  - [ ] 去掉叠键后本不构成多押的情况不会残留 `MultiTouch`

## 10. 建议记录模板

- [ ] 样例名称
- [ ] 谱面文本
- [ ] 预期结果
- [ ] 实际结果
- [ ] 是否稳定复现
- [ ] 是否已有 `muri_spec` 覆盖
