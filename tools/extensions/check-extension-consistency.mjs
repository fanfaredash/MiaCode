#!/usr/bin/env node
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const scriptDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(scriptDir, "../..");

function read(relativePath) {
  return fs.readFileSync(path.join(repoRoot, relativePath), "utf8");
}

function fail(message) {
  console.error(`Extension consistency check failed: ${message}`);
  process.exitCode = 1;
}

function qStringLiterals(block) {
  return [...block.matchAll(/QStringLiteral\("([^"]+)"\)/g)].map((match) => match[1]);
}

const schema = JSON.parse(read("resources/extensions/miacode-extension.schema.json"));
const schemaPermissions = new Set(schema.properties.permissions.items.enum);
const manifestCpp = read("src/extensions/ExtensionManifest.cpp");
const archiveRegistry = JSON.parse(read("tools/extensions/extension-api-registry.json"));

if (typeof archiveRegistry.version !== "string" || archiveRegistry.version.trim() === "") {
  fail("archive API fixture has no version string");
}
if (!Array.isArray(archiveRegistry.apis)) {
  fail("archive API fixture has no apis array");
}

const apiFieldNames = ["id", "method", "permission", "risk", "status", "description"];
const apiIds = new Set();
const allowedStatuses = new Set(["implemented", "planned", "blocked"]);
for (const [index, api] of (archiveRegistry.apis ?? []).entries()) {
  if (api === null || typeof api !== "object") {
    fail(`archive API entry ${index} is not an object`);
    continue;
  }
  const keys = Object.keys(api).sort();
  if (keys.join("|") !== apiFieldNames.slice().sort().join("|")) {
    fail(`archive API entry ${index} must contain exactly id/method/permission/risk/status/description`);
  }
  for (const field of apiFieldNames) {
    if (typeof api[field] !== "string") {
      fail(`archive API entry ${index} has a non-string ${field}`);
    }
  }
  if (api.id && apiIds.has(api.id)) {
    fail(`archive API id is duplicated: ${api.id}`);
  }
  if (api.id) apiIds.add(api.id);
  if (api.status && !allowedStatuses.has(api.status)) {
    fail(`unsupported registry status: ${api.status}`);
  }
}
if (!(archiveRegistry.apis ?? []).some((api) => api.status === "implemented")) {
  fail("archive API fixture has no implemented APIs");
}

const supportedBlock = manifestCpp.match(/bool isSupportedPermission[\s\S]*?static const QSet<QString> allowed\{([\s\S]*?)\};/);
if (!supportedBlock) {
  fail("cannot find C++ supported permission list");
} else {
  const cppPermissions = new Set(qStringLiterals(supportedBlock[1]));
  for (const permission of cppPermissions) {
    if (!schemaPermissions.has(permission)) {
      fail(`C++ loader permission is missing from schema: ${permission}`);
    }
  }
  for (const permission of schemaPermissions) {
    if (!cppPermissions.has(permission)) {
      fail(`schema permission is missing from C++ loader: ${permission}`);
    }
  }
}

const requiredExperimentalRawApiIds = [
  "shell.execute",
  "process.spawn",
  "native",
  "internal.raw",
  "renderer.raw",
  "export.raw",
  "security",
  "updates",
];
for (const id of requiredExperimentalRawApiIds) {
  const descriptor = archiveRegistry.apis?.find((api) => api.id === id);
  if (!descriptor || descriptor.status !== "implemented") {
    fail(`archive API fixture is missing implemented experimental raw API descriptor: ${id}`);
  }
}

const bundledManifest = JSON.parse(read("resources/extensions/bundled/miacode-mine-skin-toggle/miacode-extension.json"));
if (typeof bundledManifest.id !== "string" || bundledManifest.id.trim() === "") {
  fail("bundled manifest fixture is missing its id");
}

const publicDocs = [
  "resources/extensions/README.md",
  "docs/specs/extensions/EXTENSION_SYSTEM_V1.md",
  "packages/miacode-extension-api/index.d.ts",
];
for (const relativePath of publicDocs) {
  const text = read(relativePath);
  const deprecatedStatuses = ["par" + "tial", "fro" + "zen"];
  const deprecatedStatusPattern = new RegExp(`\\b(${deprecatedStatuses.join("|")})\\b`);
  if (deprecatedStatusPattern.test(text)) {
    fail(`${relativePath} still mentions old API status words`);
  }
}

if (process.exitCode) {
  process.exit(process.exitCode);
}
console.log("Extension consistency OK");
