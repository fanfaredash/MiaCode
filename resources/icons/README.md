# App Icon Assets

Canonical source asset:

- `resources/icons/app.png`

Windows executable icon:

- `resources/icons/app.ico`

macOS bundle icon:

- `resources/icons/app.icns`

Build behavior:

- `app.png` is embedded into the Qt resource system and used as the runtime window icon.
- If `app.ico` exists, CMake embeds it into `MiaCode.exe` on Windows.
- If `app.icns` exists, CMake copies it into `MiaCode.app/Contents/Resources` and registers it as the bundle icon on macOS.
- If `app.ico` does not exist, build still succeeds with the default executable icon.
