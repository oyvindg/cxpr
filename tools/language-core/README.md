# CXPR language assets

`cxpr-language-spec.json` is the host-agnostic source for token types, keywords,
types, operators and punctuation. `cxpr-grammar.js` adds the canonical lexical
patterns. Together they generate:

- `../vscode-cxpr/syntaxes/cxpr.tmLanguage.json` for TextMate/VS Code
- `generated/cxpr.monarch.json` for Monaco, loaded through `cxpr-monarch.js`
- `generated/cxpr_language_spec.inc` for the C document tooling

Run `npm run generate` after changing the canonical grammar. CI/tests should run
`npm run check`; it fails when any generated file was edited or is stale.
Generated files carry no independent source of truth and must not be edited by hand.

`../theme/cxpr-color-profile.json` remains the canonical semantic/syntax color
profile. Color is deliberately separate from lexical grammar so hosts can apply
their own theme while sharing CXPR token names. The language configuration stays
in `../vscode-cxpr/syntaxes/cxpr.language-configuration.json`; it describes editor
brackets/comments rather than token recognition.
