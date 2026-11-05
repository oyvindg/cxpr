const assert = require("node:assert/strict");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");
const { fileURLToPath } = require("node:url");
const { encodeSemanticTokens, NodeWorkspace, WorkspaceReferenceIndex, TOKEN_TYPES,
  selectAnalyzer } = require("../lsp-server");

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
  ]), { data: [1, 2, 3, 0, 0, 0, 6, 2, TOKEN_TYPES.indexOf("number"), 0] });
});

test("workspace resolves canonical Dynasty cxpr library imports", async () => {
  const root = path.resolve(__dirname, "../../../../..");
  const workspace = new NodeWorkspace([root]);
  const target = await workspace.resolveImport("indicators.adx", path.join(root, "demo.cxpr"));
  assert.equal(target.path, path.join(root, "libs/dyn/cxpr/indicators/adx.cxpr"));
  assert.match(target.source, /model adx/);
});

test("workspace resolves modules from the nearest ancestor module root", async () => {
  const root = path.resolve(__dirname, "../../../../..");
  const workspace = new NodeWorkspace([root]);
  const song = path.join(root,
    "libs/audio/examples/songs/neon_highway/song.cxpr");
  const target = await workspace.resolveImport(
    "instruments.retrowave.arp", song);
  assert.equal(target.path, path.join(root,
    "libs/audio/modules/instruments/retrowave/arp.cxpr"));
  assert.match(target.source, /model arp/);
});

test("reference index keeps identical module names isolated by nearest root", async () => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), "cxpr-reference-roots-"));
  try {
    for (const project of ["alpha", "beta"]) {
      fs.mkdirSync(path.join(root, project, "modules", "instruments"), { recursive: true });
      fs.mkdirSync(path.join(root, project, "songs"), { recursive: true });
      fs.writeFileSync(path.join(root, project, "modules", "instruments", "arp.cxpr"),
        `model arp\nin value\nout sample = value\n`);
      fs.writeFileSync(path.join(root, project, "songs", "song.cxpr"),
        `model song\nuse arp from instruments\nout sample = arp(value=1)\n`);
    }
    const index = new WorkspaceReferenceIndex(new NodeWorkspace([root]));
    await index.ensure();
    const declaration = path.join(root, "alpha", "modules", "instruments", "arp.cxpr");
    const target = index.targetAt(declaration, { line: 0, character: 7 });
    const references = index.references(target, false);
    assert.equal(references.length, 2);
    assert.ok(references.every((item) => fileURLToPath(item.uri).includes(`${path.sep}alpha${path.sep}`)));
  } finally {
    fs.rmSync(root, { recursive: true, force: true });
  }
});

test("language service resolves the complete nested modular synth graph", async () => {
  const root = path.resolve(__dirname, "../../../../..");
  const file = path.join(root,
    "libs/audio/examples/songs/modular_synth/song.cxpr");
  const analyzer = await selectAnalyzer({
    transport: "native",
    cxprNodeModule: path.join(root, "libs/cxpr-node/dist/index.js")
  });
  const { LanguageService } = require("../../language-service/dist/index.js");
  const service = new LanguageService(analyzer, new NodeWorkspace([root]));
  assert.deepEqual(await service.getDiagnostics(fs.readFileSync(file, "utf8"), file), []);
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
