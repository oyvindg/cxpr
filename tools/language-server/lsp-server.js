const fs = require("node:fs/promises");
const path = require("node:path");
const { spawn } = require("node:child_process");
const { fileURLToPath, pathToFileURL } = require("node:url");
const lsp = require("vscode-languageserver/node");
const { TextDocument } = require("vscode-languageserver-textdocument");
const { LanguageService } = require("../language-service/dist/index.js");

const TOKEN_TYPES = [
  "function", "type", "import", "importPath", "parameter", "namedArgument", "assignment",
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
    const name = spec.endsWith(".cxpr") ? spec : `${spec}.cxpr`;
    const candidates = [
      path.resolve(path.dirname(from), name),
      ...this.roots.flatMap((root) => [
        path.resolve(root, name),
        path.resolve(root, "libs/dyn/cxpr", name)
      ])
    ];
    for (const candidate of candidates) {
      try { return { path: candidate, source: await this.readFile(candidate) }; } catch {}
    }
    return null;
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
      ? { locateFile: (name) => name.endsWith(".wasm") ? path.resolve(wasmPath) : name }
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
  const document = (uri) => documents.get(uri);
  const name = (item) => item.uri.startsWith("file:") ? fileURLToPath(item.uri) : item.uri;
  const service = () => servicePromise;

  connection.onInitialize(async (params) => {
    const roots = (params.workspaceFolders || []).map(({ uri }) => uri.startsWith("file:") ? fileURLToPath(uri) : "").filter(Boolean);
    if (!roots.length && params.rootUri?.startsWith("file:")) roots.push(fileURLToPath(params.rootUri));
    servicePromise = selectAnalyzer(params.initializationOptions || {}).then((analyzer) =>
      new LanguageService(analyzer, new NodeWorkspace(roots)));
    await servicePromise;
    return { serverInfo: { name: "cxpr-language-server", version: "0.1.0" }, capabilities: {
      textDocumentSync: lsp.TextDocumentSyncKind.Incremental, hoverProvider: true,
      completionProvider: { triggerCharacters: ["$", ".", "(", ","] }, definitionProvider: true,
      documentSymbolProvider: true, foldingRangeProvider: true,
      semanticTokensProvider: { legend: { tokenTypes: TOKEN_TYPES, tokenModifiers: [] }, full: true }
    } };
  });

  async function publish(item) {
    connection.sendDiagnostics({ uri: item.uri,
      diagnostics: await (await service()).getDiagnostics(item.getText(), name(item)) });
  }
  documents.onDidOpen(({ document: item }) => void publish(item));
  documents.onDidChangeContent(({ document: item }) => void publish(item));
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
    return value ? { contents: { kind: "markdown", value: value.markdown }, range: value.range } : null;
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

module.exports = { CliAnalyzer, NodeWorkspace, TOKEN_TYPES, createLanguageServer, encodeSemanticTokens, selectAnalyzer };
