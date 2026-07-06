#!/usr/bin/env node
"use strict";

const path = require("path");

let nextId = 1;
const pending = new Map();
const commands = new Map();
const loadedExtensions = new Map();

for (const method of ["log", "info", "warn", "error"]) {
  console[method] = (...args) => {
    process.stderr.write(`${args.map((arg) => (typeof arg === "string" ? arg : JSON.stringify(arg))).join(" ")}\n`);
  };
}

function write(message) {
  process.stdout.write(`${JSON.stringify(message)}\n`);
}

function request(method, params = {}) {
  const id = nextId++;
  write({ jsonrpc: "2.0", id, method, params });
  return new Promise((resolve, reject) => {
    pending.set(id, { resolve, reject });
  });
}

function notify(method, params = {}) {
  write({ jsonrpc: "2.0", method, params });
}

function respond(id, result) {
  write({ jsonrpc: "2.0", id, result });
}

function fail(id, error) {
  write({
    jsonrpc: "2.0",
    id,
    error: { message: error && error.message ? error.message : String(error) },
  });
}

function createApi(extension) {
  const context = {
    extension,
    subscriptions: [],
    extensionPath: extension.rootPath,
    log: (message) => request("log", { message: `[${extension.qualifiedId}] ${message}` }),
  };

  const api = {
    commands: {
      registerCommand(command, callback) {
        if (typeof command !== "string" || !command) {
          throw new Error("registerCommand requires a command id.");
        }
        if (typeof callback !== "function") {
          throw new Error(`Command '${command}' requires a callback.`);
        }
        commands.set(command, { extensionId: extension.qualifiedId, callback });
        request("commands/register", { command, extensionId: extension.qualifiedId }).catch(() => {});
        const disposable = {
          dispose() {
            const current = commands.get(command);
            if (current && current.callback === callback) {
              commands.delete(command);
            }
          },
        };
        context.subscriptions.push(disposable);
        return disposable;
      },
    },
    window: {
      showInformationMessage(message) {
        return request("window/showMessage", { severity: "info", message: String(message) });
      },
      showWarningMessage(message) {
        return request("window/showMessage", { severity: "warning", message: String(message) });
      },
      showErrorMessage(message) {
        return request("window/showMessage", { severity: "error", message: String(message) });
      },
      openPreferences() {
        return request("window/openPreferences");
      },
    },
    workspace: {
      getActiveDocument() {
        return request("workspace/getActiveDocument");
      },
      applyDocumentEdit(edit) {
        if (!edit || typeof edit.text !== "string") {
          return Promise.reject(new Error("applyDocumentEdit v1 requires { text }."));
        }
        return request("workspace/applyDocumentEdit", { text: edit.text });
      },
    },
    diagnostics: {
      validateDocument() {
        return request("diagnostics/validateDocument");
      },
    },
  };

  return { api, context };
}

async function activateExtension(extension) {
  if (loadedExtensions.has(extension.qualifiedId)) {
    return;
  }
  const entry = path.resolve(extension.main);
  const previousGlobal = global.miacode;
  const { api, context } = createApi(extension);
  global.miacode = api;
  try {
    const moduleExports = require(entry);
    loadedExtensions.set(extension.qualifiedId, { moduleExports, context });
    if (moduleExports && typeof moduleExports.activate === "function") {
      await moduleExports.activate(context);
    }
    await request("log", { message: `Activated ${extension.qualifiedId}` });
  } finally {
    global.miacode = previousGlobal;
  }
}

async function initialize(params) {
  const extensions = Array.isArray(params.extensions) ? params.extensions : [];
  for (const extension of extensions) {
    if (extension.activateOnStartup) {
      try {
        await activateExtension(extension);
      } catch (error) {
        await request("log", {
          message: `Activation failed for ${extension.qualifiedId}: ${error && error.stack ? error.stack : error}`,
        });
      }
    }
  }
  return { ok: true };
}

async function executeCommand(params) {
  const command = params.command;
  const registration = commands.get(command);
  if (!registration) {
    throw new Error(`Command not registered: ${command}`);
  }
  await registration.callback();
  return { ok: true };
}

async function shutdown() {
  for (const [extensionId, loaded] of loadedExtensions) {
    try {
      if (loaded.moduleExports && typeof loaded.moduleExports.deactivate === "function") {
        await loaded.moduleExports.deactivate();
      }
      for (const disposable of loaded.context.subscriptions) {
        if (disposable && typeof disposable.dispose === "function") {
          disposable.dispose();
        }
      }
    } catch (error) {
      notify("log", { message: `Deactivate failed for ${extensionId}: ${error}` });
    }
  }
  process.exit(0);
}

async function handleRequest(message) {
  if (message.method === "miacode/initialize") {
    return initialize(message.params || {});
  }
  if (message.method === "commands/execute") {
    return executeCommand(message.params || {});
  }
  if (message.method === "miacode/shutdown") {
    await shutdown();
    return { ok: true };
  }
  throw new Error(`Unknown extension host method: ${message.method}`);
}

let buffer = "";
process.stdin.setEncoding("utf8");
process.stdin.on("data", (chunk) => {
  buffer += chunk;
  let newline = buffer.indexOf("\n");
  while (newline >= 0) {
    const line = buffer.slice(0, newline).trim();
    buffer = buffer.slice(newline + 1);
    newline = buffer.indexOf("\n");
    if (!line) {
      continue;
    }
    let message;
    try {
      message = JSON.parse(line);
    } catch (error) {
      notify("log", { message: `Invalid host JSON: ${line}` });
      continue;
    }
    if (Object.prototype.hasOwnProperty.call(message, "id") && !message.method) {
      const waiter = pending.get(message.id);
      if (waiter) {
        pending.delete(message.id);
        if (message.error) {
          waiter.reject(new Error(message.error.message || "Host request failed."));
        } else {
          waiter.resolve(message.result);
        }
      }
      continue;
    }
    if (message.method) {
      Promise.resolve(handleRequest(message))
        .then((result) => {
          if (Object.prototype.hasOwnProperty.call(message, "id")) {
            respond(message.id, result || {});
          }
        })
        .catch((error) => {
          if (Object.prototype.hasOwnProperty.call(message, "id")) {
            fail(message.id, error);
          } else {
            notify("log", { message: error && error.stack ? error.stack : String(error) });
          }
        });
    }
  }
});

process.on("uncaughtException", (error) => {
  notify("log", { message: `uncaughtException: ${error && error.stack ? error.stack : error}` });
});

process.on("unhandledRejection", (error) => {
  notify("log", { message: `unhandledRejection: ${error && error.stack ? error.stack : error}` });
});
