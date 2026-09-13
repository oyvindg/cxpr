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

From the Dynasty repository, the same artifact is forwarded by:

```sh
build/libs/dyn/cli cxpr-language-server --stdio
```
