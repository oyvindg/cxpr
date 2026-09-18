# CXPR language server

Standalone LSP host for CXPR. It delegates language analysis to
`cxpr_document_tooling`, built from `libs/cxpr`, and therefore contains neither
a CXPR parser nor a builtin-function catalogue.

```sh
npm install
npm run build
CXPR_DOCUMENT_TOOLING=../../../../build/libs/cxpr/cxpr_document_tooling \
  dist/server.js --stdio
```

The output in `dist/server.js` is the reusable server artifact. VS Code packages
it unchanged, and `dyn_cli` or another editor can launch the same command over
stdio without any VS Code dependency.

The server resolves `cxpr_document_tooling` from the explicit editor setting,
`CXPR_DOCUMENT_TOOLING`, a bundled executable, the workspace build tree, or
`PATH`, in that order.

Hover and definition requests use the complete CXPR source resolver migrated
from the original VS Code extension. It covers parameters, inputs, states,
records, local functions, grouped imports, imported models and output
properties. Editors may provide additional bundled library roots during LSP
initialization.

From the Dynasty repository, the same artifact is forwarded by:

```sh
build/libs/dyn/cli cxpr-language-server --stdio
```
