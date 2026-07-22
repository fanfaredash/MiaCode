"use strict";

var PET_ID = "miacode-pet-overlay.pet";
var PET_SCRIPT = "pet-window.ps1";
var STATE_FILE = "pet-extension-state.json";
var extensionPath = "";

function resultOk(result) {
  return !result || result.ok !== false;
}

function showFailure(prefix, result) {
  var detail = result && result.error ? result.error : "unknown error";
  miacode.window.showErrorMessage(prefix + ": " + detail);
}

function removePetOverlay() {
  var result = miacode.preview.removeOverlay(PET_ID);
  return resultOk(result);
}

function launchPetWindow(showMessage) {
  removePetOverlay();
  var result = miacode.experimental.invoke("process.spawn", {
    program: "powershell.exe",
    args: [
      "-NoProfile",
      "-ExecutionPolicy",
      "Bypass",
      "-STA",
      "-File",
      PET_SCRIPT,
      "-ImagePath",
      "assets\\skins\\default.png",
      "-Size",
      "180",
      "-HostPid",
      "0"
    ],
    cwd: extensionPath
  });

  if (!resultOk(result)) {
    showFailure("Failed to show pet window", result);
    return;
  }

  if (showMessage) {
    miacode.window.showInformationMessage("Pet window launched. It should appear near the bottom-right of the code area.");
  }
}

function statePath() {
  return extensionPath + "/" + STATE_FILE;
}

function readState() {
  var result = miacode.fs.readText(statePath());
  if (!resultOk(result) || typeof result.value !== "string" || !result.value) {
    return {};
  }
  try {
    return JSON.parse(result.value);
  } catch (error) {
    return {};
  }
}

function writeState(state) {
  return miacode.fs.writeText(statePath(), JSON.stringify(state, null, 2) + "\n");
}

function autoStartEnabled() {
  return readState().autoStart === true;
}

function toggleAutoStart() {
  var state = readState();
  var next = !autoStartEnabled();
  state.autoStart = next;
  var result = writeState(state);
  if (!resultOk(result)) {
    showFailure("Failed to update pet auto start", result);
    return;
  }
  miacode.window.showInformationMessage("Pet auto start: " + (next ? "on" : "off"));
  if (next) {
    launchPetWindow(false);
  }
}

function activate(context) {
  extensionPath = context.extensionPath;
  miacode.commands.registerCommand("miacode-pet-overlay.spin", function() {
    launchPetWindow(true);
  });
  miacode.commands.registerCommand("miacode-pet-overlay.reload", function() {
    launchPetWindow(true);
  });
  miacode.commands.registerCommand("miacode-pet-overlay.toggle-autostart", toggleAutoStart);
  if (autoStartEnabled()) {
    launchPetWindow(false);
  }
  context.log("MiaCode Pet Overlay activated.");
}

module.exports = {
  activate: activate
};
