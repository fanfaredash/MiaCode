# MiaCode Extension Guide

This folder is the user extension folder for this MiaCode installation. User-installed extensions should be placed here, next to this README:

```text
<MiaCode install root>\extensions
```

In release packages this is the `extensions` folder beside the top-level `MiaCode.exe` launcher. Do not install user extensions into `app\extensions`; that folder is reserved for MiaCode's internal Extension Host runtime.

Each extension is one folder. The manifest must be either:

```text
my-extension\miacode-extension.json
```

or:

```text
my-extension\package.json
```

with a `miacodeExtension` object.

## Language Pack

A pure language pack does not need JavaScript or `main`.

```json
{
  "id": "my-language",
  "name": "My Language",
  "version": "0.0.1",
  "publisher": "local",
  "engines": { "miacode": ">=1.0.0" },
  "contributes": {
    "languages": [
      {
        "id": "sample",
        "label": "Sample Language",
        "translations": "./i18n/sample.json"
      }
    ]
  }
}
```

Translation files are JSON objects keyed by MiaCode UI text ids:

```json
{
  "dialog.preferences.title": "Settings",
  "dialog.preferences.language": "Language"
}
```

If the language pack is deleted or disabled, its language disappears from Preferences.

## Command Extension

Command extensions need a JavaScript entry file.

```json
{
  "id": "hello-world",
  "name": "Hello World",
  "version": "0.0.1",
  "publisher": "local",
  "main": "./extension.js",
  "engines": { "miacode": ">=1.0.0" },
  "activationEvents": ["onStartupFinished"],
  "contributes": {
    "commands": [
      { "command": "hello-world.say-hello", "title": "Say Hello", "category": "Hello World" },
      { "command": "hello-world.open-preferences", "title": "Preferences", "category": "Hello World" }
    ],
    "menus": {
      "tools/menu": [
        { "command": "hello-world.say-hello" }
      ],
      "menubar/beforeHelp": [
        { "command": "hello-world.open-preferences" }
      ]
    }
  }
}
```

Menu locations:

- `tools/menu`: adds commands under `Tools -> Extensions`.
- `menubar/beforeHelp`: adds a top-level command entry before `Help`.

`extension.js` exports `activate(context)`:

```js
"use strict";

function activate(context) {
  context.log("activated");
  miacode.commands.registerCommand("hello-world.say-hello", async () => {
    await miacode.window.showInformationMessage("Hello from MiaCode.");
  });
  miacode.commands.registerCommand("hello-world.open-preferences", async () => {
    await miacode.window.openPreferences();
  });
}

module.exports = { activate };
```

## Available API

```text
miacode.commands.registerCommand(id, callback)
miacode.window.showInformationMessage(message)
miacode.window.showWarningMessage(message)
miacode.window.showErrorMessage(message)
miacode.window.openPreferences()
miacode.workspace.getActiveDocument()
miacode.workspace.applyDocumentEdit({ text })
miacode.diagnostics.validateDocument()
```

Document edits replace the active difficulty text as a controlled patch. Extensions do not receive internal C++ objects.

## Safety Notes

- Keep ids lowercase: `publisher.extension` and `command.id`.
- Do not assume a command extension runs if Node.js is missing.
- Language packs are data-only and do not require Node.js.
- MiaCode reloads the extension directory automatically and also provides a manual refresh button in Preferences.
- Refreshing extensions must not modify the current unsaved chart.
- Disabled extensions contribute nothing: no commands, menus, or languages.

## Prompt For AI

You can give this folder and the following prompt to an AI:

```text
Create a local MiaCode extension folder. Use miacode-extension.json. If it is a language pack, put translations in i18n/<language>.json and do not add main. If it registers commands, add extension.js and use the miacode API shown in README.md. Keep ids lowercase and validate the manifest before delivery.
```
