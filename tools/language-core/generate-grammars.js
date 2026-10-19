#!/usr/bin/env node
"use strict";

const fs = require("fs");
const path = require("path");
const { monarchGrammar, textMateGrammar } = require("./cxpr-grammar");
const languageSpec = require("./cxpr-language-spec.json");

const root = __dirname;
const cString = (value) => JSON.stringify(value);
const cLanguageSpec = [
  "/* Generated from cxpr-language-spec.json. Do not edit. */",
  "static const char* const tooling_keywords[] = {",
  ...languageSpec.keywords.map((keyword) => `    ${cString(keyword)},`),
  "};",
  ""
].join("\n");
const languageServerProxy = "\"use strict\";\n\nmodule.exports = require(\"../language-core/cxpr-language-core\");\n";
const legacyLanguageServiceProxy = "\"use strict\";\n\nmodule.exports = require(\"../../language-core/cxpr-language-core\");\n";
const outputs = new Map([
  [path.join(root, "..", "vscode-cxpr", "syntaxes", "cxpr.tmLanguage.json"),
    `${JSON.stringify(textMateGrammar(), null, 2)}\n`],
  [path.join(root, "generated", "cxpr.monarch.json"), `${JSON.stringify(monarchGrammar(), null, 2)}\n`],
  [path.join(root, "generated", "cxpr_language_spec.inc"), cLanguageSpec],
  [path.join(root, "..", "language-server", "cxpr-language-core.js"), languageServerProxy],
  [path.join(root, "..", "language-service", "legacy", "cxpr-language-core.js"), legacyLanguageServiceProxy]
]);

const check = process.argv.includes("--check");
let stale = false;
for (const [file, content] of outputs) {
  if (check) {
    if (!fs.existsSync(file) || fs.readFileSync(file, "utf8") !== content) {
      console.error(`Generated grammar is stale: ${path.relative(process.cwd(), file)}`);
      stale = true;
    }
  } else {
    fs.mkdirSync(path.dirname(file), { recursive: true });
    fs.writeFileSync(file, content);
    console.log(path.relative(process.cwd(), file));
  }
}
if (stale) process.exitCode = 1;
