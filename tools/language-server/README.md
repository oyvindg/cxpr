# CXPR language server

Standalone LSP host for CXPR. On Linux x64 it uses `cxpr-node` in-process. Other
platforms use the bundled WASM analyzer. It contains neither a CXPR parser nor a
builtin-function catalogue.

```sh
npm install
npm run build
dist/server.js --stdio
```

The output in `dist/server.js` is the reusable server artifact. VS Code packages
it unchanged, and `dyn_cli` or another editor can launch the same command over
stdio without any VS Code dependency.

The old CLI transport is an explicit emergency fallback only:
`CXPR_ANALYZER=cli CXPR_DOCUMENT_TOOLING=/path/to/cxpr_document_tooling`.

Hover and definition requests use the complete CXPR source resolver migrated
from the original VS Code extension. It covers parameters, inputs, states,
records, local functions, grouped imports, imported models and output
properties. Editors may provide additional bundled library roots during LSP
initialization.

Projects may declare portable module and source roots in the nearest
`cxpr.json`:

```json
{
  "moduleRoots": ["modules"],
  "sourceRoots": ["examples", "modules"]
}
```

Paths are relative to the configuration file. A monorepo may contain several
`cxpr.json` files; the nearest configuration to each source file wins. LSP
clients may provide additional `moduleRoots` and `sourceRoots` through
initialization options. Without configuration, the server falls back to the
nearest ancestor `modules/` directory and workspace-folder roots.

From the Dynasty repository, the same artifact is forwarded by:

```sh
build/libs/dyn/cli cxpr-language-server --stdio
```
