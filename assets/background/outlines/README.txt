MiaCode Judge Line Import Guide

[简体中文]
放置位置：
- 将自定义判定线图片放入 assets/background/outlines。
- 文件名会作为预览设置中“判定线”列表的名称显示。

文件名与格式：
- 使用 PNG 图片文件。
- 建议使用透明背景 PNG。
- 建议尺寸为 1080 x 1080，内容对齐预览画面的正方形判定区。

注意事项：
- 导入按钮只会打开此文件夹，不会复制或转换文件。
- 自定义判定线会作为普通预览的外观覆盖层；启用“暂停时显示判定区”时，暂停辅助视图会按“自定义判定线 + outline_area.png + region_labels_overlay_transparent_v3.png”合成显示。
- 修改后若列表没有刷新，请重新打开预览设置。

[English]
Location:
- Put custom judge-line images in assets/background/outlines.
- The file name is shown in the Judge Line list in Preview Settings.

File names and formats:
- Use PNG image files.
- Transparent PNG files are recommended.
- Recommended size: 1080 x 1080, aligned to the square judge area in the preview.

Notes:
- The import button only opens this folder; it does not copy or convert files.
- Custom judge lines are the normal visual overlay. When "show judge area while paused" is enabled, the paused helper view composites the custom judge line + the judge-area region/label overlay; the built-in default outline ring is removed from that overlay so it does not stack on top of the custom judge line.
- If the list does not refresh after editing files, reopen Preview Settings.

[日本語]
配置場所：
- カスタム判定線画像は assets/background/outlines に配置してください。
- ファイル名がプレビュー設定の「判定線」リスト名として表示されます。

ファイル名と形式：
- PNG 画像ファイルを使用してください。
- 透明背景の PNG を推奨します。
- 推奨サイズは 1080 x 1080 で、プレビュー画面の正方形判定エリアに合わせてください。

注意：
- インポートボタンはこのフォルダーを開くだけで、ファイルのコピーや変換は行いません。
- カスタム判定線は通常プレビューの外観用オーバーレイです。「一時停止中に判定エリアを表示」が有効な場合、一時停止中の補助表示は「カスタム判定線 + outline_area.png + region_labels_overlay_transparent_v3.png」を合成して表示します。
- 編集後にリストへ反映されない場合は、プレビュー設定を開き直してください。
