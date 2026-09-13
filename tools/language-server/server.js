#!/usr/bin/env node

const fs = require("fs");
const path = require("path");
const { execFile } = require("child_process");
const { fileURLToPath, pathToFileURL } = require("url");
const lsp = require("vscode-languageserver/node");
const { TextDocument } = require("vscode-languageserver-textdocument");

const TOKEN_TYPES = [
  "function", "type", "import", "importPath", "parameter", "namedArgument",
  "assignment", "block", "plotBlock", "property", "reference", "input",
  "state", "variable", "string", "number", "operator", "keyword",
  "punctuation", "comment"
];
const SYMBOL_KINDS = {
  function: lsp.SymbolKind.Function,
  input: lsp.SymbolKind.Variable,
  param: lsp.SymbolKind.Constant,
  state: lsp.SymbolKind.Variable,
  use: lsp.SymbolKind.Namespace,
  model: lsp.SymbolKind.Module,
  hostBlock: lsp.SymbolKind.Object,
  binding: lsp.SymbolKind.Variable,
  output: lsp.SymbolKind.Variable
};

function asRange(item) {
  const startLine = item.startLine || 0;
  const startChar = item.startChar || 0;
  return {
    start: { line: startLine, character: startChar },
    end: {
      line: item.endLine === undefined ? startLine : item.endLine,
      character: item.endChar === undefined ? startChar : item.endChar
    }
  };
}

function findTooling(configured, roots) {
  const candidates = [configured, process.env.CXPR_DOCUMENT_TOOLING];
  for (const root of roots) {
    candidates.push(
      path.join(root, "build", "libs", "cxpr", "cxpr_document_tooling"),
      path.join(root, "build", "libs", "cxpr", "Debug", "cxpr_document_tooling"),
      path.join(root, "build", "libs", "cxpr", "Release", "cxpr_document_tooling")
    );
  }
  return candidates.find((candidate) => candidate && fs.existsSync(candidate)) || "";
}

function analyze(executable, document, cwd) {
  return new Promise((resolve) => {
    if (!executable) {
      resolve({ ok: false, error: "cxpr_document_tooling executable not found", line: 1, column: 0 });
      return;
    }
    const child = execFile(executable, ["--source-name", fileURLToPath(document.uri)], {
      cwd, encoding: "utf8", maxBuffer: 16 * 1024 * 1024
    }, (error, stdout) => {
      try {
        resolve(JSON.parse(stdout || "{}"));
      } catch {
        resolve({ ok: false, error: error ? error.message : "Invalid CXPR tooling response", line: 1, column: 0 });
      }
    });
    child.stdin.end(document.getText());
  });
}

function wordAt(document, position) {
  const line = document.getText().split(/\r?\n/)[position.line] || "";
  let start = position.character;
  let end = position.character;
  while (start > 0 && /[A-Za-z0-9_$]/.test(line[start - 1])) start--;
  while (end < line.length && /[A-Za-z0-9_$]/.test(line[end])) end++;
  return line.slice(start, end);
}

function sourcePathForUse(document, usePath, roots) {
  const normalized = usePath.endsWith(".cxpr") ? usePath : `${usePath}.cxpr`;
  const current = path.dirname(fileURLToPath(document.uri));
  const candidates = [
    path.join(current, normalized),
    ...roots.map((root) => path.join(root, normalized)),
    ...roots.map((root) => path.join(root, "libs", "dyn", "cxpr", normalized)),
    ...roots.map((root) => path.join(root, "libs", "cxpr", normalized))
  ];
  return candidates.find((candidate) => fs.existsSync(candidate)) || "";
}

function useTargets(source) {
  const targets = new Map();
  for (const match of source.matchAll(/^\s*use\s+(.*?)\s+from\s+([A-Za-z0-9_./-]+)/gm)) {
    const names = match[1].replace(/[{}]/g, "").split(",").map((name) => name.trim()).filter(Boolean);
    for (const name of names) {
      const parts = name.split(/\s+as\s+/);
      targets.set(parts[1] || parts[0], `${match[2].replace(/\/$/, "")}/${parts[0]}`);
    }
  }
  return targets;
}

function functionSignature(source, name) {
  const match = source.match(new RegExp(`(?:^|\\n)\\s*fn\\s+${name}\\s*\\([^\\n]*\\)`));
  return match ? match[0].trim().replace(/^fn\\s+/, "") : "";
}

function definitionInSource(source, name) {
  const match = new RegExp(`(?:^|\\n)(\\s*(?:fn\\s+)?${name}\\b)`,'m').exec(source);
  if (!match) return null;
  const before = source.slice(0, match.index + match[0].indexOf(match[1]));
  const line = before.split(/\r?\n/).length - 1;
  const character = before.length - before.lastIndexOf("\n") - 1;
  return { line, character };
}

function recordFieldNames(source) {
  const fields = new Set();
  for (const block of source.matchAll(/\bout\s*\{([^}]*)\}/gs)) {
    for (const field of block[1].matchAll(/\b([A-Za-z_][A-Za-z0-9_]*)\b/g)) fields.add(field[1]);
  }
  return fields;
}

function receiverFunction(source, receiver) {
  const match = new RegExp(`\\b${receiver}\\s*=\\s*([A-Za-z_][A-Za-z0-9_]*)\\s*\\(`).exec(source);
  return match ? match[1] : "";
}

function receiverAt(document, position) {
  const line = document.getText().split(/\r?\n/)[position.line] || "";
  return line.slice(0, position.character).match(/([A-Za-z_][A-Za-z0-9_]*)\s*\.\s*$/)?.[1] || "";
}

function lexicalTokens(document, result) {
  if (!document) return [];
  const known = new Map();
  for (const item of result?.outline || []) {
    if (item.name) known.set(item.name, item.kind === "input" ? "input" : item.kind === "param" ? "parameter" : "variable");
  }
  for (const item of result?.functions || []) known.set(item.name, "function");
  for (const signature of document.getText().matchAll(/\bfn\s+[A-Za-z_][A-Za-z0-9_]*\s*\(([^)]*)\)/g)) {
    for (const parameter of signature[1].matchAll(/[A-Za-z_][A-Za-z0-9_]*/g)) known.set(parameter[0], "variable");
  }
  const keywords = new Set(["and", "as", "or", "not", "if", "then", "else", "fn", "from", "in", "initial", "out", "return", "state", "update", "use", "true", "false", "null"]);
  const tokens = [];
  document.getText().split(/\r?\n/).forEach((line, lineNumber) => {
    const code = line.replace(/#.*/, "");
    if (/^\s*(?:use|model|fn)\b/.test(code)) return;
    for (const match of code.matchAll(/\$?[A-Za-z_][A-Za-z0-9_]*|(?:\d+(?:\.\d+)?|\.\d+)/g)) {
      const word = match[0];
      const start = match.index;
      // TextMate owns string literals (including duration values such as
      // "-1h"); do not overlay numeric/identifier semantic tokens inside.
      const before = code.slice(0, start);
      if ((before.match(/"/g) || []).length % 2 === 1 ||
          (before.match(/'/g) || []).length % 2 === 1) continue;
      let type = word[0] === "$" ? "parameter" : known.get(word);
      if (!type && keywords.has(word)) type = "keyword";
      if (!type && /\b(?:bool|buffer|int|number|series)\b/.test(word)) type = "type";
      if (!type && /^\d|^\.\d/.test(word)) type = "number";
      if (!type) continue;
      tokens.push({ line: lineNumber, start, length: word.length, type });
    }
  });
  return tokens;
}

function createServer(connection) {
  const documents = new lsp.TextDocuments(TextDocument);
  const cache = new Map();
  let roots = [];
  let executable = "";

  async function resultFor(uri) {
    const document = documents.get(uri);
    if (!document) return null;
    const key = `${uri}@${document.version}`;
    if (!cache.has(key)) cache.set(key, analyze(executable, document, roots[0] || process.cwd()));
    return cache.get(key);
  }

  async function publish(document) {
    const result = await resultFor(document.uri);
    const diagnostics = (result?.diagnostics || []).map((diagnostic) => {
      // The C typechecker may not have a source span for synthetic nodes.
      // Prefer the quoted offending operand in its message in that case.
      let line = diagnostic.line || 0;
      let character = diagnostic.column || 0;
      let spanMatch = null;
      if (line === 0 && character <= 4 || /duration|index|source/i.test(String(diagnostic.message || ""))) {
        spanMatch = String(diagnostic.message || "").match(/(?:argument|index expression) '([^']+)'/i) ||
          String(diagnostic.message || "").match(/'(-?\d+(?:\.\d+)?[A-Za-z]*)'/i) ||
          (String(diagnostic.message || "").match(/resample index/i) &&
            document.getText().match(/\[\s*(-?(?:\d+(?:\.\d+)?|\.\d+)(?:[eE][+-]?\d+)?)\s*\]/)) ||
          (String(diagnostic.message || "").match(/resample source/i) &&
            document.getText().match(/\bresample\(\s*([^,]+?)(?=\s*,)/i)) ||
          (String(diagnostic.message || "").match(/duration/i) &&
            document.getText().match(/\bevery\s*=\s*"([^"]+)"/i));
        if (spanMatch) {
          const offset = document.getText().indexOf(spanMatch[1]);
          if (offset >= 0) {
            const prefix = document.getText().slice(0, offset);
            line = prefix.split("\n").length - 1;
            character = offset - prefix.lastIndexOf("\n") - 1;
          }
        }
      }
      return {
      severity: diagnostic.severity || lsp.DiagnosticSeverity.Error,
      source: "cxpr",
      message: diagnostic.message,
      range: {
        start: { line, character },
        end: { line, character: character + (spanMatch ? spanMatch[1].length : 1) }
      }
      };
    });
    if (result && !result.ok) {
      let errorLine = Math.max(0, (result.line || 1) - 1);
      let errorColumn = result.column || 0;
      let errorLength = 1;
      if (/Invalid declaration type/i.test(result.error || "")) {
        const text = document.getText().split(/\r?\n/)[errorLine] || "";
        const match = text.match(/:\s*([A-Za-z_][A-Za-z0-9_]*)/);
        if (match) { errorColumn = match.index + match[0].indexOf(match[1]); errorLength = match[1].length; }
      }
      diagnostics.push({
      severity: lsp.DiagnosticSeverity.Error,
      source: "cxpr",
      message: result.error || "CXPR parse failed",
      range: {
        start: { line: errorLine, character: errorColumn },
        end: { line: errorLine, character: errorColumn + errorLength }
      }
      });
    }
    connection.sendDiagnostics({ uri: document.uri, diagnostics });
  }

  connection.onInitialize((params) => {
    roots = (params.workspaceFolders || []).map(({ uri }) =>
      uri.startsWith("file:") ? fileURLToPath(uri) : "").filter(Boolean);
    if (!roots.length && params.rootUri?.startsWith("file:")) roots = [fileURLToPath(params.rootUri)];
    executable = findTooling(params.initializationOptions?.toolingExecutable, roots);
    return {
      serverInfo: { name: "cxpr-language-server", version: "0.1.0" },
      capabilities: {
        textDocumentSync: lsp.TextDocumentSyncKind.Incremental,
        hoverProvider: true,
        definitionProvider: true,
        documentSymbolProvider: true,
        foldingRangeProvider: true,
        semanticTokensProvider: { legend: { tokenTypes: TOKEN_TYPES, tokenModifiers: [] }, full: true }
      }
    };
  });

  documents.onDidOpen(({ document }) => { void publish(document); });
  documents.onDidChangeContent(({ document }) => {
    for (const key of cache.keys()) if (key.startsWith(`${document.uri}@`)) cache.delete(key);
    void publish(document);
  });
  documents.onDidClose(({ document }) => connection.sendDiagnostics({ uri: document.uri, diagnostics: [] }));

  connection.onDocumentSymbol(async ({ textDocument }) => {
    const result = await resultFor(textDocument.uri);
    return (result?.outline || []).map((item) => ({
      name: item.name || item.kind,
      detail: item.value || "",
      kind: SYMBOL_KINDS[item.kind] || lsp.SymbolKind.Variable,
      range: asRange(item),
      selectionRange: asRange(item)
    }));
  });
  connection.onFoldingRanges(async ({ textDocument }) => {
    const result = await resultFor(textDocument.uri);
    return (result?.folds || []).map((item) => ({
      startLine: item.startLine, startCharacter: item.startChar,
      endLine: item.endLine, endCharacter: item.endChar, kind: "region"
    }));
  });
  connection.languages.semanticTokens.on(async ({ textDocument }) => {
    const result = await resultFor(textDocument.uri);
    const tokens = (result?.tokens || []).concat(lexicalTokens(documents.get(textDocument.uri), result))
      .filter((token, index, all) => all.findIndex((candidate) => candidate.line === token.line && candidate.start === token.start && candidate.length === token.length) === index)
      .slice().sort((a, b) => a.line - b.line || a.start - b.start);
    const data = [];
    let previousLine = 0;
    let previousStart = 0;
    for (const token of tokens) {
      // The document AST currently reports a whole binding as `assignment`.
      // Let the CXPR TextMate grammar color binding expressions so that this
      // coarse span does not turn every binding name dark blue.
      // Function/assignment AST spans cover the complete declaration line
      // (including parameters and operators). TextMate has the precise
      // sub-token scopes for these declarations, so coarse LSP spans would
      // incorrectly paint `from`, parameters and `=` as function colors.
      if (token.type === "assignment" || token.type === "function") continue;
      const type = TOKEN_TYPES.indexOf(token.type);
      if (type < 0 || token.length < 1) continue;
      const deltaLine = token.line - previousLine;
      data.push(deltaLine, deltaLine ? token.start : token.start - previousStart, token.length, type, 0);
      previousLine = token.line;
      previousStart = token.start;
    }
    return { data };
  });
  connection.onDefinition(async ({ textDocument, position }) => {
    const result = await resultFor(textDocument.uri);
    const document = documents.get(textDocument.uri);
    let word = document ? wordAt(document, position) : "";
    if (document && !word) {
      const line = document.getText().split(/\r?\n/)[position.line] || "";
      const m = line.slice(0, position.character + 1).match(/[A-Za-z_][A-Za-z0-9_]*$/);
      if (m) word = m[0];
    }
    const typeDocs = {
      number: "A scalar numeric value (typically a price, indicator, or calculation result).",
      bool: "A boolean value: `true` or `false`.",
      int: "An integer-valued number, used where fractional values are not allowed.",
      string: "A text value enclosed in quotes.",
      series: "A sequence of values indexed by time.",
      buffer: "A bounded historical buffer of values, optionally configured with a sample count."
    };
    if (typeDocs[word]) return { contents: { kind: "markdown", value: `**${word}**\n\n${typeDocs[word]}` } };
    const symbol = (result?.outline || []).find((item) => item.name === word || `$${item.name}` === word);
    if (symbol) return { uri: textDocument.uri, range: asRange(symbol) };
    if (!document) return null;
    const lineText = document.getText().split(/\r?\n/)[position.line] || "";
    if (lineText.slice(0, position.character).endsWith(".")) {
      const receiver = receiverAt(document, position);
      const fnName = receiverFunction(document.getText(), receiver);
      const targetPath = fnName ? sourcePathForUse(document, useTargets(document.getText()).get(fnName) || "", roots) : "";
      const source = targetPath ? fs.readFileSync(targetPath, "utf8") : document.getText();
      if (recordFieldNames(source).has(word)) {
        const location = definitionInSource(source, word);
        if (location) return { uri: targetPath ? pathToFileURL(targetPath).toString() : textDocument.uri, range: {
          start: location, end: { line: location.line, character: location.character + word.length }
        } };
      }
    }
    const targets = useTargets(document.getText());
    const usePath = targets.get(word);
    if (!usePath) return null;
    const target = sourcePathForUse(document, usePath, roots);
    const source = target ? fs.readFileSync(target, "utf8") : "";
    const location = definitionInSource(source, word);
    return location ? { uri: pathToFileURL(target).toString(), range: {
      start: location, end: { line: location.line, character: location.character + word.length }
    } } : null;
  });
  connection.onHover(async ({ textDocument, position }) => {
    const document = documents.get(textDocument.uri);
    const word = document ? wordAt(document, position) : "";
    const typeDocs = {
      number: "A scalar numeric value.", bool: "A boolean value: `true` or `false`.",
      int: "An integer-valued number.", string: "A text value.",
      series: "A sequence of values indexed by time.",
      buffer: "A bounded historical value buffer."
    };
    if (typeDocs[word]) return { contents: { kind: "markdown", value: `**${word}**\n\n${typeDocs[word]}` } };
    const result = await resultFor(textDocument.uri);
    const symbol = (result?.outline || []).find((item) => item.name === word || `$${item.name}` === word);
    if (symbol) return { contents: { kind: "markdown", value: `**${word}** \`${symbol.kind}\`` } };
    if (document) {
      const lineText = document.getText().split(/\r?\n/)[position.line] || "";
      if (lineText.slice(0, position.character).endsWith(".")) {
        const receiver = receiverAt(document, position);
        const fnName = receiverFunction(document.getText(), receiver);
        const targetPath = fnName ? sourcePathForUse(document, useTargets(document.getText()).get(fnName) || "", roots) : "";
        const source = targetPath ? fs.readFileSync(targetPath, "utf8") : document.getText();
        if (recordFieldNames(source).has(word)) return { contents: { kind: "markdown", value: `**${word}** \`record field\`` } };
      }
    }
    const fn = (result?.functions || []).find((item) => item.name === word);
    if (!fn && document) {
      const targetPath = sourcePathForUse(document, useTargets(document.getText()).get(word) || "", roots);
      if (targetPath) {
        const signature = functionSignature(fs.readFileSync(targetPath, "utf8"), word);
        if (signature) return { contents: { kind: "markdown", value: `**${word}** \`CXPR function/import\`\n\n\`${signature}\`` } };
      }
    }
    if (!fn) return null;
    if (fn.signature) {
      return { contents: { kind: "markdown", value: `**${word}** \`CXPR function\`\n\n\`${fn.signature}\`` } };
    }
    const arity = fn.minArgs === fn.maxArgs
      ? `${fn.minArgs} argument${fn.minArgs === 1 ? "" : "s"}`
      : `${fn.minArgs}–${fn.maxArgs} arguments`;
    return {
      contents: {
        kind: "markdown",
        value: `**${word}** \`${fn.builtin ? "CXPR builtin" : "CXPR function/import"}\`\n\n${arity}`
      }
    };
  });

  documents.listen(connection);
  return { listen: () => connection.listen() };
}

if (require.main === module) createServer(lsp.createConnection(lsp.ProposedFeatures.all)).listen();

module.exports = { TOKEN_TYPES, asRange, createServer, findTooling, lexicalTokens, wordAt };
