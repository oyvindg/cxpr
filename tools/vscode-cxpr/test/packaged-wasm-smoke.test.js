const assert = require("node:assert/strict");
const { spawnSync } = require("node:child_process");
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const test = require("node:test");

class IsolatedWorkspace {
  async readFile(file) { return fs.promises.readFile(file, "utf8"); }
  importDir() { return "/isolated"; }
  async resolveImport() { return null; }
}

test("isolated VSIX serves diagnostics, completion, hover and definition through bundled WASM", async (t) => {
  const root = path.resolve(__dirname, "..");
  const pkg = JSON.parse(fs.readFileSync(path.join(root, "package.json"), "utf8"));
  const vsix = path.join(root, "dist", `${pkg.name}-${pkg.version}.vsix`);
  assert.ok(fs.existsSync(vsix), `missing ${vsix}; run npm run package:vsix`);

  const isolated = fs.mkdtempSync(path.join(os.tmpdir(), "cxpr-vsix-smoke-"));
  t.after(() => fs.rmSync(isolated, { recursive: true, force: true }));
  const unzip = spawnSync("unzip", ["-q", vsix, "-d", isolated], { encoding: "utf8" });
  assert.equal(unzip.status, 0, unzip.stderr);

  const extensionRoot = path.join(isolated, "extension");
  const requiredAssets = [
    "server/server.js",
    "runtime/language-service.js",
    "runtime/wasm-analyzer.js",
    "runtime/cxpr_wasm.js",
    "runtime/cxpr_wasm.wasm",
    "syntaxes/cxpr.tmLanguage.json",
    "syntaxes/cxpr.language-configuration.json",
    "theme/cxpr-color-profile.json"
  ];
  for (const asset of requiredAssets) {
    assert.ok(fs.existsSync(path.join(extensionRoot, asset)), `VSIX is missing ${asset}`);
  }

  const packagedGrammar = fs.readFileSync(
    path.join(extensionRoot, "syntaxes", "cxpr.tmLanguage.json"), "utf8");
  const canonicalGrammar = fs.readFileSync(
    path.join(root, "syntaxes", "cxpr.tmLanguage.json"), "utf8");
  assert.equal(packagedGrammar, canonicalGrammar, "VSIX grammar must be the canonical generated asset");

  const nativeAddons = [];
  function findNativeAddons(directory) {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) findNativeAddons(file);
      else if (entry.name.endsWith(".node")) nativeAddons.push(file);
    }
  }
  findNativeAddons(extensionRoot);
  assert.deepEqual(nativeAddons, [], "portable VSIX must not bundle platform-specific native addons");

  const runtime = path.join(isolated, "extension", "runtime");
  const { WasmAnalyzer } = require(path.join(runtime, "wasm-analyzer.js"));
  const factoryModule = require(path.join(runtime, "cxpr_wasm.js"));
  const { LanguageService } = require(path.join(runtime, "language-service.js"));
  const analyzer = await WasmAnalyzer.create(factoryModule.default || factoryModule, {
    locateFile: () => path.join(runtime, "cxpr_wasm.wasm")
  });
  const service = new LanguageService(analyzer, new IsolatedWorkspace());
  const source = "model smoke\nin close\nout x = sqrt(close)\n";

  assert.deepEqual(await service.getDiagnostics(source, "smoke.cxpr"), []);
  assert.ok((await service.getCompletions(source, { line: 2, column: 10 }, "smoke.cxpr"))
    .some((item) => item.label === "sqrt"));
  assert.match((await service.getHover(source, { line: 2, column: 10 }, "smoke.cxpr")).markdown,
    /sqrt/);
  assert.deepEqual(await service.getDefinitions(source, { line: 2, column: 16 }, "smoke.cxpr"), [{
    uri: "smoke.cxpr",
    range: { start: { line: 1, character: 3 }, end: { line: 1, character: 8 } }
  }]);
  const tokens = await service.getSemanticTokens(source, "smoke.cxpr");
  for (let index = 1; index < tokens.length; index += 1) {
    const previous = tokens[index - 1];
    const token = tokens[index];
    assert.equal(previous.line === token.line && token.start < previous.start + previous.length, false);
  }
});
