# CXPR Language

Thin VS Code client for the standalone CXPR language server.

Current support:

- `.cxpr` language registration
- TextMate syntax highlighting
- standalone server artifact from `tools/language-server`
- bundled `cxpr_document_tooling` executable for the build platform
- LanguageClient activation in `extension.js`
- C-core-backed symbols, folding, semantic tokens and diagnostics over LSP
- no CXPR parser, builtin catalogue or declaration analysis in the VSIX
- bracket/comment configuration

## Build VSIX

From the CXPR repo root:

```bash
cmake --build build --target cxpr_document_tooling
```

Or directly:

```bash
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

The command replaces the installed `local.cxpr-language` extension. Set
`CXPR_VSCODE_CLI` when the VS Code CLI is not available as `code`, for example:

```bash
CXPR_VSCODE_CLI=code-insiders npm run install:extension
```
