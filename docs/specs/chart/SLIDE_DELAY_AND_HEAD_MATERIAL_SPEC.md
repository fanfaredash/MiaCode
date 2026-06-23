# 延迟 Slide、头材质与无头 Slide 规则

本文档整理当前仓库中已经落地的最终规则，用作后续 parser / timeline / preview / export / transform 的统一依据。

## 1. 延迟 Slide

延迟 Slide 用于覆写 slide / wifi 默认的等待时间和滑动时长。

基础语义:

- 等待时间: 从 note 头判定时刻到 slide 真正开始跑轨迹的时间。
- 滑动时长: 从 `slideTraceSecond` 到 `endSecond` 的移动时间。
- 仅影响当前 note，不改全局 BPM / 拍号。

合法语法:

- `[指定BPM#时值:时值]`
- `[指定BPM#秒数]`
- `[等待秒数##滑动秒数]`
- `[等待秒数##指定BPM#时值:时值]`
- `[等待秒数##时值:时值]`
  - 这是保留兼容语法。
  - `##` 左侧显式等待秒数，右侧仍按当前 BPM 解释时值。

约束:

- `#` 表示“等待时间由 BPM 推导，后面描述滑动时长”。
- `##` 表示“左边显式等待，右边描述滑动时长”。
- `#/##` 不能任意混写，只有 `等待秒数##指定BPM#时值:时值` 这一类双层写法合法。
- 适用于所有 slide-like note，包括普通 slide 和 wifi。

## 2. Tap 转 Star 材质: `$` / `$$`

作用对象:

- 仅普通按键 tap。
- 不适用于 hold、slide、wifi、touch、touch_hold。

合法语法:

- `1$`
- `1$$`
- `1$b`
- `1x$$`
- `1$$bx`

规则:

- `$` 将普通 tap 的头部材质切换为 star 材质。
- `$$` 在 star 材质基础上追加双星/旋转效果。
- `b` / `x` 仍然绑定在这个 tap 上，并继承到 star 材质上。
- `h` 与 `$` / `$$` 组合视为语法错误。
- 这组修饰符只改变材质，不改变判定、each 分组、时序。

实现语义:

- `tapUsesStarMaterial = true` 表示该 tap 用 star 头材质绘制。
- `tapStarDouble = true` 表示使用 `$$` 语义。

## 3. Slide 头转 Tap 材质: `@`

作用对象:

- slide-like note 的起点头部，包括普通 slide 和 wifi。

合法语法:

- `1@-4[8:1]`
- `1@bx-4[8:1]`
- `1bx@-4[8:1]`

规则:

- `@` 仅作用于起点头部材质，把原本的 star 头改为普通 tap 材质。
- 头部上的 `b` / `x` 仍然继承到这个 tap 头上。
- slide 本体的 `trackBreak` 仍由轨迹侧的 `b` 控制，不受 `@` 影响。
- `sameHeadSlide` 的 doublestar 外观在 `@` 语义下忽略，不再影响头部材质。
- `@` 只能出现在起点数字后、轨迹符号前。
- `@` 可与头部 `b` / `x` 任意顺序组合。
- 当前实现中，`@` 与 `?` / `!` 的组合不定义，parser 直接报错。

实现语义:

- `slideHeadUsesTapMaterial = true` 表示该 slide 头部按 tap 材质绘制。
- 该标记只影响“前置可打头”的绘制与图标，不影响等待阶段/移动阶段的 slide 星体。

## 4. 无头 Slide: `?` / `!`

作用对象:

- slide-like note 的起点头部，包括普通 slide 和 wifi。

合法语法:

- `1?-4[8:1]`
- `1!-4[8:1]`

规则:

- `?` / `!` 仅能出现在起点数字后、轨迹符号前。
- 两者都会移除前置可打头。
- 两者都不改变 slide 的等待时间、滑动时长、轨迹判定、轨迹 each/break 语义。

视觉语义:

- `?`
  - 等待阶段的星体沿用现有渐入/放大曲线。
  - 轨迹箭头仍按原有规则渐入。
- `!`
  - 等待阶段的星体从一开始就使用最终亮度和最终大小。
  - 不走等待阶段的亮度/缩放渐变。
  - 轨迹箭头仍按原有规则渐入。

当前实现约定:

- `hasHeadStar = false` 表示前置可打头不存在。
- `headlessImmediate = true` 表示 `!`，`false` 表示 `?`。
- 无头 slide 会关闭头部 SFX / 头部 judge / 头部 review overlay，这一行为是当前产品约定。

## 5. 解析与渲染字段

正式 parser 输出的关键字段:

- tap:
  - `tapUsesStarMaterial`
  - `tapStarDouble`
  - `isBreak`
  - `isEx`
- slide / wifi:
  - `headBreak`
  - `headEx`
  - `trackBreak`
  - `hasHeadStar`
  - `slideHeadUsesTapMaterial`
  - `headlessImmediate`
  - `slideTraceSecond`
  - `endSecond`

quick timeline 需要同步的 flag:

- `TimelineRenderFlagTapUsesStarMaterial`
- `TimelineRenderFlagTapStarDouble`
- `TimelineRenderFlagSlideHeadUsesTapMaterial`
- `TimelineRenderFlagHeadlessImmediate`

预览层对应关系:

- `drawTapMarker`
  - 处理普通 tap、`$` / `$$` tap，以及带 `@` 的 slide 头。
- `drawSlideMarker` / `drawWifiMarker`
  - 处理等待阶段与移动阶段的 slide 星体。
  - `?` / `!` 的差异只在这里体现。
- `selectTapNoteGuideImage`
  - 根据材质切换普通 guide / slide guide。

## 6. Transform 保真要求

图表批处理、镜像、旋转、切换 break/ex 时，以下修饰符必须保留:

- tap: `$` / `$$`
- slide-like: `@` / `?` / `!`

允许批处理把修饰符重排成规范顺序，只要语义不变即可。

当前实现的规范顺序:

- tap: `lane + b + x + $/$$ + h + [duration]`
- slide-like: `lane + head b + head x + @ + ?/! + track/body`

