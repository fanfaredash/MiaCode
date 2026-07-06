"use strict";

function activate(context) {
  context.log("hello-world activated");
  miacode.commands.registerCommand("hello-world.say-hello", async () => {
    const document = await miacode.workspace.getActiveDocument();
    await miacode.window.showInformationMessage(
      `Hello from MiaCode extension. Active document has ${document.text.length} characters.`
    );
  });
}

function deactivate() {}

module.exports = { activate, deactivate };
