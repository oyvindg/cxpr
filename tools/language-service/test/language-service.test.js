const assert = require("node:assert/strict")
const path = require("node:path")
const test = require("node:test")
const { LanguageService } = require("../dist")

const workspace = {
  async readFile() { return "" },
  importDir() { return "" },
  async resolveImport() { return null }
}

test("generic editor consumer only depends on the neutral service contract", async () => {
  const calls = []
  const analyzer = {
    async analyzeDocument() { return { ok: true, diagnostics: [], outline: [], folds: [], tokens: [] } },
    async resolveAt() { return { name: "max", kind: "function", builtin: true, type: "number", signature: "max(left, right)", documentation: "Returns the greater value." } },
    async completeAt() { return [{ name: "max", kind: "function", builtin: true, signature: "max(left, right)" }] },
    async queryFunctions() { return [] }
  }
  const service = new LanguageService(analyzer, workspace)
  const editor = {
    async update(cxprService) {
      calls.push(await cxprService.getCompletions("value = ma", { line: 0, column: 10 }, "memory.cxpr"))
      calls.push(await cxprService.getHover("value = max(1, 2)", { line: 0, column: 9 }, "memory.cxpr"))
    }
  }
  await editor.update(service)
  assert.equal(calls[0][0].label, "max")
  assert.match(calls[1].markdown, /max\(left, right\)/)
  assert.match(calls[1].markdown, /Returns the greater value\./)
  assert.equal(JSON.stringify(calls).includes("monaco"), false)
})

test("service accepts an async analyzer", async () => {
  let resumed = false
  const analyzer = {
    async analyzeDocument() { await Promise.resolve(); resumed = true; return { ok: true, diagnostics: [] } },
    async resolveAt() { return null }, async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const service = new LanguageService(analyzer, workspace)
  assert.deepEqual(await service.getDiagnostics("model demo", path.join("tmp", "demo.cxpr")), [])
  assert.equal(resumed, true)
})

test("host requirement diagnostics underline the complete requirement function", async () => {
  const analyzer = {
    async analyzeDocument() {
      return { ok: true, diagnostics: [{ severity: 1,
        message: "Dyn cannot bind resample source 'close' at interval '5m'", line: 4, column: 12 }] }
    },
    async resolveAt() { return null }, async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const service = new LanguageService(analyzer, workspace)
  const diagnostics = await service.getDiagnostics("", "strategy.cxpr")
  assert.deepEqual(diagnostics[0].range, {
    start: { line: 4, character: 12 }, end: { line: 4, character: 20 }
  })
})

test("definition searches exported symbols inside imported modules", async () => {
  const analyzer = {
    async analyzeDocument() { return { ok: true } },
    async resolveAt(source) {
      return source.includes("fn ts_window_span")
        ? { name: "ts_window_span", kind: "function", builtin: false,
          signature: "ts_window_span(left, right)",
          definition: { startLine: 5, startChar: 3, endLine: 5, endChar: 17 } }
        : { name: "ts_window_span", kind: "unresolved", builtin: false,
          imports: ["ts/pivots", "ts/window"] }
    },
    async completeAt() { return [] }, async queryFunctions() { return [] }
  }
  const requested = []
  const importWorkspace = {
    async readFile() { return "fn ts_window_span(left, right) = left + right" },
    importDir() { return "/workspace" },
    async resolveImport(spec) {
      requested.push(spec)
      return spec === "ts/window" ? { path: "/workspace/ts/window.cxpr" } : null
    }
  }
  const service = new LanguageService(analyzer, importWorkspace)
  assert.deepEqual(await service.getDefinitions("", { line: 20, column: 12 },
    "/workspace/trendline.cxpr"), [{
    uri: "/workspace/ts/window.cxpr",
    range: { start: { line: 5, character: 3 }, end: { line: 5, character: 17 } }
  }])
  assert.deepEqual(requested, ["ts/pivots", "ts/window"])
})
