const fs = require("node:fs/promises");
const path = require("node:path");
const { spawn } = require("node:child_process");
const { fileURLToPath, pathToFileURL } = require("node:url");
const lsp = require("vscode-languageserver/node");
const { TextDocument } = require("vscode-languageserver-textdocument");
const { LanguageService } = require("../language-service/dist/index.js");

const TOKEN_TYPES = [
  "function", "type", "model", "import", "importPath", "parameter", "namedArgument", "assignment",
  "block", "plotBlock", "property", "reference", "input", "state", "variable", "string",
  "number", "operator", "keyword", "punctuation", "comment"
];
const SYMBOL_KINDS = {
  function: lsp.SymbolKind.Function, input: lsp.SymbolKind.Variable, param: lsp.SymbolKind.Constant,
  state: lsp.SymbolKind.Variable, use: lsp.SymbolKind.Namespace, model: lsp.SymbolKind.Module,
  hostBlock: lsp.SymbolKind.Object, binding: lsp.SymbolKind.Variable, output: lsp.SymbolKind.Variable
};
const COMPLETION_KINDS = {
  function: lsp.CompletionItemKind.Function, keyword: lsp.CompletionItemKind.Keyword,
  module: lsp.CompletionItemKind.Module, property: lsp.CompletionItemKind.Property,
  parameter: lsp.CompletionItemKind.Variable, variable: lsp.CompletionItemKind.Variable
};

class NodeWorkspace {
  constructor(roots) { this.roots = roots; }
  readFile(file) { return fs.readFile(file, "utf8"); }
  importDir() { return this.roots[0] || process.cwd(); }
  async resolveImport(spec, from) {
    const name = `${spec.replaceAll(".", path.sep)}.cxpr`;
    const moduleRoots = [];
    for (let directory = path.dirname(from);;) {
      moduleRoots.push(path.join(directory, "modules"));
      const parent = path.dirname(directory);
      if (parent === directory) break;
      directory = parent;
    }
    const candidates = [
      path.resolve(path.dirname(from), name),
      ...moduleRoots.map((root) => path.resolve(root, name)),
      ...this.roots.flatMap((root) => [
        path.resolve(root, name),
        path.resolve(root, "modules", name),
        path.resolve(root, "libs/dyn/cxpr", name)
      ])
    ];
    for (const candidate of candidates) {
      try { return { path: candidate, source: await this.readFile(candidate) }; } catch {}
    }
    return null;
  }
}

class WorkspaceReferenceIndex {
  constructor(workspace) {
    this.workspace = workspace;
    this.files = new Map();
    this.ready = null;
  }

  async ensure() {
    if (!this.ready) this.ready = this.scan();
    await this.ready;
  }

  async scan() {
    for (const root of this.workspace.roots) await this.scanDirectory(root);
  }

  async scanDirectory(directory) {
    let entries;
    try { entries = await fs.readdir(directory, { withFileTypes: true }); } catch { return; }
    for (const entry of entries) {
      if (entry.name === ".git" || entry.name === "node_modules" || entry.name.startsWith("build")) continue;
      const target = path.join(directory, entry.name);
      if (entry.isDirectory()) await this.scanDirectory(target);
      else if (entry.isFile() && entry.name.endsWith(".cxpr")) {
        try { await this.update(target, await this.workspace.readFile(target)); } catch {}
      }
    }
  }

  async update(file, source) {
    const lines = source.split("\n");
    const declarations = [];
    const imports = new Map();
    const calls = [];
    for (let line = 0; line < lines.length; line += 1) {
      const text = lines[line];
      const declaration = text.match(/^\s*(model|fn)\s+([A-Za-z_][A-Za-z0-9_]*)/);
      if (declaration) declarations.push({ name: declaration[2], line,
        character: text.indexOf(declaration[2], declaration.index), kind: declaration[1] });
      const grouped = text.match(/^\s*use\s+(.+?)\s+from\s+([A-Za-z_][A-Za-z0-9_.]*)\s*$/);
      const direct = grouped ? null : text.match(/^\s*use\s+([A-Za-z_][A-Za-z0-9_]*)\s*$/);
      if (grouped) {
        for (const rawName of grouped[1].split(",")) {
          const name = rawName.trim();
          if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) continue;
          const target = await this.workspace.resolveImport(`${grouped[2]}.${name}`, file);
          if (target) imports.set(name, { path: target.path, line,
            character: text.indexOf(name, grouped.index), name });
        }
      } else if (direct) {
        const target = await this.workspace.resolveImport(direct[1], file);
        if (target) imports.set(direct[1], { path: target.path, line,
          character: text.indexOf(direct[1], direct.index), name: direct[1] });
      }
      const callPattern = /\b([A-Za-z_][A-Za-z0-9_]*)\s*\(/g;
      for (let call; (call = callPattern.exec(text));) {
        if (declaration && call[1] === declaration[2] && call.index === text.indexOf(declaration[2])) continue;
        calls.push({ name: call[1], line, character: call.index });
      }
    }
    this.files.set(path.resolve(file), { source, declarations, imports, calls });
  }

  tokenAt(file, position) {
    const indexed = this.files.get(path.resolve(file));
    const line = indexed?.source.split("\n")[position.line] || "";
    const pattern = /[A-Za-z_][A-Za-z0-9_]*/g;
    for (let token; (token = pattern.exec(line));) {
      if (position.character >= token.index && position.character <= token.index + token[0].length)
        return token[0];
    }
    return null;
  }

  targetAt(file, position) {
    const resolvedFile = path.resolve(file);
    const indexed = this.files.get(resolvedFile);
    const name = this.tokenAt(resolvedFile, position);
    if (!indexed || !name) return null;
    const declaration = indexed.declarations.find((item) => item.name === name);
    if (declaration) return { path: resolvedFile, name, declaration };
    const imported = indexed.imports.get(name);
    return imported ? { path: path.resolve(imported.path), name, declaration: null } : null;
  }

  references(target, includeDeclaration = false) {
    const locations = [];
    for (const [file, indexed] of this.files) {
      const local = file === target.path && indexed.declarations.some((item) => item.name === target.name);
      const imported = indexed.imports.get(target.name);
      const usesTarget = local || path.resolve(imported?.path || "") === target.path;
      if (!usesTarget) continue;
      if (imported && path.resolve(imported.path) === target.path) locations.push(this.location(file, imported));
      for (const call of indexed.calls.filter((item) => item.name === target.name))
        locations.push(this.location(file, call));
      if (includeDeclaration && local) {
        for (const declaration of indexed.declarations.filter((item) => item.name === target.name))
          locations.push(this.location(file, declaration));
      }
    }
    return locations;
  }

  location(file, item) {
    return { uri: pathToFileURL(file).toString(), range: {
      start: { line: item.line, character: item.character },
      end: { line: item.line, character: item.character + item.name.length }
    } };
  }
}

class CliAnalyzer {
  constructor(executable) { this.executable = executable; }
  async analyzeDocument(source, name) {
    const stdout = await new Promise((resolve, reject) => {
      const child = spawn(this.executable, ["--source-name", name], { stdio: ["pipe", "pipe", "pipe"] });
      const output = [];
      const errors = [];
      child.stdout.on("data", (chunk) => output.push(chunk));
      child.stderr.on("data", (chunk) => errors.push(chunk));
      child.on("error", reject);
      child.on("close", (code) => code === 0
        ? resolve(Buffer.concat(output).toString("utf8"))
        : reject(new Error(Buffer.concat(errors).toString("utf8") || `cxpr CLI exited ${code}`)));
      child.stdin.end(source);
    });
    return JSON.parse(stdout);
  }
  async resolveAt() { return null; }
  async completeAt() { return []; }
  async queryFunctions() { return []; }
}

function moduleCandidates(configured) {
  return [configured, process.env.CXPR_NODE, "cxpr-node",
    path.resolve(__dirname, "../../../cxpr-node/dist/index.js")].filter(Boolean);
}

function loadNativeAnalyzer(configured) {
  for (const candidate of moduleCandidates(configured)) {
    try {
      const loaded = require(candidate);
      if (loaded.CxprNodeAnalyzer) return new loaded.CxprNodeAnalyzer();
    } catch {}
  }
  throw new Error("cxpr-node native analyzer not found");
}

async function loadWasmAnalyzer(configured, analyzerModule, factoryModule, wasmPath) {
  if (analyzerModule && factoryModule) {
    const { WasmAnalyzer } = require(path.resolve(analyzerModule));
    const loadedFactory = require(path.resolve(factoryModule));
    const factory = loadedFactory.default || loadedFactory;
    const options = wasmPath
      ? { wasmBinary: await fs.readFile(path.resolve(wasmPath)) }
      : undefined;
    return WasmAnalyzer.create(factory, options);
  }
  for (const candidate of moduleCandidates(configured)) {
    try {
      const loaded = require(candidate);
      if (!loaded.WasmAnalyzer) continue;
      const packageRoot = path.dirname(require.resolve(candidate));
      const factory = require(path.resolve(packageRoot, "../wasm/dist/cxpr_wasm.js"));
      return loaded.WasmAnalyzer.create(factory.default || factory);
    } catch {}
  }
  throw new Error("cxpr-node WASM analyzer not found");
}

async function selectAnalyzer(options = {}) {
  const requested = options.transport || process.env.CXPR_ANALYZER || "auto";
  if (requested === "cli") {
    const executable = options.cli || process.env.CXPR_DOCUMENT_TOOLING;
    if (!executable) throw new Error("CLI fallback requires CXPR_DOCUMENT_TOOLING");
    return new CliAnalyzer(executable);
  }
  if (requested === "native" || (requested === "auto" && process.platform === "linux" && process.arch === "x64")) {
    return loadNativeAnalyzer(options.cxprNodeModule);
  }
  return loadWasmAnalyzer(options.cxprNodeModule, options.wasmAnalyzerModule,
    options.wasmFactoryModule, options.wasmPath);
}

function encodeSemanticTokens(tokens) {
  const data = [];
  let previousLine = 0;
  let previousStart = 0;
  for (const token of tokens) {
    const type = TOKEN_TYPES.indexOf(token.type);
    if (type < 0 || token.length < 1) continue;
    const deltaLine = token.line - previousLine;
    data.push(deltaLine, deltaLine ? token.start : token.start - previousStart, token.length, type, 0);
    previousLine = token.line;
    previousStart = token.start;
  }
  return { data };
}

function createLanguageServer(connection) {
  const documents = new lsp.TextDocuments(TextDocument);
  let servicePromise;
  let referenceIndex;
  const document = (uri) => documents.get(uri);
  const name = (item) => item.uri.startsWith("file:") ? fileURLToPath(item.uri) : item.uri;
  const service = () => servicePromise;

  connection.onInitialize(async (params) => {
    const roots = (params.workspaceFolders || []).map(({ uri }) => uri.startsWith("file:") ? fileURLToPath(uri) : "").filter(Boolean);
    if (!roots.length && params.rootUri?.startsWith("file:")) roots.push(fileURLToPath(params.rootUri));
    const workspace = new NodeWorkspace(roots);
    referenceIndex = new WorkspaceReferenceIndex(workspace);
    servicePromise = selectAnalyzer(params.initializationOptions || {}).then((analyzer) =>
      new LanguageService(analyzer, workspace));
    await servicePromise;
    return { serverInfo: { name: "cxpr-language-server", version: "0.1.0" }, capabilities: {
      textDocumentSync: lsp.TextDocumentSyncKind.Incremental, hoverProvider: true,
      completionProvider: { triggerCharacters: ["$", ".", "(", ","] }, definitionProvider: true,
      referencesProvider: true, documentSymbolProvider: true, foldingRangeProvider: true,
      semanticTokensProvider: { legend: { tokenTypes: TOKEN_TYPES, tokenModifiers: [] }, full: true }
    } };
  });

  async function publish(item) {
    connection.sendDiagnostics({ uri: item.uri,
      diagnostics: await (await service()).getDiagnostics(item.getText(), name(item)) });
  }
  documents.onDidOpen(({ document: item }) => {
    if (item.uri.startsWith("file:")) void referenceIndex.update(name(item), item.getText());
    void publish(item);
  });
  documents.onDidChangeContent(({ document: item }) => {
    if (item.uri.startsWith("file:")) void referenceIndex.update(name(item), item.getText());
    void publish(item);
  });
  documents.onDidClose(({ document: item }) => connection.sendDiagnostics({ uri: item.uri, diagnostics: [] }));

  connection.onCompletion(async ({ textDocument, position }) => {
    const item = document(textDocument.uri); if (!item) return [];
    return (await (await service()).getCompletions(item.getText(), { line: position.line, column: position.character }, name(item)))
      .map((value) => ({ label: value.label, detail: value.detail, insertText: value.insertText,
        kind: COMPLETION_KINDS[value.kind] || lsp.CompletionItemKind.Variable,
        textEdit: value.replaceRange ? { range: value.replaceRange, newText: value.insertText } : undefined }));
  });
  connection.onHover(async ({ textDocument, position }) => {
    const item = document(textDocument.uri); if (!item) return null;
    const value = await (await service()).getHover(item.getText(), { line: position.line, column: position.character }, name(item));
    await referenceIndex.ensure();
    const target = referenceIndex.targetAt(name(item), position);
    const usage = target?.declaration ? referenceIndex.references(target, false).length : 0;
    const markdown = value?.markdown ?? (target?.declaration
      ? `**${target.name}** \`CXPR ${target.declaration.kind}\`` : null);
    return markdown ? { contents: { kind: "markdown",
      value: markdown + (target?.declaration ? `\n\nUsed at ${usage} workspace location${usage === 1 ? "" : "s"}.` : "") },
      range: value?.range } : null;
  });
  connection.onReferences(async ({ textDocument, position, context }) => {
    const item = document(textDocument.uri); if (!item) return [];
    await referenceIndex.ensure();
    await referenceIndex.update(name(item), item.getText());
    const target = referenceIndex.targetAt(name(item), position);
    return target ? referenceIndex.references(target, context.includeDeclaration) : [];
  });
  connection.onDefinition(async ({ textDocument, position }) => {
    const item = document(textDocument.uri); if (!item) return [];
    return (await (await service()).getDefinitions(item.getText(), { line: position.line, column: position.character }, name(item)))
      .map((value) => ({ uri: value.uri.startsWith("file:") ? value.uri : pathToFileURL(value.uri).toString(), range: value.range }));
  });
  connection.onDocumentSymbol(async ({ textDocument }) => {
    const item = document(textDocument.uri); if (!item) return [];
    return (await (await service()).getDocumentSymbols(item.getText(), name(item))).map((value) => ({ ...value,
      kind: SYMBOL_KINDS[value.kind] || lsp.SymbolKind.Variable }));
  });
  connection.onFoldingRanges(async ({ textDocument }) => {
    const item = document(textDocument.uri); return item ? (await service()).getFoldingRanges(item.getText(), name(item)) : [];
  });
  connection.languages.semanticTokens.on(async ({ textDocument }) => {
    const item = document(textDocument.uri); return item ? encodeSemanticTokens(await (await service()).getSemanticTokens(item.getText(), name(item))) : { data: [] };
  });
  documents.listen(connection);
  return { listen: () => connection.listen() };
}

module.exports = { CliAnalyzer, NodeWorkspace, WorkspaceReferenceIndex, TOKEN_TYPES,
  createLanguageServer, encodeSemanticTokens, selectAnalyzer };
