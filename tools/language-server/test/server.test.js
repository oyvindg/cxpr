const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const { execFile } = require("node:child_process");
const test = require("node:test");
const { TextDocument } = require("vscode-languageserver-textdocument");
const { asRange, findTooling, wordAt } = require("../server");

test("resolves the authoritative workspace tooling", () => {
  const root = path.resolve(__dirname, "../../../../..");
  assert.match(findTooling("", [root]), /cxpr_document_tooling$/);
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

function runTooling(executable, sourceName, source) {
  return new Promise((resolve, reject) => {
    const child = execFile(executable, ["--source-name", sourceName], { encoding: "utf8" },
      (error, stdout) => error ? reject(error) : resolve(JSON.parse(stdout)));
    child.stdin.end(source);
  });
}

test("C tooling owns parameter and unknown-reference diagnostics", async () => {
  const root = path.resolve(__dirname, "../../../../..");
  const tooling = findTooling("", [root]);
  const valid = await runTooling(tooling, "valid.cxpr",
    "model valid\nin close\n$period = 14\nvalue = rsi(close, $period)\n");
  assert.deepEqual(valid.diagnostics, []);

  const invalid = await runTooling(tooling, "invalid.cxpr",
    "model invalid\nin close\nvalue = rsi(close, $period)\n");
  assert.match(invalid.diagnostics[0].message, /unknown constant/i);
});

test("C tooling publishes compiler value-type diagnostics to editors", async () => {
  const root = path.resolve(__dirname, "../../../../..");
  const tooling = findTooling("", [root]);
  const result = await runTooling(tooling, "invalid-types.cxpr",
    "model invalid_types\nvalue = 1 and 2\nout value\n");

  assert.equal(result.ok, true);
  assert.equal(result.diagnostics.length, 1);
  assert.match(result.diagnostics[0].message, /type error.*bool operands/i);
});

test("value typing syntax fixtures are enforced by C tooling", async () => {
  const root = path.resolve(__dirname, "../../../../..");
  const tooling = findTooling("", [root]);
  const fixtureRoot = path.join(root, "libs", "cxpr", "tests", "fixtures", "syntax");
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
