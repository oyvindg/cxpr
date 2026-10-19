"use strict";

const assert = require("node:assert/strict");
const fs = require("node:fs");
const path = require("node:path");
const test = require("node:test");

const root = path.resolve(__dirname, "..");
const textMate = require(path.join(root, "..", "vscode-cxpr", "syntaxes", "cxpr.tmLanguage.json"));
const monarch = require(path.join(root, "generated", "cxpr.monarch.json"));
const languageSpec = require(path.join(root, "cxpr-language-spec.json"));
const colors = require(path.join(root, "..", "theme", "cxpr-color-profile.json"));
const source = fs.readFileSync(path.join(__dirname, "fixtures", "grammar.cxpr"), "utf8");

function matches(patterns) {
  const result = {};
  for (const [token, pattern] of Object.entries(patterns)) {
    const sourcePattern = typeof pattern === "string" ? pattern : pattern.pattern;
    result[token] = [...source.matchAll(new RegExp(sourcePattern, "gm"))].map((match) => match[0]);
  }
  return result;
}

test("TextMate and Monarch expose identical canonical token patterns", () => {
  const textMatePatterns = Object.fromEntries(Object.entries(textMate["x-cxpr-token-patterns"])
    .map(([token, value]) => [token, value.pattern]));
  assert.deepEqual(textMatePatterns, monarch.tokenPatterns);
  assert.deepEqual(matches(textMatePatterns), matches(monarch.tokenPatterns));
});

test("C and editor assets use the canonical language specification", () => {
  const cSpec = fs.readFileSync(path.join(root, "generated", "cxpr_language_spec.inc"), "utf8");
  const cKeywords = [...cSpec.matchAll(/^\s*"([^"]+)",$/gm)].map((match) => match[1]);

  assert.deepEqual(monarch.keywords, languageSpec.keywords);
  assert.deepEqual(cKeywords, languageSpec.keywords);
});

test("Monarch assigns model names the legacy assignment class", () => {
  const rule = monarch.tokenizer.root.find(([pattern]) => pattern.includes("(name|model)"));
  assert.deepEqual(rule?.[1], ["keyword", "white", "assignment"]);
});

test("Monarch preserves legacy input and output declaration classes", () => {
  const input = monarch.tokenizer.root.find((rule) =>
    rule[0].includes("(in)(\\s+)") && rule[1][2] === "input");
  const output = monarch.tokenizer.root.find(([pattern]) => pattern.includes("(out)(\\s+)"));
  assert.deepEqual(input?.[1], ["keyword", "white", "input"]);
  const inputBlock = monarch.tokenizer.root.find((rule) =>
    rule[1]?.some?.((action) => action?.next === "@inputBlock"));
  assert.deepEqual(inputBlock?.[1], ["keyword", "white", "assignment", "white",
    { token: "punctuation", next: "@inputBlock" }]);
  assert.deepEqual(monarch.tokenizer.inputBlock, [
    ["[A-Za-z_][A-Za-z0-9_]*", "input"],
    ["\\}", "punctuation", "@pop"],
    ["[.,]", "punctuation"],
    ["\\s+", "white"]
  ]);
  assert.deepEqual(output?.[1], ["keyword", "white", "variable"]);
});

test("Monarch colors direct imports and every comma-separated input", () => {
  const directImport = monarch.tokenizer.root.find(([pattern]) =>
    pattern.includes("[A-Za-z0-9_./-]*\\/"));
  const inputList = monarch.tokenizer.root.find((rule) =>
    rule[1]?.some?.((action) => action?.next === "@inputList"));

  assert.deepEqual(directImport?.[1], ["keyword", "white", "importPath"]);
  assert.deepEqual(inputList?.[1], ["keyword", "white", { token: "input", next: "@inputList" }]);
  assert.ok(monarch.tokenizer.inputList.some((rule) => rule[1] === "input"));
});

test("Monarch checks keywords before function calls", () => {
  const keywordIndex = monarch.tokenizer.root.findIndex((rule) => rule[1] === "keyword");
  const functionIndex = monarch.tokenizer.root.findIndex((rule) => rule[1] === "function");

  assert.ok(keywordIndex >= 0 && keywordIndex < functionIndex,
    "not ( and and (( must remain keywords instead of function names");
});

test("Monarch treats optimize metadata as a named argument", () => {
  const optimizeIndex = monarch.tokenizer.root.findIndex(([pattern]) =>
    pattern.includes("optimize(?="));
  const identifierIndex = monarch.tokenizer.root.findIndex((rule) =>
    rule[1]?.cases?.["@keywords"] === "keyword");

  assert.ok(optimizeIndex >= 0 && optimizeIndex < identifierIndex);
  assert.equal(monarch.tokenizer.root[optimizeIndex]?.[1], "namedArgument");
});

test("Monarch consumes complete identifiers before classifying exact keywords", () => {
  const identifierRule = monarch.tokenizer.root.find((rule) =>
    rule[1]?.cases?.["@keywords"] === "keyword");

  assert.equal(identifierRule?.[0], "[A-Za-z_][A-Za-z0-9_]*");
  assert.equal(identifierRule?.[1].cases["@default"], "reference");
  assert.ok(monarch.keywords.includes("out"));
  assert.ok(monarch.keywords.includes("as"));
  assert.equal(monarch.keywords.includes("breakout"), false);
  assert.equal(monarch.keywords.includes("price_bias"), false);
});

test("Monarch colors model type and timeframe as metadata", () => {
  const ruleIndex = monarch.tokenizer.root.findIndex(([pattern]) =>
    pattern.includes("type|timeframe"));
  const assignmentIndex = monarch.tokenizer.root.findIndex(([pattern]) =>
    pattern.includes("[A-Za-z_][A-Za-z0-9_]*)(\\s*)(=)"));

  assert.ok(ruleIndex >= 0 && ruleIndex < assignmentIndex);
  assert.deepEqual(monarch.tokenizer.root[ruleIndex]?.[1],
    ["white", "namedArgument", "white", "operator"]);
});

test("shared corpus covers the editor token classes", () => {
  const tokens = matches(monarch.tokenPatterns);
  for (const required of ["comment", "string", "parameter", "keyword", "function", "property", "number", "operator", "punctuation"]) {
    assert.ok(tokens[required].length > 0, `fixture must cover ${required}`);
  }
});

test("the shared color profile covers every canonical token class", () => {
  for (const token of Object.keys(monarch.tokenPatterns)) {
    assert.match(colors.tokens[token], /^#[0-9A-F]{6}$/i, `missing color for ${token}`);
  }
});

test("generated assets have not drifted", () => {
  const result = require("node:child_process").spawnSync(process.execPath,
    [path.join(root, "generate-grammars.js"), "--check"], { cwd: root, encoding: "utf8" });
  assert.equal(result.status, 0, result.stderr);
});
