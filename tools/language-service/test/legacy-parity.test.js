const assert = require("node:assert/strict")
const test = require("node:test")
const { pathToFileURL } = require("node:url")
const native = require("../../../../cxpr-node/dist")
const { LanguageService } = require("../dist")
const legacy = require("../legacy/source-language-features")
const vscode = require("../legacy/vscode-lsp-shim")

const source = "model demo\nin close\nvalue = max(close, 1)\nout value\n"
const name = "/tmp/cxpr-language-parity/model.cxpr"
const workspace = {
  async readFile() { return "" },
  importDir() { return "" },
  async resolveImport() { return null }
}
const analyzer = {
  async analyzeDocument(text, file) { return native.analyzeDocument(text, file) },
  async resolveAt(text, file, position) { return native.resolveAt(text, file, position) },
  async completeAt(text, file, position) {
    const result = native.completeAt(text, file, position)
    if (!result.ok) throw new Error(result.error || "completion failed")
    return result.candidates || []
  },
  async queryFunctions() { return native.queryFunctions() }
}
const service = new LanguageService(analyzer, workspace)

function document(text, file) {
  const lines = text.split(/\r?\n/)
  return {
    uri: vscode.Uri.file(file),
    languageId: "cxpr",
    lineCount: lines.length,
    getText(range) {
      if (!range) return text
      if (range.start.line === range.end.line) {
        return (lines[range.start.line] || "").slice(range.start.character, range.end.character)
      }
      return lines.slice(range.start.line, range.end.line + 1).map((line, index, selected) =>
        line.slice(index === 0 ? range.start.character : 0,
          index === selected.length - 1 ? range.end.character : undefined)).join("\n")
    },
    lineAt(line) { return { text: lines[line] || "" } },
    getWordRangeAtPosition(position, pattern = /[A-Za-z_][A-Za-z0-9_]*/) {
      const expression = new RegExp(pattern.source, pattern.flags.replace("g", "") + "g")
      for (const match of (lines[position.line] || "").matchAll(expression)) {
        if (position.character >= match.index && position.character <= match.index + match[0].length) {
          return new vscode.Range(position.line, match.index, position.line, match.index + match[0].length)
        }
      }
      return undefined
    }
  }
}

function normalizedDiagnostics(items) {
  return items.map((item) => ({
    severity: item.severity || 1,
    message: item.message,
    line: item.range.start.line,
    character: item.range.start.character
  })).sort((left, right) => left.line - right.line || left.character - right.character ||
    left.message.localeCompare(right.message))
}

// Historical server mapping for analyzer diagnostics. Range length is presentation-only.
function legacyDiagnostics(analysis) {
  const items = (analysis.diagnostics || []).map((item) => ({
    severity: item.severity || 1,
    source: "cxpr",
    message: item.message,
    range: { start: { line: item.line || 0, character: item.column || 0 },
      end: { line: item.line || 0, character: (item.column || 0) + 1 } }
  }))
  if (!analysis.ok) {
    const line = Math.max(0, (analysis.line || 1) - 1)
    const character = analysis.column || 0
    items.push({ severity: 1, source: "cxpr", message: analysis.error || "CXPR parse failed",
      range: { start: { line, character }, end: { line, character: character + 1 } } })
  }
  return items
}

function prefixAt(text, position) {
  const line = text.split(/\r?\n/)[position.line] || ""
  let start = Math.min(position.column, line.length)
  while (start > 0 && /[A-Za-z0-9_$]/.test(line[start - 1])) start -= 1
  return line.slice(start, position.column)
}

// The legacy server returned every analyzer function/symbol. C now filters by context and prefix.
function normalizedLegacyCompletions(analysis, text, position) {
  const prefix = prefixAt(text, position)
  const labels = [...(analysis.functions || []).map(({ name: label }) => label),
    ...(analysis.outline || []).map(({ name: label }) => label)]
  return [...new Set(labels)].filter((label) => label.startsWith(prefix)).sort()
}

function normalizedHover(value) {
  if (!value) return null
  const markdown = value.markdown || value.contents?.value || ""
  const symbol = /\*\*([^*]+)\*\*/.exec(markdown)?.[1] || ""
  const returns = /Returns(?:\s*:\s*|\s+)`(double|number|bool|boolean|string)`/i.exec(markdown)?.[1]
  return { symbol, returns: ({ double: "number", bool: "boolean" })[returns] || returns || "" }
}

function normalizedDefinition(value) {
  const item = Array.isArray(value) ? value[0] : value
  if (!item) return null
  const uri = typeof item.uri === "string" ? item.uri : item.uri.toString()
  return { path: uri.startsWith("file:") ? new URL(uri).pathname : uri,
    line: item.range.start.line, character: item.range.start.character }
}

function normalizedSymbols(items) {
  return items.map(({ name: symbolName, kind, range }) => ({
    name: symbolName, kind, line: range.start.line, character: range.start.character
  }))
}

function normalizedTokens(items) {
  const symbolTypes = new Set(["input", "reference", "variable", "assignment"])
  return items.map((item) => ({ ...item, type: symbolTypes.has(item.type) ? "symbol" : item.type }))
    .sort((left, right) => left.line - right.line || left.start - right.start || left.length - right.length)
}

test("legacy and neutral service retain normalized feature parity", async () => {
  const analysis = native.analyzeDocument(source, name)
  const oldDocument = document(source, name)

  assert.deepEqual(normalizedDiagnostics(await service.getDiagnostics(source, name)),
    normalizedDiagnostics(legacyDiagnostics(analysis)))
  const invalidSource = "model broken\nvalue = (\n"
  const invalidAnalysis = native.analyzeDocument(invalidSource, name)
  assert.equal(invalidAnalysis.ok, false)
  assert.deepEqual(normalizedDiagnostics(await service.getDiagnostics(invalidSource, name)),
    normalizedDiagnostics(legacyDiagnostics(invalidAnalysis)))

  const completionPosition = { line: 2, column: 10 }
  const currentCompletions = (await service.getCompletions(source, completionPosition, name))
    .map(({ label }) => label).sort()
  assert.deepEqual(currentCompletions,
    normalizedLegacyCompletions(analysis, source, completionPosition))

  const oldHover = new legacy.CxprHoverProvider().provideHover(oldDocument, { line: 2, character: 9 })
  assert.deepEqual(normalizedHover(await service.getHover(source, { line: 2, column: 9 }, name)),
    normalizedHover(oldHover))

  const oldDefinition = new legacy.CxprDefinitionProvider()
    .provideDefinition(oldDocument, { line: 2, character: 14 })
  assert.deepEqual(normalizedDefinition(await service.getDefinitions(source, { line: 2, column: 14 }, name)),
    normalizedDefinition(oldDefinition))

  const oldSymbols = (analysis.outline || []).map((item) => ({
    name: item.name || item.kind, kind: item.kind,
    range: { start: { line: item.startLine, character: item.startChar } }
  }))
  assert.deepEqual(normalizedSymbols(await service.getDocumentSymbols(source, name)),
    normalizedSymbols(oldSymbols))

  assert.deepEqual(normalizedTokens(await service.getSemanticTokens(source, name)),
    normalizedTokens(legacy.buildDocumentTokens(source)))
})

test("legacy oracle is isolated from production exports", () => {
  const entry = require("../dist")
  assert.equal(Object.values(entry).includes(legacy), false)
  assert.equal(pathToFileURL(require.resolve("../legacy/source-language-features")).pathname
    .includes("/legacy/"), true)
})
