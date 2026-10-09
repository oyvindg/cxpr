#!/usr/bin/env node

const fs = require("fs");
const path = require("path");
const { PassThrough } = require("stream");
const { pathToFileURL } = require("url");
const lsp = require("vscode-languageserver/node");
const { TOKEN_TYPES, createServer } = require("./server");

function fail(outputPath, error) {
  const message = error instanceof Error ? error.message : String(error);
  if (outputPath) fs.writeFileSync(outputPath, JSON.stringify({ error: message }), "utf8");
  process.stderr.write(`${message}\n`);
  process.exitCode = 1;
}

function frame(payload) {
  const body = JSON.stringify(payload);
  return `Content-Length: ${Buffer.byteLength(body)}\r\n\r\n${body}`;
}

function createReader(stream, onMessage) {
  let buffered = Buffer.alloc(0);
  stream.on("data", (chunk) => {
    buffered = Buffer.concat([buffered, chunk]);
    for (;;) {
      const headerEnd = buffered.indexOf("\r\n\r\n");
      if (headerEnd < 0) return;
      const header = buffered.subarray(0, headerEnd).toString("ascii");
      const match = /Content-Length:\s*(\d+)/i.exec(header);
      if (!match) throw new Error("Invalid LSP response header");
      const length = Number(match[1]);
      const bodyStart = headerEnd + 4;
      if (buffered.length < bodyStart + length) return;
      const body = buffered.subarray(bodyStart, bodyStart + length).toString("utf8");
      buffered = buffered.subarray(bodyStart + length);
      onMessage(JSON.parse(body));
    }
  });
}

function severity(value) {
  return value === 1 ? "error" : value === 2 ? "warning" : "info";
}

function decodeSemantic(data) {
  const tokens = [];
  let line = 0;
  let start = 0;
  for (let i = 0; i < data.length; i += 5) {
    line += data[i];
    start = data[i] === 0 ? start + data[i + 1] : data[i + 1];
    tokens.push({ line, start, length: data[i + 2], type: TOKEN_TYPES[data[i + 3]] });
  }
  return tokens;
}

function normalize(op, result) {
  if (op === "semantic") return { tokens: decodeSemantic(result?.data || []) };
  if (op === "hover") {
    if (!result) return { kind: "markdown", contents: [] };
    const contents = Array.isArray(result.contents) ? result.contents : [result.contents];
    return {
      kind: "markdown",
      contents: contents.map((item) => ({ value: typeof item === "string" ? item : item.value || "" })),
      ...(result.range ? { range: {
        line: result.range.start.line,
        start: result.range.start.character,
        end: result.range.end.character
      } } : {})
    };
  }
  if (op === "completions") {
    const items = Array.isArray(result) ? result : result?.items || [];
    return { items: items.map((item) => {
      const edit = item.textEdit || {};
      return {
        label: typeof item.label === "string" ? item.label : item.label?.label || "",
        insertText: edit.newText || item.insertText || item.label,
        start: edit.range?.start?.character || 0,
        end: edit.range?.end?.character || 0,
        kind: item.kind === 3 ? "function" : item.kind === 9 ? "module" : item.kind === 14 ? "keyword" : "variable",
        detail: item.detail || ""
      };
    }) };
  }
  if (op === "definition") {
    const locations = !result ? [] : Array.isArray(result) ? result : [result];
    return { locations: locations.map((location) => ({
      path: location.uri?.startsWith("file:") ? require("url").fileURLToPath(location.uri) : location.uri || "",
      line: location.range?.start?.line || 0,
      start: location.range?.start?.character || 0,
      end: location.range?.end?.character || 0
    })) };
  }
  return result;
}

async function request(op, payload) {
  const keepAlive = setInterval(() => {}, 1000);
  const serverInput = new PassThrough();
  const serverOutput = new PassThrough();
  const connection = lsp.createConnection(serverInput, serverOutput, lsp.ProposedFeatures.all);
  createServer(connection).listen();
  const pending = new Map();
  let diagnosticsResolve;
  const diagnostics = new Promise((resolve) => {
    diagnosticsResolve = resolve;
  });
  createReader(serverOutput, (message) => {
    if (message.method === "textDocument/publishDiagnostics") diagnosticsResolve(message.params);
    if (message.id !== undefined && pending.has(message.id)) {
      const { resolve, reject } = pending.get(message.id);
      pending.delete(message.id);
      message.error ? reject(new Error(message.error.message)) : resolve(message.result);
    }
  });
  let nextId = 1;
  const sendRequest = (method, params) => new Promise((resolve, reject) => {
    const id = nextId++;
    pending.set(id, { resolve, reject });
    serverInput.write(frame({ jsonrpc: "2.0", id, method, params }));
  });
  const notify = (method, params) => serverInput.write(frame({ jsonrpc: "2.0", method, params }));
  const currentFile = path.resolve(payload.currentFile || "untitled.cxpr");
  const workspaceRoot = path.resolve(payload.workspaceRoot || process.cwd());
  const uri = pathToFileURL(currentFile).toString();
  await sendRequest("initialize", {
    processId: null,
    rootUri: pathToFileURL(workspaceRoot).toString(),
    workspaceFolders: [{ uri: pathToFileURL(workspaceRoot).toString(), name: path.basename(workspaceRoot) }],
    capabilities: {}, initializationOptions: {}
  });
  notify("initialized", {});
  notify("textDocument/didOpen", { textDocument: {
    uri, languageId: "cxpr", version: 1, text: payload.yaml || ""
  } });
  let result;
  if (op === "diagnostics") {
    const published = await diagnostics;
    result = { diagnostics: (published.diagnostics || []).map((item) => ({
      line: item.range.start.line,
      start: item.range.start.character,
      end: item.range.end.character,
      severity: severity(item.severity),
      message: item.message
    })) };
  } else {
    const methods = {
      completions: "textDocument/completion",
      hover: "textDocument/hover",
      definition: "textDocument/definition",
      semantic: "textDocument/semanticTokens/full"
    };
    const params = { textDocument: { uri } };
    if (op === "completions" || op === "hover" || op === "definition") {
      params.position = { line: payload.line || 0, character: payload.character || 0 };
    }
    result = normalize(op, await sendRequest(methods[op], params));
  }
  await sendRequest("shutdown", null);
  notify("exit");
  serverInput.destroy();
  serverOutput.destroy();
  clearInterval(keepAlive);
  return result;
}

async function main() {
  const [, , op, inputPath, outputPath] = process.argv;
  if (!op || !inputPath || !outputPath) throw new Error("Usage: request-cli.js <op> <input.json> <output.json>");
  const payload = JSON.parse(fs.readFileSync(inputPath, "utf8"));
  fs.writeFileSync(outputPath, JSON.stringify(await request(op, payload)), "utf8");
}

main().catch((error) => fail(process.argv[4], error));
