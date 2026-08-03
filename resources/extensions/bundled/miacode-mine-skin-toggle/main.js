"use strict";

var SKIN_SETTING_KEY = "miacode-mine-skin-toggle.enabled";
var SKIN_COMMAND_ID = "miacode-mine-skin-toggle.toggle";
var SFX_SETTING_KEY = "miacode-mine-skin-toggle.sfx-enabled";
var SFX_COMMAND_ID = "miacode-mine-skin-toggle.toggle-sfx";

function resultValue(result, fallback) {
  return result && result.ok !== false && result.value !== undefined
    ? result.value
    : fallback;
}

function storedPreference(key) {
  return resultValue(miacode.settings.get(key), true) !== false;
}

function applyPreference(enabled) {
  var result = miacode.preview.setMineSkinEnabled(enabled);
  if (result && result.ok === false) {
    miacode.window.showErrorMessage("切换地雷皮肤失败：" + (result.error || "未知错误"));
    return false;
  }
  miacode.commands.setChecked(SKIN_COMMAND_ID, enabled);
  return true;
}

function toggleMineSkin() {
  var renderState = resultValue(miacode.preview.getRenderState(), {});
  var current = typeof renderState.mineSkinEnabled === "boolean"
    ? renderState.mineSkinEnabled
    : storedPreference(SKIN_SETTING_KEY);
  var enabled = !current;
  if (!applyPreference(enabled)) {
    return;
  }
  miacode.settings.set(SKIN_SETTING_KEY, enabled);
  miacode.window.showInformationMessage(
    enabled ? "地雷将显示地雷皮肤" : "地雷将显示普通皮肤"
  );
}

function applySfxPreference(enabled) {
  var result = miacode.preview.setMineSfxEnabled(enabled);
  if (result && result.ok === false) {
    miacode.window.showErrorMessage("切换地雷音效失败：" + (result.error || "未知错误"));
    return false;
  }
  miacode.commands.setChecked(SFX_COMMAND_ID, enabled);
  return true;
}

function toggleMineSfx() {
  var renderState = resultValue(miacode.preview.getRenderState(), {});
  var current = typeof renderState.mineSfxEnabled === "boolean"
    ? renderState.mineSfxEnabled
    : storedPreference(SFX_SETTING_KEY);
  var enabled = !current;
  if (!applySfxPreference(enabled)) {
    return;
  }
  miacode.settings.set(SFX_SETTING_KEY, enabled);
  miacode.window.showInformationMessage(
    enabled ? "地雷音效已开启" : "地雷音效已关闭"
  );
}

function activate(context) {
  miacode.commands.registerCommand(
    SKIN_COMMAND_ID,
    toggleMineSkin
  );
  miacode.commands.registerCommand(SFX_COMMAND_ID, toggleMineSfx);
  applyPreference(storedPreference(SKIN_SETTING_KEY));
  applySfxPreference(storedPreference(SFX_SETTING_KEY));
  context.log("Mine skin and SFX toggles activated.");
}

function deactivate() {
  applyPreference(true);
  applySfxPreference(true);
}

module.exports = {
  activate: activate,
  deactivate: deactivate
};
