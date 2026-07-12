#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.resolve(process.argv[2] || ".");
const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const schemaPath = path.resolve(scriptDir, "../../resources/extensions/miacode-extension.schema.json");
const schema = JSON.parse(fs.readFileSync(schemaPath, "utf8"));
const supportedPermissions = new Set(schema.properties.permissions.items.enum);
const manifestPath = ["miacode-extension.json", "package.json"]
  .map((name) => path.join(root, name))
  .find((candidate) => fs.existsSync(candidate));

function fail(message) {
  console.error(`Extension manifest invalid: ${message}`);
  process.exit(1);
}

if (!manifestPath) {
  fail("missing miacode-extension.json or package.json");
}

let manifest = JSON.parse(fs.readFileSync(manifestPath, "utf8"));
if (path.basename(manifestPath) === "package.json" && manifest.miacodeExtension) {
  manifest = { ...manifest, ...manifest.miacodeExtension };
}

const idPattern = /^[a-z0-9][a-z0-9._-]{1,95}$/;
for (const key of ["id", "name", "version", "publisher"]) {
  if (typeof manifest[key] !== "string" || !manifest[key].trim()) {
    fail(`missing string field '${key}'`);
  }
}
for (const key of ["id", "publisher"]) {
  if (!idPattern.test(manifest[key])) {
    fail(`invalid '${key}'`);
  }
}
if (!manifest.engines || typeof manifest.engines.miacode !== "string") {
  fail("missing engines.miacode");
}
if (manifest.main && !fs.existsSync(path.resolve(root, manifest.main))) {
  fail(`main entry does not exist: ${manifest.main}`);
}
const permissions = manifest.permissions || [];
if (!Array.isArray(permissions)) {
  fail("permissions must be an array");
}
const seenPermissions = new Set();
for (const permission of permissions) {
  if (typeof permission !== "string" || !permission.trim()) {
    fail("permissions must contain only non-empty strings");
  }
  if (!supportedPermissions.has(permission)) {
    fail(`unsupported permission '${permission}'`);
  }
  if (seenPermissions.has(permission)) {
    fail(`duplicate permission '${permission}'`);
  }
  seenPermissions.add(permission);
}

const commands = manifest.contributes?.commands || [];
const languages = manifest.contributes?.languages || [];
const activationEvents = manifest.activationEvents || [];
if ((commands.length > 0 || activationEvents.length > 0) && !manifest.main) {
  fail("command or activated extensions require a main entry");
}
if (!manifest.main && languages.length === 0) {
  fail("pure data extensions without main must contribute at least one language");
}
const commandIds = new Set();
for (const command of commands) {
  if (!command.command || !command.title) {
    fail("each contributes.commands item needs command and title");
  }
  if (!idPattern.test(command.command)) {
    fail(`invalid command id '${command.command}'`);
  }
  if (commandIds.has(command.command)) {
    fail(`duplicate command id '${command.command}'`);
  }
  commandIds.add(command.command);
}

const languageIds = new Set();
for (const language of languages) {
  if (!language.id || !language.label || !language.translations) {
    fail("each contributes.languages item needs id, label, and translations");
  }
  if (!idPattern.test(language.id)) {
    fail(`invalid language id '${language.id}'`);
  }
  if (languageIds.has(language.id)) {
    fail(`duplicate language id '${language.id}'`);
  }
  languageIds.add(language.id);
  const translationsPath = path.resolve(root, language.translations);
  if (!fs.existsSync(translationsPath)) {
    fail(`language translations file does not exist: ${language.translations}`);
  }
  const translations = JSON.parse(fs.readFileSync(translationsPath, "utf8"));
  if (!translations || typeof translations !== "object" || Array.isArray(translations)) {
    fail(`language translations must be a JSON object: ${language.translations}`);
  }
}

const menus = manifest.contributes?.menus || {};
const supportedMenuLocations = new Set(["tools/menu", "menubar/beforeHelp"]);
for (const [location, items] of Object.entries(menus)) {
  if (!supportedMenuLocations.has(location)) {
    fail(`unsupported menu location '${location}'`);
  }
  if (!Array.isArray(items)) {
    fail(`contributes.menus.${location} must be an array`);
  }
  for (const item of items) {
    if (!commandIds.has(item.command)) {
      fail(`menu '${location}' references unknown command '${item.command}'`);
    }
  }
}

console.log(`Extension manifest OK: ${manifest.publisher}.${manifest.id}`);
