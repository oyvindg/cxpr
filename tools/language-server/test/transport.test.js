const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { encodeSemanticTokens, NodeWorkspace, selectAnalyzer } = require("../lsp-server");

test("native transport is injected in-process", async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), "cxpr-lsp-native-"));
  const modulePath = path.join(directory, "analyzer.cjs");
  fs.writeFileSync(modulePath, `
    class CxprNodeAnalyzer {
      async analyzeDocument(source, name) { return { ok: true, source, name }; }
      async resolveAt() { return null; }
      async completeAt() { return []; }
      async queryFunctions() { return []; }
    }
    module.exports = { CxprNodeAnalyzer };
  `);
  try {
    const analyzer = await selectAnalyzer({ transport: "native", cxprNodeModule: modulePath });
    assert.deepEqual(await analyzer.analyzeDocument("model demo", "demo.cxpr"),
      { ok: true, source: "model demo", name: "demo.cxpr" });
  } finally {
    fs.rmSync(directory, { recursive: true, force: true });
  }
});

test("semantic DTOs are encoded only in the LSP adapter", () => {
  assert.deepEqual(encodeSemanticTokens([
    { line: 1, start: 2, length: 3, type: "function" },
    { line: 1, start: 8, length: 2, type: "number" }
  ]), { data: [1, 2, 3, 0, 0, 0, 6, 2, 15, 0] });
});

test("workspace resolves canonical Dynasty cxpr library imports", async () => {
  const root = path.resolve(__dirname, "../../../../..");
  const workspace = new NodeWorkspace([root]);
  const target = await workspace.resolveImport("indicators/adx", path.join(root, "demo.cxpr"));
  assert.equal(target.path, path.join(root, "libs/dyn/cxpr/indicators/adx.cxpr"));
  assert.match(target.source, /model adx/);
});

test("bundled WASM transport accepts explicit isolated artifact paths", async () => {
  const cxprNode = path.resolve(__dirname, "../../../../cxpr-node");
  const analyzer = await selectAnalyzer({
    transport: "wasm",
    wasmAnalyzerModule: path.join(cxprNode, "dist", "wasm-analyzer.js"),
    wasmFactoryModule: path.join(cxprNode, "wasm", "dist", "cxpr_wasm.js"),
    wasmPath: path.join(cxprNode, "wasm", "dist", "cxpr_wasm.wasm")
  });
  const analysis = await analyzer.analyzeDocument(
    "model bundled\nin close\nvalue = rsi(close, 14)\nout value\n", "bundled.cxpr");
  assert.equal(analysis.ok, true);
  assert.deepEqual(analysis.diagnostics, []);
  assert.ok((await analyzer.completeAt("model bundled\nin close\nout x = sq", "bundled.cxpr",
    { line: 2, column: 10 })).some((item) => item.name === "sqrt"));
  assert.equal((await analyzer.resolveAt("model bundled\nin close\nout x = sqrt(close)", "bundled.cxpr",
    { line: 2, column: 10 })).name, "sqrt");
});
