# CXPR Language

Thin VS Code client for the standalone CXPR language server.

Current support:

- `.cxpr` language registration
- TextMate syntax highlighting
- standalone server artifact from `tools/language-server`
- bundled portable CXPR WASM analyzer (default on Linux, macOS and Windows)
- explicit native/CLI development fallbacks; no platform-native matrix in the VSIX
- LanguageClient activation in `extension.js`
- C-core-backed symbols, folding, semantic tokens and diagnostics over LSP
- no CXPR parser, builtin catalogue or declaration analysis in the VSIX
- bracket/comment configuration

## Build VSIX

Build the shared artifacts, then package the extension:

```bash
npm --prefix ../cxpr-node run build:wasm
npm --prefix ../cxpr-node run build:ts
npm --prefix tools/language-server install
npm --prefix tools/language-server run build
npm --prefix tools/vscode-cxpr install
npm --prefix tools/vscode-cxpr run package:vsix
```

Output:

- `tools/vscode-cxpr/dist/cxpr-language-<version>.vsix`

## Install in VS Code

Build and force-install the current extension through the VS Code CLI:

```bash
cd tools/vscode-cxpr
npm run install:extension
```

From the Dynasty repo root, the equivalent command is:

```bash
./cli install-vsix-cxpr
```

This single command activates a configured Emscripten SDK, builds WASM, the TypeScript wrapper,
the shared language service and language server, packages the VSIX, and installs it. The CLI checks
`CXPR_EMSDK`, an already active `EMSDK`, `/tmp/cxpr-emsdk`, and `.emsdk` in the repository root.
Use `CXPR_EMSDK=/path/to/emsdk ./cli install-vsix-cxpr` for another SDK location.

The command replaces the installed `local.cxpr-language` extension. Set
`CXPR_VSCODE_CLI` when the VS Code CLI is not available as `code`, for example:

```bash
CXPR_VSCODE_CLI=code-insiders npm run install:extension
```
