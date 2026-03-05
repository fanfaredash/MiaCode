# App Icon Placeholder

Put your Windows app icon file here:

- `resources/icons/app.ico`

Build behavior:

- If `app.ico` exists, CMake embeds it into `miacode.exe` on Windows.
- If it does not exist, build still succeeds with default executable icon.

