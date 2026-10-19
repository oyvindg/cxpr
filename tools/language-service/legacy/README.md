# Legacy parity oracle

This directory is test-only. The three JavaScript files are byte-for-byte copies from cxpr commit
`9290d61be5eebbc02bb7ea34f17249528a9cc609` (the last revision before the language-service cutover):

- `source-language-features.js`
- `cxpr-language-core.js`
- `vscode-lsp-shim.js`

They preserve the old source-aware hover, definition, and token behavior as a parity oracle. They are
not exported by `@cxpr/language-service`, included in its published `files`, or imported by production
entrypoints. `test/legacy-parity.test.js` adapts their editor-shaped results to semantic facts before
comparison because the new service intentionally owns neutral DTOs and C now owns completion context.

The old server mixed this module with analyzer JSON for diagnostics, completions, and symbols. The
test reconstructs those small historical mappings from the same analyzer result and records every
intentional normalization next to the comparison.
