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

const managerCpp = read("src/extensions/ExtensionManager.cpp");
const registryBlock = managerCpp.match(/QVector<QJsonObject> extensionApiRegistry\(\)[\s\S]*?return registry;\r?\n}/);
if (!registryBlock) {
  fail("cannot find extension API registry");
} else {
  const statuses = registryBlock[0]
    .split(/\r?\n/)
    .filter((line) => line.includes("apiDescriptor("))
    .map((line) => {
      const literals = qStringLiterals(line);
      return literals[literals.length - 2];
    })
    .filter(Boolean);
  const allowedStatuses = new Set(["implemented", "planned", "blocked"]);
  for (const status of statuses) {
    if (!allowedStatuses.has(status)) {
      fail(`unsupported registry status: ${status}`);
    }
  }
  if (!statuses.includes("implemented")) {
    fail("registry has no implemented APIs");
  }
}

const blockedBlock = managerCpp.match(/bool isBlockedPermission[\s\S]*?static const QSet<QString> blocked\{([\s\S]*?)\};/);
if (!blockedBlock) {
  fail("cannot find blocked permission list");
} else {
  const blockedPermissions = qStringLiterals(blockedBlock[1]);
  const expectedBlockedPermissions = [];
  if (blockedPermissions.length !== expectedBlockedPermissions.length) {
    fail(`blocked permission list has unexpected size: ${blockedPermissions.join(", ")}`);
  }
  for (const permission of expectedBlockedPermissions) {
    if (!blockedPermissions.includes(permission)) {
      fail(`blocked permission list is missing: ${permission}`);
    }
  }
  for (const permission of blockedPermissions) {
    if (!schemaPermissions.has(permission)) {
      fail(`blocked permission is missing from schema: ${permission}`);
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
  const descriptor = new RegExp(`apiDescriptor\\(QStringLiteral\\("${id.replace(".", "\\.")}"\\)[\\s\\S]*?QStringLiteral\\("implemented"\\)`);
  if (!descriptor.test(managerCpp)) {
    fail(`registry is missing experimental raw API descriptor: ${id}`);
  }
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
