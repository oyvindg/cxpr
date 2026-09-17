const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { execFile } = require("node:child_process");
const test = require("node:test");
const { TextDocument } = require("vscode-languageserver-textdocument");
const { asRange, callAt, completionItems, findTooling, importedModelHover, tokenAt, wordAt } = require("../server");
const sourceFeatures = require("../source-language-features");
const vscodeShim = require("../vscode-lsp-shim");
const cxprRoot = path.resolve(__dirname, "../../..");
const toolingRoots = [cxprRoot, path.resolve(cxprRoot, "../..")];

test("resolves the authoritative workspace tooling", () => {
  assert.match(findTooling("", toolingRoots), /cxpr_document_tooling$/);
});

test("prefers configured, environment, bundled, workspace and PATH tooling in that order", () => {
  const temporaryRoot = fs.mkdtempSync(path.join(require("node:os").tmpdir(), "cxpr-tooling-"));
  const makeExecutable = (relativePath) => {
    const executable = path.join(temporaryRoot, relativePath);
    fs.mkdirSync(path.dirname(executable), { recursive: true });
    fs.writeFileSync(executable, "#!/bin/sh\n", { mode: 0o755 });
    return executable;
  };
  const configured = makeExecutable("configured");
  const bundled = makeExecutable("bundled");
  const pathExecutable = makeExecutable("bin/cxpr_document_tooling");
  const previous = process.env.CXPR_DOCUMENT_TOOLING;
  delete process.env.CXPR_DOCUMENT_TOOLING;
  try {
    assert.equal(findTooling(configured, [], bundled, path.dirname(pathExecutable)), configured);
    assert.equal(findTooling("", [], bundled, path.dirname(pathExecutable)), bundled);
    assert.equal(findTooling("", [], "", path.dirname(pathExecutable)), pathExecutable);
  } finally {
    if (previous === undefined) delete process.env.CXPR_DOCUMENT_TOOLING;
    else process.env.CXPR_DOCUMENT_TOOLING = previous;
    fs.rmSync(temporaryRoot, { recursive: true, force: true });
  }
});

test("maps authoritative spans to LSP ranges", () => {
  assert.deepEqual(
    asRange({ startLine: 2, startChar: 3, endLine: 4, endChar: 5 }),
    { start: { line: 2, character: 3 }, end: { line: 4, character: 5 } }
  );
});

test("finds dollar-prefixed parameters for definition and hover requests", () => {
  const document = TextDocument.create("file:///model.cxpr", "cxpr", 1,
    "in $period = 14\nvalue = rsi(close, $period)");
  assert.equal(wordAt(document, { line: 1, character: 22 }), "$period");
});

test("finds host-block properties for hover requests", () => {
  const result = {
    tokens: [{ line: 1, start: 4, length: 14, type: "property" }]
  };

  assert.deepEqual(tokenAt(result, { line: 1, character: 10 }), result.tokens[0]);
});

test("describes named properties on an imported model call", () => {
  const document = TextDocument.create("file:///model.cxpr", "cxpr", 1,
    "daily_atr = atr(period = 14, timeframe = \"1d\")");
  assert.deepEqual(callAt(document, { line: 0, character: 14 }, "atr"), {
    signature: "atr(period = 14, timeframe = \"1d\")",
    argumentCount: 2,
    properties: ["period", "timeframe"]
  });
});

test("serves completion items from the shared LSP implementation", () => {
  const document = TextDocument.create("file:///model.cxpr", "cxpr", 1,
    "model demo\nvalue = rs");
  const items = completionItems(document, {
    functions: [{ name: "rsi", builtin: true }],
    outline: [{ name: "value", kind: "binding" }]
  }, { line: 1, character: 10 });
  const rsi = items.find((item) => item.label === "rsi");
  assert.equal(rsi.detail, "CXPR builtin");
  assert.deepEqual(rsi.textEdit.range, {
    start: { line: 1, character: 8 },
    end: { line: 1, character: 10 }
  });
  assert.ok(items.some((item) => item.label === "model"));
  assert.ok(items.some((item) => item.label === "value"));
});

test("renders imported model parameters and output properties", () => {
  const hover = importedModelHover(`# Average True Range.\nmodel atr\nin {\n  $period = 14 {\n    description = "Wilder period."\n  }\n}\nout value {\n  label = "ATR"\n  description = "Volatility series."\n}\n`, "atr");
  assert.match(hover, /Average True Range/);
  assert.match(hover, /`period` = `14` — Wilder period/);
  assert.match(hover, /Properties:\n- `value` \(ATR\) — Volatility series/);
});

test("source-aware LSP features provide rich hover and definitions", () => {
  const temporaryRoot = fs.mkdtempSync(path.join(require("node:os").tmpdir(), "cxpr-language-"));
  const indicatorPath = path.join(temporaryRoot, "indicators", "atr.cxpr");
  fs.mkdirSync(path.dirname(indicatorPath), { recursive: true });
  fs.writeFileSync(indicatorPath, `# Average True Range.\nmodel atr\nin {\n  $period = 14 {\n    description = "Wilder period."\n  }\n}\nout value {\n  label = "ATR"\n  description = "Volatility series."\n}\n`);
  const source = `use { atr } from indicators\n$risk = 2 { description = "Risk multiplier." }\natr_value = atr(period = 14)\nsignal = atr_value.value * $risk\n`;
  const sourcePath = path.join(temporaryRoot, "strategy.cxpr");
  const document = TextDocument.create(`file://${sourcePath}`, "cxpr", 1, source);
  vscodeShim.configureWorkspaceRoots([temporaryRoot]);
  sourceFeatures.configureImportRoots([temporaryRoot]);
  const adapted = vscodeShim.asDocument(document);
  const hoverProvider = new sourceFeatures.CxprHoverProvider();
  const definitionProvider = new sourceFeatures.CxprDefinitionProvider();
  try {
    const atrHover = hoverProvider.provideHover(adapted, { line: 2, character: 13 });
    assert.match(atrHover.contents.value, /Average True Range/);
    assert.match(atrHover.contents.value, /Wilder period/);
    assert.match(atrHover.contents.value, /Volatility series/);

    const paramHover = hoverProvider.provideHover(adapted, { line: 3, character: 30 });
    assert.match(paramHover.contents.value, /Risk multiplier/);

    const importDefinition = definitionProvider.provideDefinition(adapted, { line: 0, character: 7 });
    assert.equal(importDefinition.uri, `file://${indicatorPath}`);

    const propertyDefinition = definitionProvider.provideDefinition(adapted, { line: 3, character: 20 });
    assert.equal(propertyDefinition.uri, `file://${indicatorPath}`);
    assert.equal(propertyDefinition.range.start.line, 7);
  } finally {
    fs.rmSync(temporaryRoot, { recursive: true, force: true });
  }
});

test("source-aware tokens color use keyword and imported module separately", () => {
  const tokens = sourceFeatures.buildDocumentTokens("use wellbeing\n");
  assert.deepEqual(tokens.filter((token) => token.line === 0), [
    { line: 0, start: 0, length: 3, type: "keyword" },
    { line: 0, start: 4, length: 9, type: "importPath" }
  ]);
});

test("list imports resolve files and namespace definitions", () => {
  const temporaryRoot = fs.mkdtempSync(path.join(require("node:os").tmpdir(), "cxpr-list-use-"));
  const tsRoot = path.join(temporaryRoot, "ts");
  fs.mkdirSync(tsRoot, { recursive: true });
  fs.writeFileSync(path.join(tsRoot, "pivots.cxpr"), "fn ts_pivot_low(value) = value\n");
  fs.writeFileSync(path.join(tsRoot, "window.cxpr"), "fn ts_window_span(value) = value\n");
  const sourcePath = path.join(temporaryRoot, "trendline.cxpr");
  const document = TextDocument.create(`file://${sourcePath}`, "cxpr", 1,
    "use pivots, ts from ts\n");
  vscodeShim.configureWorkspaceRoots([temporaryRoot]);
  sourceFeatures.configureImportRoots([temporaryRoot]);
  const provider = new sourceFeatures.CxprDefinitionProvider();
  const adapted = vscodeShim.asDocument(document);
  try {
    const pivots = provider.provideDefinition(adapted, { line: 0, character: 5 });
    assert.equal(pivots.uri, `file://${path.join(tsRoot, "pivots.cxpr")}`);
    const namespace = provider.provideDefinition(adapted, { line: 0, character: 13 });
    assert.deepEqual(namespace.map((location) => path.basename(new URL(location.uri).pathname)),
      ["pivots.cxpr", "window.cxpr"]);
  } finally {
    fs.rmSync(temporaryRoot, { recursive: true, force: true });
  }
});

test("parameter metadata names do not override builtin function colors", () => {
  const source = `in {\n  high\n  low\n  $left = 2 { min = 1, max = 512 }\n}\nwindow_high = max(window(high, 5))\nwindow_low = min(window(low, 5))\n`;
  const tokens = sourceFeatures.buildDocumentTokens(source);
  for (const line of [5, 6]) {
    const call = tokens.find((token) => token.line === line && token.type === "function");
    assert.ok(call, `missing function token on line ${line + 1}`);
    assert.match(source.split("\n")[line].slice(call.start, call.start + call.length), /^(?:max|min)$/);
  }
});

function runTooling(executable, sourceName, source) {
  return new Promise((resolve, reject) => {
    const child = execFile(executable, ["--source-name", sourceName], { encoding: "utf8" },
      (error, stdout) => error ? reject(error) : resolve(JSON.parse(stdout)));
    child.stdin.end(source);
  });
}

test("C tooling owns parameter and unknown-reference diagnostics", async () => {
  const tooling = findTooling("", toolingRoots);
  const valid = await runTooling(tooling, "valid.cxpr",
    "model valid\nin close\n$period = 14\nvalue = rsi(close, $period)\n");
  assert.deepEqual(valid.diagnostics, []);

  const invalid = await runTooling(tooling, "invalid.cxpr",
    "model invalid\nin close\nvalue = rsi(close, $period)\n");
  assert.match(invalid.diagnostics[0].message, /unknown constant/i);
});

test("C tooling publishes compiler value-type diagnostics to editors", async () => {
  const tooling = findTooling("", toolingRoots);
  const result = await runTooling(tooling, "invalid-types.cxpr",
    "model invalid_types\nvalue = 1 and 2\nout value\n");

  assert.equal(result.ok, true);
  assert.equal(result.diagnostics.length, 1);
  assert.match(result.diagnostics[0].message, /type error.*bool operands/i);
});

test("C tooling does not require a model name for host-block manifests", async () => {
  const tooling = findTooling("", toolingRoots);
  const result = await runTooling(tooling, "scenario.cxpr",
    "scenario office_default { schema_version = 1 }\nroster { agent kari { name = \"Kari\" } }\n");

  assert.equal(result.ok, true);
  assert.deepEqual(result.diagnostics, []);
  assert.equal(result.outline[0].kind, "hostBlock");
});

test("value typing syntax fixtures are enforced by C tooling", async () => {
  const tooling = findTooling("", toolingRoots);
  const fixtureRoot = path.join(cxprRoot, "tests", "fixtures", "syntax");
  const validPath = path.join(fixtureRoot, "values_valid.cxpr");
  const valid = await runTooling(tooling, validPath, fs.readFileSync(validPath, "utf8"));
  assert.deepEqual(valid.diagnostics, []);
  const contractPath = path.join(fixtureRoot, "value_type_contracts.cxpr");
  const contract = await runTooling(
    tooling, contractPath, fs.readFileSync(contractPath, "utf8"));
  assert.deepEqual(contract.diagnostics, []);
  const runtimeFieldPath = path.join(fixtureRoot, "values_invalid_field.cxpr");
  const runtimeField = await runTooling(
    tooling, runtimeFieldPath, fs.readFileSync(runtimeFieldPath, "utf8"));
  assert.deepEqual(runtimeField.diagnostics, []);

  const invalid = [
    ["values_invalid_operator.cxpr", /bool operands/i],
    ["values_invalid_index.cxpr", /finite non-negative integer/i],
    ["values_invalid_function_arg.cxpr", /bool argument/i]
  ];
  for (const [name, expected] of invalid) {
    const fixturePath = path.join(fixtureRoot, name);
    const result = await runTooling(
      tooling, fixturePath, fs.readFileSync(fixturePath, "utf8"));
    assert.equal(result.diagnostics.length, 1, name);
    assert.match(result.diagnostics[0].message, expected, name);
  }
});
