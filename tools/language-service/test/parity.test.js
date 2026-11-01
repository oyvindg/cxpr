const assert = require("node:assert/strict")
const test = require("node:test")
const native = require("../../../../cxpr-node/dist")
const { LanguageService } = require("../dist")

const source = "model demo\nin close\nvalue = max(close, 1)\nout value\n"
const name = "/tmp/parity.cxpr"
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

test("retained parity corpus covers the authoritative language-service features", async () => {
  assert.deepEqual(await service.getDiagnostics(source, name), [])

  const completions = await service.getCompletions(source, { line: 2, column: 9 }, name)
  assert.ok(completions.some(({ label }) => label === "max"))
  assert.ok(completions.every(({ label }) => label.startsWith("m")))

  const hover = await service.getHover(source, { line: 2, column: 9 }, name)
  assert.match(hover.markdown, /\*\*max\*\*/)
  assert.match(hover.markdown, /Returns `number`/)

  assert.deepEqual(await service.getDefinitions(source, { line: 2, column: 14 }, name), [{
    uri: name,
    range: { start: { line: 1, character: 3 }, end: { line: 1, character: 8 } }
  }])

  assert.deepEqual((await service.getDocumentSymbols(source, name)).map(({ name, kind }) => ({ name, kind })), [
    { name: "demo", kind: "model" },
    { name: "close", kind: "input" },
    { name: "value", kind: "binding" },
    { name: "value", kind: "output" }
  ])

  const tokens = await service.getSemanticTokens(source, name)
  assert.ok(tokens.some((token) => token.type === "function" && token.line === 2 && token.start === 8))
  assert.ok(tokens.some((token) => token.type === "input" && token.line === 2 && token.start === 12))
  for (let index = 1; index < tokens.length; index += 1) {
    const previous = tokens[index - 1]
    const token = tokens[index]
    assert.equal(previous.line === token.line && token.start < previous.start + previous.length, false,
      `overlapping semantic tokens: ${JSON.stringify(previous)} / ${JSON.stringify(token)}`)
  }
})
