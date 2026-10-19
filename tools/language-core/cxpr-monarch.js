"use strict";

const generated = require("./generated/cxpr.monarch.json");

function compileRules(states) {
  return Object.fromEntries(Object.entries(states).map(([state, rules]) => [state,
    rules.map(([pattern, ...action]) => [new RegExp(pattern), ...action])]));
}

/** Return a fresh Monaco Monarch definition backed by the generated asset. */
function createCxprMonarchLanguage() {
  return { defaultToken: generated.defaultToken, keywords: [...generated.keywords],
    tokenizer: compileRules(generated.tokenizer) };
}

module.exports = { createCxprMonarchLanguage };
