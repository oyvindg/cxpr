"use strict";

const { CXPR_KEYWORDS, CXPR_TYPE_NAMES } = require("./cxpr-language-core");

/*
 * Canonical lexical grammar. Keep regex sources here; generated editor assets
 * must never acquire editor-specific copies of these patterns.
 */
const CXPR_GRAMMAR = Object.freeze({
  keywords: [...CXPR_KEYWORDS],
  types: [...CXPR_TYPE_NAMES],
  patterns: Object.freeze({
    lineCommentHash: "#.*$",
    lineCommentSlash: "//.*$",
    blockCommentBegin: "/\\*",
    blockCommentEnd: "\\*/",
    doubleString: "\"(?:\\\\.|[^\"\\\\])*\"",
    singleString: "'(?:\\\\.|[^'\\\\])*'",
    parameter: "\\$[A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)*",
    identifier: "[A-Za-z_][A-Za-z0-9_]*",
    type: `\\b(?:${CXPR_TYPE_NAMES.join("|")})\\b`,
    keyword: `\\b(?:${CXPR_KEYWORDS.join("|")})\\b`,
    function: "\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\()",
    property: "\\.[A-Za-z_][A-Za-z0-9_]*",
    number: "(?:\\d+(?:\\.\\d+)?|\\.\\d+)(?:[eE][+-]?\\d+)?(?:i64)?",
    operator: "<=|>=|==|!=|&&|\\|\\||\\|>|\\*\\*|[-+*/%^<>=!?:]",
    punctuation: "[.,]",
    brackets: "[{}\\[\\]()]",
    importDirect: "\\b(use)(\\s+)([A-Za-z0-9_./-]*\\/[A-Za-z0-9_./-]+)",
    importFlat: "\\b(use)(\\s+)([A-Za-z_][A-Za-z0-9_]*(?:\\s*,\\s*[A-Za-z_][A-Za-z0-9_]*)*)(\\s+)(from)(\\s+)([A-Za-z0-9_./-]+)",
    importGrouped: "\\b(use)(\\s*\\{)([^}]*)(\\})(\\s+)(from)(\\s+)([A-Za-z0-9_./-]+)",
    namedBlock: "\\b(name|model)(\\s+)([A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)?)(?=\\s*\\{)",
    inputBlockBegin: "\\b(in)(\\s+)([A-Za-z_][A-Za-z0-9_]*)(\\s*)(\\{)",
    inputListBegin: "\\b(in)(\\s+)([A-Za-z_][A-Za-z0-9_]*)(?=\\s*,)",
    inputDeclaration: "\\b(in)(\\s+)([A-Za-z_][A-Za-z0-9_]*)(?=\\s*(?:,|\\{|$))",
    outputDeclaration: "\\b(out)(\\s+)([A-Za-z_][A-Za-z0-9_]*)(?=\\s*(?:\\{|$))",
    stateUpdate: "\\b(update)(\\s+)(state)(?=\\s*\\{)",
    outputAssignment: "\\b(out|state)(\\s+)([A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)?)(?=\\s*=)",
    optimizeMarker: "\\boptimize(?=\\s*(?:=|\\{))",
    metadataAssignment: "^(\\s*)(type|timeframe)(\\s*)(=)",
    lineAssignment: "^(\\s*)([A-Za-z_][A-Za-z0-9_]*)(\\s*)(=)",
    namedArgument: "[A-Za-z_][A-Za-z0-9_]*(?=\\s*=)",
    block: "\\b[A-Za-z_][A-Za-z0-9_]*(?=\\s*\\{)",
    plotBlock: "\\bplot(?=\\s*\\{)",
    hostBlock: "^\\s*((?!name\\b|model\\b|use\\b|in\\b|fn\\b|update\\b|out\\b|state\\b|meta\\b|assert\\b|optimize\\b)[A-Za-z_][A-Za-z0-9_]*)(?:\\s+([A-Za-z_][A-Za-z0-9_.]*))?(?=\\s*\\{)",
    functionDeclaration: "\\b(fn)\\s+([A-Za-z_][A-Za-z0-9_]*)(?=\\s*\\()",
    documentString: "\\b(type|id|label|description|hover|mode|source|broker|symbol|strategy|aggregate_from|lifecycle|source_arg)\\b(\\s*=\\s*)([A-Za-z_][A-Za-z0-9_.-]*)(?=\\s*(?:[,}\\]#]|//|$))",
    documentStringArray: "\\b(books|hedging)\\b(\\s*=\\s*\\[\\s*)([A-Za-z_][A-Za-z0-9_.-]*)(?=\\s*(?:,|\\]))",
    parameterAssignment: "^(\\s*)(\\$[A-Za-z_][A-Za-z0-9_]*(?:\\.[A-Za-z_][A-Za-z0-9_]*)*)(\\s*)(=)"
  })
});

const TOKEN_SCOPE = Object.freeze({
  comment: "comment.line.cxpr", string: "string.quoted.cxpr", parameter: "variable.parameter.cxpr",
  type: "entity.name.type.cxpr", keyword: "keyword.control.cxpr", function: "entity.name.function.cxpr",
  property: "variable.other.property.cxpr", number: "constant.numeric.cxpr",
  operator: "keyword.operator.cxpr", punctuation: "punctuation.cxpr"
});

function lexicalPattern(token, p) {
  if (token === "comment") return p.lineCommentHash;
  if (token === "string") return `(?:${p.doubleString}|${p.singleString})`;
  return p[token];
}

function textMateGrammar() {
  const p = CXPR_GRAMMAR.patterns;
  const simple = (name, match) => ({ name, match });
  return {
    "$schema": "https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json",
    name: "CXPR", scopeName: "source.cxpr",
    "x-cxpr-generated": true,
    "x-cxpr-token-patterns": Object.fromEntries(Object.entries(TOKEN_SCOPE).map(([token, scope]) =>
      [token, { pattern: lexicalPattern(token, p), scope }])),
    patterns: [
      simple("comment.line.number-sign.cxpr", p.lineCommentHash),
      simple("comment.line.double-slash.cxpr", p.lineCommentSlash),
      { name: "comment.block.cxpr", begin: p.blockCommentBegin, end: p.blockCommentEnd },
      simple("string.quoted.double.cxpr", p.doubleString), simple("string.quoted.single.cxpr", p.singleString),
      { match: p.documentString, captures: { "1": { name: "variable.other.assignment.cxpr" }, "2": { name: "keyword.operator.assignment.cxpr" }, "3": { name: "string.unquoted.document.cxpr" } } },
      { match: p.documentStringArray, captures: { "1": { name: "variable.other.assignment.cxpr" }, "2": { name: "keyword.operator.assignment.cxpr" }, "3": { name: "string.unquoted.document.cxpr" } } },
      { match: p.importGrouped, captures: { "1": { name: "keyword.control.cxpr" }, "3": { name: "entity.name.function.cxpr" }, "6": { name: "keyword.control.cxpr" }, "8": { name: "string.unquoted.import-path.cxpr" } } },
      { match: p.importDirect, captures: { "1": { name: "keyword.control.cxpr" }, "3": { name: "string.unquoted.import-path.cxpr" } } },
      { match: p.importFlat, captures: { "1": { name: "keyword.control.cxpr" }, "3": { name: "entity.name.function.cxpr" }, "5": { name: "keyword.control.cxpr" }, "7": { name: "string.unquoted.import-path.cxpr" } } },
      { match: p.namedBlock, captures: { "1": { name: "keyword.control.cxpr" }, "3": { name: "variable.other.assignment.cxpr" } } },
      { begin: p.inputBlockBegin, beginCaptures: {
        "1": { name: "keyword.control.cxpr" },
        "3": { name: "variable.other.assignment.cxpr" },
        "5": { name: "punctuation.cxpr" }
      }, end: "\\}", endCaptures: { "0": { name: "punctuation.cxpr" } }, patterns: [
        simple("variable.other.input.cxpr", p.identifier),
        simple("punctuation.cxpr", p.punctuation)
      ] },
      { begin: p.inputListBegin, beginCaptures: {
        "1": { name: "keyword.control.cxpr" },
        "3": { name: "variable.other.input.cxpr" }
      }, end: "$", patterns: [
        simple("variable.other.input.cxpr", p.identifier),
        simple("punctuation.cxpr", p.punctuation)
      ] },
      { match: p.inputDeclaration, captures: { "1": { name: "keyword.control.cxpr" }, "3": { name: "variable.other.input.cxpr" } } },
      { match: p.outputDeclaration, captures: { "1": { name: "keyword.control.cxpr" }, "3": { name: "variable.other.cxpr" } } },
      { match: p.stateUpdate, captures: { "1": { name: "keyword.control.cxpr" }, "3": { name: "keyword.control.cxpr" } } },
      { match: p.outputAssignment, captures: { "1": { name: "keyword.control.cxpr" }, "3": { name: "variable.other.assignment.cxpr" } } },
      simple("variable.other.named-argument.cxpr", p.optimizeMarker),
      { match: p.metadataAssignment, captures: { "2": { name: "variable.other.named-argument.cxpr" }, "4": { name: "keyword.operator.cxpr" } } },
      { match: p.lineAssignment, captures: { "2": { name: "variable.other.assignment.cxpr" }, "4": { name: "keyword.operator.assignment.cxpr" } } },
      { match: p.parameterAssignment, captures: { "2": { name: "variable.other.assignment.cxpr" }, "4": { name: "keyword.operator.assignment.cxpr" } } },
      { match: p.functionDeclaration, captures: { "1": { name: "keyword.control.cxpr" }, "2": { name: "entity.name.function.cxpr" } } },
      { match: p.hostBlock, captures: { "1": { name: "keyword.control.cxpr" }, "2": { name: "variable.other.block-label.cxpr" } } },
      simple("variable.other.block-label.cxpr", p.plotBlock),
      simple("variable.other.block-label.cxpr", p.block),
      simple(TOKEN_SCOPE.parameter, p.parameter), simple(TOKEN_SCOPE.type, p.type),
      simple(TOKEN_SCOPE.keyword, p.keyword), simple(TOKEN_SCOPE.function, p.function),
      simple("variable.other.named-argument.cxpr", p.namedArgument), simple(TOKEN_SCOPE.property, p.property),
      simple(TOKEN_SCOPE.number, p.number), simple(TOKEN_SCOPE.operator, p.operator),
      simple(TOKEN_SCOPE.punctuation, p.punctuation)
    ]
  };
}

function monarchGrammar() {
  const p = CXPR_GRAMMAR.patterns;
  return {
    // Monaco adds the language id postfix when it emits CSS token classes.
    defaultToken: "variable", keywords: CXPR_GRAMMAR.keywords,
    tokenPatterns: Object.fromEntries(Object.entries(TOKEN_SCOPE).map(([token]) =>
      [token, lexicalPattern(token, p)])),
    tokenizer: { root: [
      [p.lineCommentHash, "comment"], [p.lineCommentSlash, "comment"],
      [p.blockCommentBegin, "comment", "@blockComment"],
      ["\"([^\"\\\\]|\\\\.)*$", "invalid"], ["'([^'\\\\]|\\\\.)*$", "invalid"],
      ["\"", "string", "@doubleString"], ["'", "string", "@singleString"],
      [p.importGrouped, ["keyword", "punctuation", "import", "punctuation", "white", "keyword", "white", "importPath"]],
      [p.importDirect, ["keyword", "white", "importPath"]],
      [p.importFlat, ["keyword", "white", "import", "white", "keyword", "white", "importPath"]],
      [p.namedBlock, ["keyword", "white", "assignment"]],
      [p.inputBlockBegin, ["keyword", "white", "assignment", "white",
        { token: "punctuation", next: "@inputBlock" }]],
      [p.inputListBegin, ["keyword", "white", { token: "input", next: "@inputList" }]],
      [p.inputDeclaration, ["keyword", "white", "input"]],
      [p.outputDeclaration, ["keyword", "white", "variable"]],
      [p.stateUpdate, ["keyword", "white", "keyword"]],
      [p.outputAssignment, ["keyword", "white", "assignment"]],
      [p.optimizeMarker, "namedArgument"],
      [p.metadataAssignment, ["white", "namedArgument", "white", "operator"]],
      [p.lineAssignment, ["white", "assignment", "white", "operator"]],
      [p.parameterAssignment, ["white", "assignment", "white", "operator"]],
      [p.plotBlock, "plotBlock"], [p.hostBlock, ["block", "block"]],
      [p.block, "block"],
      [p.parameter, "parameter"], [p.type, "type"], [p.keyword, "keyword"],
      [p.function, "function"],
      [p.namedArgument, "namedArgument"], [p.property, "property"],
      [p.identifier, { cases: { "@keywords": "keyword", "@default": "reference" } }],
      [p.number, "number"], [p.operator, "operator"],
      [p.brackets, "@brackets"], [p.punctuation, "punctuation"], ["\\s+", "white"]
    ], inputBlock: [[p.identifier, "input"], ["\\}", "punctuation", "@pop"],
      [p.punctuation, "punctuation"], ["\\s+", "white"]],
    inputList: [[p.lineCommentHash, "comment", "@pop"], [p.lineCommentSlash, "comment", "@pop"],
      [`${p.identifier}(?=\\s*$)`, "input", "@pop"], [p.identifier, "input"],
      [p.punctuation, "punctuation"], ["\\s+", "white"]],
    blockComment: [["[^*/]+", "comment"], ["\\*/", "comment", "@pop"], ["[*/]", "comment"]],
    doubleString: [["[^\\\\\"]+", "string"], ["\\\\.", "escape"], ["\"", "string", "@pop"]],
    singleString: [["[^\\\\']+", "string"], ["\\\\.", "escape"], ["'", "string", "@pop"]] }
  };
}

module.exports = { CXPR_GRAMMAR, monarchGrammar, textMateGrammar };
