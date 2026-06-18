# HUD 字体裁剪脚本 (Font subsetting)

把大体积 CJK 字体裁剪成 HUD 真正需要的字符集,用于 MiaCode 预览/导出 HUD 的
**默认字体**(`PreviewHudState.cpp::previewHudTimestampFont`,内嵌资源
`:/fonts/xiaolai_mono.ttf`)。

## 文件

- `subset_hud_font.py` — 裁剪脚本(参数化输入/输出)。
- `tongyong_guifan.txt` — 通用规范汉字表(8105 常用汉字),Chinese 字符来源。

依赖:`fonttools`(`pip install fonttools`)。

## 保留的字符集

- 拉丁(Basic Latin + Latin-1 + Latin Extended-A)、希腊、西里尔
- 常用标点 / 货币 / 字母式符号(No.、TM)/ 罗马数字 / 圈号 / 星号·音符等符号 / Dingbats
- CJK 标点、日文假名、全角形式
- **中文常用字**:通用规范汉字表 8105(`tongyong_guifan.txt`)
- **日文汉字**:JIS X 0208(shift_jis 在 CJK 区可编码的全部码位)

与片头/封面卡片字体的既有配方 `font_candidates/_subset/do_subset.py` 一致。

## 用法

```sh
python scripts/subset_hud_font.py \
    --input  C:/path/to/SomeBigCJK-Regular.ttf \
    --output assets/fonts/XiaolaiMono-Regular.subset.ttf
```

裁剪后字体家族名(name ID 1)保持不变。当前默认字体 = Xiaolai Mono
(21 MB → ~5.5 MB)。

## 替换默认 HUD 字体后还要做

1. `resources/fonts.qrc` 的 `fonts/xiaolai_mono.ttf` 别名指向新的 subset 文件。
2. 重新构建(AUTORCC 会重新打包 `qrc_fonts.cpp`)。若增量构建没生效,
   touch `resources/fonts.qrc` 或删 `build/**/qrc_fonts.cpp*` 强制 RCC。
3. 如换了字体家族,改 `PreviewHudState.cpp::previewHudTimestampFont` 里的回退家族名。
