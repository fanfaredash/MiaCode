MiaCode Music Import Guide

[简体中文]
放置位置：
- 将片头音效文件放入 assets/music。
- 可在预览设置 > 音乐 中点击“打开文件夹”进入此目录。

文件名与格式：
- 可以放入多个片头音效文件，并在“预览设置 > 音乐 > 片头音效”中选择。
- 支持的格式：WAV、MP3、OGG、FLAC。
- 旧版兼容：如果没有手动选择文件，track_start.wav 会作为默认自定义片头音效被自动识别。

注意事项：
- 已选择的文件会优先替换片头音效。
- 如果已选择文件缺失，MiaCode 会继续尝试 assets/music/track_start.wav。
- 如果 assets/music/track_start.wav 也缺失，MiaCode 会读取 assets/SFX/track_start.wav。
- 建议使用较短音频，避免片头播放过长。

[English]
Location:
- Put the intro sound file in assets/music.
- Open this folder from Preview Settings > Music > Open Folder.

File names and formats:
- You can place multiple intro sound files here and select one in Preview Settings > Music > Intro Sound.
- Supported formats: WAV, MP3, OGG, FLAC.
- Backward compatibility: when no file is selected manually, track_start.wav is still detected as the default custom intro sound.

Notes:
- The selected file overrides the intro sound first.
- If the selected file is missing, MiaCode still tries assets/music/track_start.wav.
- If assets/music/track_start.wav is also missing, MiaCode falls back to assets/SFX/track_start.wav.
- Short audio is recommended so the intro does not run too long.

[日本語]
配置場所：
- イントロ音声ファイルは assets/music に配置してください。
- プレビュー設定 > 音楽 > フォルダーを開く からこのフォルダーを開けます。

ファイル名と形式：
- 複数のイントロ音声ファイルを配置し、プレビュー設定 > 音楽 > イントロ音声 で選択できます。
- 対応形式：WAV、MP3、OGG、FLAC。
- 互換性：手動でファイルを選択していない場合、track_start.wav は従来どおりデフォルトのカスタムイントロ音声として認識されます。

注意：
- 選択したファイルが最優先でイントロ音声として使用されます。
- 選択したファイルが見つからない場合、MiaCode は assets/music/track_start.wav を試します。
- assets/music/track_start.wav もない場合、MiaCode は assets/SFX/track_start.wav を使用します。
- イントロが長くなりすぎないよう、短めの音声を推奨します。
